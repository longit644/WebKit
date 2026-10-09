#include "PageControllerUWP.h"

namespace WebKit {

// Convert frozen operation results by meaning, never by positive-code HRESULT equality.
HRESULT PageControllerUWP::resultToHRESULT(WKUWPResult result)
{
    if (result < 0) return static_cast<HRESULT>(result);
    switch (result) {
    case WKUWPResultOk: return S_OK;
    case WKUWPResultInvalidArgument:
    case WKUWPResultInvalidCertificateToken: return E_INVALIDARG;
    case WKUWPResultWrongThread: return RPC_E_WRONG_THREAD;
    case WKUWPResultClosed:
    case WKUWPResultShuttingDown: return RO_E_CLOSED;
    case WKUWPResultUnsupported:
    case WKUWPResultMissingCapability: return E_NOTIMPL;
    case WKUWPResultNotInitialized: return HRESULT_FROM_WIN32(ERROR_NOT_READY);
    case WKUWPResultConfigurationConflict: return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    case WKUWPResultBudgetExceeded: return E_OUTOFMEMORY;
    case WKUWPResultPending: return E_PENDING;
    case WKUWPResultAbiMismatch: return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    default: return E_FAIL;
    }
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

// Keep Pending ownership until provider close acknowledgement, without hot-unloading code.
HRESULT PageControllerUWP::close()
{
    if (GetCurrentThreadId() != m_owner) return RPC_E_WRONG_THREAD;
    if (m_closeRequested) return m_page ? S_FALSE : S_OK;
    auto ready = checkBound();
    if (FAILED(ready)) return ready;
    m_closeRequested = true;
    auto result = m_api.closePage(m_page);
    if (result == WKUWPResultOk || result == WKUWPResultClosed) {
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
