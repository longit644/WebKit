#include "PageControllerUWP.h"
#include "EngineResultUWP.h"

#include <cmath>

namespace WebKit {

// Convert frozen operation results by meaning, never by positive-code HRESULT equality.
HRESULT PageControllerUWP::resultToHRESULT(WKUWPResult result)
{
    return engineResultToHRESULT(result);
}

// Validate the exact V1 identity and command capability set before attaching anything.
HRESULT PageControllerUWP::bind(const WKUWPEngineAPI* api, WKUWPPageHandle page, const WKUWPPageCallbacks* callbacks)
{
    if (GetCurrentThreadId() != m_owner) return RPC_E_WRONG_THREAD;
    if (m_page) return E_ILLEGAL_METHOD_CALL;
    if (!api || !page || !callbacks) return E_INVALIDARG;
    if (api->header.structureSize < sizeof(WKUWPEngineAPI) || api->header.structureVersion != WKUWP_STRUCTURE_VERSION
        || api->abiMajor != WKUWP_ABI_MAJOR_VERSION || api->abiMinor < WKUWP_ABI_MINOR_VERSION)
        return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    // These bits correspond to the commands this controller can actually forward.
    constexpr uint64_t required = WKUWPCapabilityStructuredNotifications | WKUWPCapabilityHostHtml | WKUWPCapabilityCertificateDecisions;
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
HRESULT PageControllerUWP::checkBound() const
{
    if (GetCurrentThreadId() != m_owner) return RPC_E_WRONG_THREAD;
    return m_page ? S_OK : HRESULT_FROM_WIN32(ERROR_NOT_READY);
}

// Send a borrowed Remote request with empty HTML/decision fields.
HRESULT PageControllerUWP::navigate(const WKUWPString& uri, uint64_t* navigationId)
{
    auto ready = checkBound();
    if (FAILED(ready)) return ready;
    if (!uri.data || !uri.length || !navigationId) return E_INVALIDARG;
    WKUWPNavigationRequest request { { sizeof(request), WKUWP_STRUCTURE_VERSION }, WKUWPContentRemote, uri, { nullptr, 0 }, { 0, 0 } };
    return resultToHRESULT(m_api.navigate(m_page, &request, navigationId));
}

// Send explicit HostHtml data with no URI/base or network-document shortcut.
HRESULT PageControllerUWP::loadHtml(const WKUWPString& html, WKUWPCertificateToken warning, uint64_t* navigationId)
{
    auto ready = checkBound();
    if (FAILED(ready)) return ready;
    if ((html.length && !html.data) || !navigationId || (!!warning.issuer != !!warning.decision)) return E_INVALIDARG;
    WKUWPNavigationRequest request { { sizeof(request), WKUWP_STRUCTURE_VERSION }, WKUWPContentHostHtml, { nullptr, 0 }, html, warning };
    return resultToHRESULT(m_api.loadHtml(m_page, &request, navigationId));
}

// Forward actual engine Back traversal, without a consumer history vector.
HRESULT PageControllerUWP::goBack()
{
    auto ready = checkBound();
    return FAILED(ready) ? ready : resultToHRESULT(m_api.goBack(m_page));
}

// Forward actual engine Forward traversal, without a consumer history vector.
HRESULT PageControllerUWP::goForward()
{
    auto ready = checkBound();
    return FAILED(ready) ? ready : resultToHRESULT(m_api.goForward(m_page));
}

// Forward remote Reload; transient replay uses explicit HostHtml submission in the control.
HRESULT PageControllerUWP::reload()
{
    auto ready = checkBound();
    return FAILED(ready) ? ready : resultToHRESULT(m_api.reload(m_page));
}

// Forward cancellation/subresource Stop with no synthesized main identity.
HRESULT PageControllerUWP::stop()
{
    auto ready = checkBound();
    return FAILED(ready) ? ready : resultToHRESULT(m_api.stop(m_page));
}

// Pass the hidden native capability to atomic provider consumption/new-ID retry.
HRESULT PageControllerUWP::proceed(WKUWPCertificateToken token, uint64_t* navigationId)
{
    auto ready = checkBound();
    if (FAILED(ready)) return ready;
    if (!token.issuer || !token.decision || !navigationId) return E_INVALIDARG;
    return resultToHRESULT(m_api.proceedWithCertificateFailure(m_page, &token, navigationId));
}

// Forward one frozen input event after validating its kind-specific contract.
HRESULT PageControllerUWP::sendInput(const WKUWPInputEvent& event)
{
    auto ready = checkBound();
    if (FAILED(ready))
        return ready;
    if (event.header.structureSize < sizeof(event) || event.header.structureVersion != WKUWP_STRUCTURE_VERSION)
        return E_INVALIDARG;
    if (event.kind > WKUWPInputWheel)
        return E_INVALIDARG;
    // The frozen header defines Alt=1/Control=2/Shift=4/Windows=8; anything else is not a modifier.
    if (event.modifiers & ~0xFu)
        return E_INVALIDARG;
    bool keyboard = event.kind == WKUWPInputKeyDown || event.kind == WKUWPInputKeyUp;
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
    if (!(m_api.capabilities & WKUWPCapabilityInput))
        return E_NOTIMPL;
    if (!m_api.sendInput)
        return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    return resultToHRESULT(m_api.sendInput(m_page, &event));
}

// Forward focus state through the negotiated slot without inventing engine behavior.
HRESULT PageControllerUWP::setFocused(bool focused)
{
    auto ready = checkBound();
    if (FAILED(ready))
        return ready;
    if (!(m_api.capabilities & WKUWPCapabilityInput))
        return E_NOTIMPL;
    if (!m_api.setFocus)
        return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    return resultToHRESULT(m_api.setFocus(m_page, focused ? 1u : 0u));
}

// Keep Pending ownership until provider close acknowledgement, without hot-unloading code.
HRESULT PageControllerUWP::close()
{
    if (GetCurrentThreadId() != m_owner) return RPC_E_WRONG_THREAD;
    if (m_closeRequested) return m_page ? S_FALSE : S_OK;
    auto ready = checkBound();
    if (FAILED(ready)) return ready;
    m_closeRequested = true;
    auto result = m_api.closePage(m_page);
    if (result == WKUWPResultOK || result == WKUWPResultClosed) {
        m_page = 0;
        return S_OK;
    }
    return result == WKUWPResultPending ? (m_page ? S_FALSE : S_OK) : resultToHRESULT(result);
}

// Treat the engine's retained-context release as cleanup acknowledgement, never a log signal.
void PageControllerUWP::acknowledgeClose()
{
    if (GetCurrentThreadId() == m_owner && m_closeRequested)
        m_page = 0;
}

} // namespace WebKit
