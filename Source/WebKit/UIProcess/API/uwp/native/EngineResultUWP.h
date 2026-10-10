#pragma once

#include "WKUWPEngine.h"
#include <windows.h>

namespace WebKit {

// Preserve original HRESULTs and map positive frozen results by their defined meaning.
inline HRESULT engineResultToHRESULT(WKUWPResult result)
{
    if (result < 0)
        return static_cast<HRESULT>(result);
    switch (result) {
    case WKUWPResultOK:
        return S_OK;
    case WKUWPResultInvalidArgument:
    case WKUWPResultInvalidCertificateToken:
        return E_INVALIDARG;
    case WKUWPResultWrongThread:
        return RPC_E_WRONG_THREAD;
    case WKUWPResultClosed:
    case WKUWPResultShuttingDown:
        return RO_E_CLOSED;
    case WKUWPResultUnsupported:
    case WKUWPResultMissingCapability:
        return E_NOTIMPL;
    case WKUWPResultNotInitialized:
        return HRESULT_FROM_WIN32(ERROR_NOT_READY);
    case WKUWPResultConfigurationConflict:
        return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    case WKUWPResultBudgetExceeded:
        return E_OUTOFMEMORY;
    case WKUWPResultPending:
        return E_PENDING;
    case WKUWPResultAbiMismatch:
        return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    default:
        return E_FAIL;
    }
}

} // namespace WebKit
