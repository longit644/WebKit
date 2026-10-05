// Fundamental async declarations in the exact C++ spellings the generated
// header references. The SDK C++ projection headers for these are not
// consumable standalone under this toolchain; layouts and IIDs below are
// the OS-stable WinRT contracts. Status travels as INT32 with AsyncStatus
// numerics (Started 0, Completed 1, Canceled 2, Error 3). Never include
// windows.foundation.h alongside this header (duplicate definitions).
#pragma once
#include <inspectable.h>

namespace Windows {
namespace Foundation {

// OS AsyncStatus values carried as INT32 by the declarations below.
enum class AsyncStatusValue : INT32 {
    // The operation has not reached a terminal state.
    Started = 0,

    // The operation finished successfully and results may be retrieved.
    Completed = 1,

    // Cancellation ended the operation before successful completion.
    Canceled = 2,

    // The operation failed; IAsyncInfo exposes the failure HRESULT.
    Error = 3
};

// OS async-operation identity, status and lifecycle contract; queried separately by hosts.
MIDL_INTERFACE("00000036-0000-0000-C000-000000000046")
IAsyncInfo : public IInspectable
{
public:
    // Operation identifier; ours always reports 1.
    virtual HRESULT STDMETHODCALLTYPE get_Id(UINT32 * id) = 0;

    // Current AsyncStatus numeric (Started 0, Completed 1, Canceled 2, Error 3).
    virtual HRESULT STDMETHODCALLTYPE get_Status(INT32 * status) = 0;

    // Terminal failure, if any; S_OK when completed normally.
    virtual HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT * code) = 0;

    // Requests cancellation; no-op on an already-completed action.
    virtual HRESULT STDMETHODCALLTYPE Cancel() = 0;

    // Releases operation resources; safe to call repeatedly.
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};

// Forward declaration used by the completion delegate's Invoke signature.
struct IAsyncAction;

// OS delegate invoked when an action reaches its terminal state.
MIDL_INTERFACE("a4ed5c81-76c9-40bd-8be6-b1d90fb0ae9d")
IAsyncActionCompletedHandler : public IUnknown
{
public:
    // Fires once when the action reaches a terminal state.
    virtual HRESULT STDMETHODCALLTYPE Invoke(IAsyncAction * asyncInfo, INT32 status) = 0;
};

// Async action completion and result contract used by EnsureInitializedAsync.
MIDL_INTERFACE("5a648006-843a-4da9-865b-9d26e5dfad7b")
IAsyncAction : public IAsyncInfo
{
public:
    // Completion handler runs immediately: our actions are already complete.
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IAsyncActionCompletedHandler * handler) = 0;

    // Returns the registered completion delegate, or null when none is retained.
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IAsyncActionCompletedHandler * *handler) = 0;

    // Retrieves results; S_OK here carries no payload.
    virtual HRESULT STDMETHODCALLTYPE GetResults() = 0;
};

} // namespace Foundation
} // namespace Windows
