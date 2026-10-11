#pragma once

#include "WKW10MEngine.h"
#include <windows.h>

namespace WebKit {

// Preserve original HRESULTs and map positive frozen results by their defined meaning.
inline HRESULT engineResultToHRESULT(WKW10MResult result)
{
    if (result < 0)
        return static_cast<HRESULT>(result);
    switch (result) {
    case WKW10MResultOK:
        return S_OK;
    case WKW10MResultInvalidArgument:
    case WKW10MResultInvalidCertificateToken:
        return E_INVALIDARG;
    case WKW10MResultWrongThread:
        return RPC_E_WRONG_THREAD;
    case WKW10MResultClosed:
    case WKW10MResultShuttingDown:
        return RO_E_CLOSED;
    case WKW10MResultUnsupported:
    case WKW10MResultMissingCapability:
        return E_NOTIMPL;
    case WKW10MResultNotInitialized:
        return HRESULT_FROM_WIN32(ERROR_NOT_READY);
    case WKW10MResultConfigurationConflict:
        return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    case WKW10MResultBudgetExceeded:
        return E_OUTOFMEMORY;
    case WKW10MResultPending:
        return E_PENDING;
    case WKW10MResultAbiMismatch:
        return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    default:
        return E_FAIL;
    }
}

} // namespace WebKit

