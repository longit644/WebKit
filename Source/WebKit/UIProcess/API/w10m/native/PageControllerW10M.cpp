#include "PageControllerW10M.h"
#include "EngineResultW10M.h"

#include <cmath>

namespace WebKit {

// Convert frozen operation results by meaning, never by positive-code HRESULT equality.
HRESULT PageControllerW10M::resultToHRESULT(WKW10MResult result)
{
    return engineResultToHRESULT(result);
}

// Validate the exact V1 identity and command capability set before attaching anything.
HRESULT PageControllerW10M::bind(const WKW10MEngineAPI* api, WKW10MPageHandle page, const WKW10MPageCallbacks* callbacks)
{
    if (GetCurrentThreadId() != m_owner) return RPC_E_WRONG_THREAD;
    if (m_page) return E_ILLEGAL_METHOD_CALL;
    if (!api || !page || !callbacks) return E_INVALIDARG;
    if (api->header.structureSize < sizeof(WKW10MEngineAPI) || api->header.structureVersion != WKW10M_STRUCTURE_VERSION
        || api->abiMajor != WKW10M_ABI_MAJOR_VERSION || api->abiMinor < WKW10M_ABI_MINOR_VERSION)
        return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    // These bits correspond to the commands this controller can actually forward.
    constexpr uint64_t required = WKW10MCapabilityStructuredNotifications | WKW10MCapabilityHostHtml | WKW10MCapabilityCertificateDecisions;
    if ((api->capabilities & required) != required) return E_NOTIMPL;
    if (!api->setCallbacks || !api->navigate || !api->loadHtml || !api->goBack || !api->goForward
        || !api->reload || !api->stop || !api->proceedWithCertificateFailure || !api->closePage)
        return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    auto result = resultToHRESULT(api->setCallbacks(page, callbacks));
    if (FAILED(result)) return result;
    m_api = *api;
    m_page = page;
    return S_OK;
}

// Enforce affinity before any engine call, including an unavailable page.
HRESULT PageControllerW10M::checkBound() const
{
    if (GetCurrentThreadId() != m_owner) return RPC_E_WRONG_THREAD;
    return m_page ? S_OK : HRESULT_FROM_WIN32(ERROR_NOT_READY);
}

// Send a borrowed Remote request with empty HTML/decision fields.
HRESULT PageControllerW10M::navigate(const WKW10MString& uri, uint64_t* navigationId)
{
    auto ready = checkBound();
    if (FAILED(ready)) return ready;
    if (!uri.data || !uri.length || !navigationId) return E_INVALIDARG;
    WKW10MNavigationRequest request { { sizeof(request), WKW10M_STRUCTURE_VERSION }, WKW10MContentRemote, uri, { nullptr, 0 }, { 0, 0 } };
    return resultToHRESULT(m_api.navigate(m_page, &request, navigationId));
}

// Send explicit HostHtml data with no URI/base or network-document shortcut.
HRESULT PageControllerW10M::loadHtml(const WKW10MString& html, WKW10MCertificateToken warning, uint64_t* navigationId)
{
    auto ready = checkBound();
    if (FAILED(ready)) return ready;
    if ((html.length && !html.data) || !navigationId || (!!warning.issuer != !!warning.decision)) return E_INVALIDARG;
    WKW10MNavigationRequest request { { sizeof(request), WKW10M_STRUCTURE_VERSION }, WKW10MContentHostHtml, { nullptr, 0 }, html, warning };
    return resultToHRESULT(m_api.loadHtml(m_page, &request, navigationId));
}

// Forward actual engine Back traversal, without a consumer history vector.
HRESULT PageControllerW10M::goBack()
{
    auto ready = checkBound();
    return FAILED(ready) ? ready : resultToHRESULT(m_api.goBack(m_page));
}

// Forward actual engine Forward traversal, without a consumer history vector.
HRESULT PageControllerW10M::goForward()
{
    auto ready = checkBound();
    return FAILED(ready) ? ready : resultToHRESULT(m_api.goForward(m_page));
}

// Forward remote Reload; transient replay uses explicit HostHtml submission in the control.
HRESULT PageControllerW10M::reload()
{
    auto ready = checkBound();
    return FAILED(ready) ? ready : resultToHRESULT(m_api.reload(m_page));
}

// Forward cancellation/subresource Stop with no synthesized main identity.
HRESULT PageControllerW10M::stop()
{
    auto ready = checkBound();
    return FAILED(ready) ? ready : resultToHRESULT(m_api.stop(m_page));
}

// Pass the hidden native capability to atomic provider consumption/new-ID retry.
HRESULT PageControllerW10M::proceed(WKW10MCertificateToken token, uint64_t* navigationId)
{
    auto ready = checkBound();
    if (FAILED(ready)) return ready;
    if (!token.issuer || !token.decision || !navigationId) return E_INVALIDARG;
    return resultToHRESULT(m_api.proceedWithCertificateFailure(m_page, &token, navigationId));
}

// Forward one frozen input event after validating its kind-specific contract.
HRESULT PageControllerW10M::sendInput(const WKW10MInputEvent& event)
{
    auto ready = checkBound();
    if (FAILED(ready))
        return ready;
    if (event.header.structureSize < sizeof(event) || event.header.structureVersion != WKW10M_STRUCTURE_VERSION)
        return E_INVALIDARG;
    if (event.kind > WKW10MInputWheel)
        return E_INVALIDARG;
    // The frozen header defines Alt=1/Control=2/Shift=4/Windows=8; anything else is not a modifier.
    if (event.modifiers & ~0xFu)
        return E_INVALIDARG;
    bool keyboard = event.kind == WKW10MInputKeyDown || event.kind == WKW10MInputKeyUp;
    if (keyboard) {
        if (!event.virtualKey)
            return E_INVALIDARG;
        if (event.unicodeScalar > 0x10FFFFu
            || (event.unicodeScalar >= 0xD800u && event.unicodeScalar <= 0xDFFFu))
            return E_INVALIDARG;
    } else {
        if (!std::isfinite(event.xCssPixels) || !std::isfinite(event.yCssPixels))
            return E_INVALIDARG;
    }
    if (!(m_api.capabilities & WKW10MCapabilityInput))
        return E_NOTIMPL;
    if (!m_api.sendInput)
        return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    return resultToHRESULT(m_api.sendInput(m_page, &event));
}

// Forward focus state through the negotiated slot without inventing engine behavior.
HRESULT PageControllerW10M::setFocused(bool focused)
{
    auto ready = checkBound();
    if (FAILED(ready))
        return ready;
    if (!(m_api.capabilities & WKW10MCapabilityInput))
        return E_NOTIMPL;
    if (!m_api.setFocus)
        return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    return resultToHRESULT(m_api.setFocus(m_page, focused ? 1u : 0u));
}

// Keep Pending ownership until provider close acknowledgement, without hot-unloading code.
HRESULT PageControllerW10M::close()
{
    if (GetCurrentThreadId() != m_owner) return RPC_E_WRONG_THREAD;
    if (m_closeRequested) return m_page ? S_FALSE : S_OK;
    auto ready = checkBound();
    if (FAILED(ready)) return ready;
    m_closeRequested = true;
    auto result = m_api.closePage(m_page);
    if (result == WKW10MResultOK || result == WKW10MResultClosed) {
        m_page = 0;
        return S_OK;
    }
    return result == WKW10MResultPending ? (m_page ? S_FALSE : S_OK) : resultToHRESULT(result);
}

// Treat the engine's retained-context release as cleanup acknowledgement, never a log signal.
void PageControllerW10M::acknowledgeClose()
{
    if (GetCurrentThreadId() == m_owner && m_closeRequested)
        m_page = 0;
}

} // namespace WebKit

