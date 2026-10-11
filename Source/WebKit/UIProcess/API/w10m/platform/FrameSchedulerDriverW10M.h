#pragma once

#include "../rendering/FrameSchedulerW10M.h"
#include <agile.h>
#include <atomic>
#include <functional>
#include <map>

namespace WebKit {

// Private CX owner dispatcher/timer/rendering bridge over native shared scheduling state.
ref class FrameSchedulerDriverW10M sealed {
internal:
    // Share the owner driver for one externally pinned process-engine table.
    static FrameSchedulerDriverW10M^ Acquire(const WKW10MEngineAPI*, Windows::UI::Core::CoreDispatcher^);
    // Attach a native lease and weak-target frame callback; timer starts for the first client.
    uint64_t AddClient(const std::shared_ptr<FrameSchedulerW10M>&, std::function<HRESULT(int64_t)>&&);
    // Close one lease, revoking owner hooks when the last driver client is removed.
    void RemoveClient(uint64_t);
    // Enqueue one coalesced owner wake from any thread without capturing a control.
    HRESULT RequestWake();
    // Pause/resume explicit app service clocks independently of visual presentation.
    HRESULT SetSuspended(bool);
    // Borrow shared native counters for controlled ownership tests/runtime bookkeeping.
    std::shared_ptr<OwnerThreadServicePumpW10M> ServicePump() { return m_pump; }
    // Last contained owner service/frame callback failure.
    HRESULT LastResult() { return m_lastResult; }
    // Number of timer-driven owner service calls, not rendering callbacks.
    uint64_t TimerDispatchCount() { return m_timerDispatches; }
    // Number of coalesced worker wake handlers actually delivered on the owner.
    uint64_t WakeDispatchCount() { return m_wakeDispatches; }
    // Dispatch a real or controlled SDK frame epoch through the same eligibility path.
    void DispatchFrame(int64_t renderingTime100ns);

private:
    // Create the weak owner hooks; no service starts before a client lease exists.
    FrameSchedulerDriverW10M(Windows::UI::Core::CoreDispatcher^, const std::shared_ptr<OwnerThreadServicePumpW10M>&);
    // Check dispatcher affinity before lease/hook/observable mutation.
    void CheckThread();
    // Run one timer/wake owner service call with boundary exception containment.
    void DispatchService(bool timer);
    // Revoke the timer and global Rendering token after all driver leases end.
    void StopHooks();

    // Copied native state/frame port held through an in-progress dispatch snapshot.
    struct Client {
        // Per-view native visibility/lease/epoch state.
        std::shared_ptr<FrameSchedulerW10M> scheduler;
        // Consumer supplies weak/lifetime-safe target closure, never an engine-global view.
        std::function<HRESULT(int64_t)> frame;
    };

    // Owner dispatcher; worker wakes only enqueue to it.
    Windows::UI::Core::CoreDispatcher^ m_dispatcher;
    // Shared native service table/epoch state, containing no XAML target.
    std::shared_ptr<OwnerThreadServicePumpW10M> m_pump;
    // One owner timer that continues service while all presentation is paused.
    Windows::UI::Xaml::DispatcherTimer^ m_timer;
    // Owner timer event registration, revoked at ordinary last-client close.
    Windows::Foundation::EventRegistrationToken m_tickToken;
    // Shared CompositionTarget registration, not one hook per control.
    Windows::Foundation::EventRegistrationToken m_renderToken;
    // Live weak-target client ports, individually revocable by monotonic token.
    std::map<uint64_t, std::shared_ptr<Client>> m_clients;
    // Never-reused client identity for ordinary remove/recreate safety.
    uint64_t m_nextClient { 0 };
    // Weak worker wake enqueue is coalesced independently of callback state.
    std::atomic<bool> m_wakePending { false };
    // Guard nested SDK Rendering dispatch while callbacks reenter the owner.
    bool m_rendering { false };
    // Timer/render hooks have been revoked and cannot admit new clients.
    bool m_stopped { false };
    // Last contained service/consumer failure, retaining its originating HRESULT.
    HRESULT m_lastResult { S_OK };
    // Number of actual timer handler invocations.
    uint64_t m_timerDispatches { 0 };
    // Delivered worker wake count, distinct from timer cadence.
    uint64_t m_wakeDispatches { 0 };
};

} // namespace WebKit

