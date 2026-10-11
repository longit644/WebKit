#pragma once

#include <agile.h>
#include <functional>
#include <memory>
#include <mutex>
#include <windows.h>

namespace WebKit {

// One caller's CX observation of shared initialization. It owns its handler,
// cancellation and IAsyncInfo.Close state; it never owns/cancels the attempt.
ref class InitializationActionW10M sealed : Windows::Foundation::IAsyncAction {
public:
    // Register one independent completion handler; late registration still fires once.
    virtual property Windows::Foundation::AsyncActionCompletedHandler^ Completed {
        // Retrieve the registered delegate in the caller's current COM context.
        Windows::Foundation::AsyncActionCompletedHandler^ get();
        // Assign this observation's single handler without altering another caller.
        void set(Windows::Foundation::AsyncActionCompletedHandler^ value);
    }

    // Return success, original failure, E_ABORT cancellation or pending/closed errors.
    virtual void GetResults();
    // Read this observation's nonzero process-unique async identifier.
    virtual property unsigned int Id { unsigned int get(); }
    // Read this observation's terminal/pending state independently of the attempt.
    virtual property Windows::Foundation::AsyncStatus Status { Windows::Foundation::AsyncStatus get(); }
    // Read this observation's originating HRESULT; cancellation is E_ABORT.
    virtual property Windows::Foundation::HResult ErrorCode { Windows::Foundation::HResult get(); }
    // Cancel only this observation; shared initialization and peer actions continue.
    virtual void Cancel();
    // Release only this observation/handler; repeated Close is harmless.
    virtual void Close();

internal:
    // Construct an owner-dispatched observation with a weak-owner detach callback.
    InitializationActionW10M(Windows::UI::Core::CoreDispatcher^, std::function<void(unsigned int)> detached);
    // Mark terminal before any completion handler runs; already-ended callers stay ended.
    bool Resolve(HRESULT result, bool canceled);
    // Deliver a registered terminal handler once on the owner's UI dispatcher.
    void Publish();
    // Read identity for internal detach bookkeeping even after IAsyncInfo.Close.
    unsigned int ObservationId() { return m_id; }

private:
    // Reject reads/mutations of an explicitly closed observation while holding its lock.
    void CheckOpenLocked();
    // Detach this observation without letting callback/dispatcher failures escape.
    void Detach();
    // Invoke a terminal handler outside the lock with a retained action lifetime.
    void Deliver();

    // Protect per-observation status/handler state against worker Cancel/Close/register.
    std::mutex m_lock;
    // Nonzero immutable async identity, unrelated to page/navigation IDs.
    unsigned int m_id;
    // Private caller status, initially Started.
    Windows::Foundation::AsyncStatus m_status { Windows::Foundation::AsyncStatus::Started };
    // Original result or E_ABORT, retained until this observation closes.
    HRESULT m_error { S_OK };
    // Handler marshaling wrapper, not a process/global completion delegate.
    std::shared_ptr<Platform::Agile<Windows::Foundation::AsyncActionCompletedHandler^>> m_completed;
    // Owner dispatcher used for callbacks without rooting a control.
    Windows::UI::Core::CoreDispatcher^ m_dispatcher;
    // Weak-owner callback removes ended observations from the producer's joiner list.
    std::function<void(unsigned int)> m_detached;
    // Enforce the WinRT single-completion-handler assignment rule.
    bool m_handlerAssigned { false };
    // Prevent duplicate dispatcher wake requests for the same terminal.
    bool m_deliveryQueued { false };
    // Mark callback invocation before allowing reentrant Cancel/Close.
    bool m_delivered { false };
    // Independent IAsyncInfo.Close marker; it never closes the shared attempt.
    bool m_closed { false };
};

} // namespace WebKit

