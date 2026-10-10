#pragma once

#include "WKUWPEngine.h"
#include <memory>
#include <windows.h>

namespace WebKit {

// One process-engine service binding on its owning UI thread; module pinning is external.
class OwnerThreadServicePumpUWP {
public:
    // Construct native service state on the thread that will own the engine.
    OwnerThreadServicePumpUWP() = default;
    // A service/epoch binding cannot be duplicated by copying.
    OwnerThreadServicePumpUWP(const OwnerThreadServicePumpUWP&) = delete;
    // Prevent independent copies of process suspension/client counts.
    OwnerThreadServicePumpUWP& operator=(const OwnerThreadServicePumpUWP&) = delete;
    // Bind a copied exact V1 service table before admitting any client.
    HRESULT bind(const WKUWPEngineAPI*);
    // Acquire the process-shared owner binding, rejecting foreign engines/threads.
    static HRESULT acquire(const WKUWPEngineAPI*, std::shared_ptr<OwnerThreadServicePumpUWP>&);
    // Add one live page/control independently of its presentation visibility.
    HRESULT addClient();
    // End one service lease after ordinary instance cleanup has completed.
    HRESULT removeClient();
    // Run service once for this CompositionTarget epoch, shared by all controls.
    HRESULT serviceForFrame(int64_t renderingTime100ns);
    // Run owner service for a timer/wake independently of frames or dirty state.
    HRESULT serviceNow();
    // Forward app suspension once; visual unload must never call this operation.
    HRESULT setSuspended(bool);
    // Check the captured native owner before any provider/state mutation.
    HRESULT checkThread() const;
    // Number of active service leases, including visually paused instances.
    unsigned clientCount() const { return m_clients; }
    // True only after explicit process suspension was accepted by the provider.
    bool suspended() const { return m_suspended; }
    // Actual provider service calls, excluding duplicate frames and recursive requests.
    uint64_t serviceCount() const { return m_serviceCount; }
    // Recursive service requests are deferred instead of cycling the run loop recursively.
    uint64_t deferredServiceCount() const { return m_deferredServiceCount; }
    // Compare immutable native service identity without retaining a caller table pointer.
    bool matches(const WKUWPEngineAPI&) const;
private:
    // Validate negotiation/header/slots without reading beyond a short reported table.
    static HRESULT validate(const WKUWPEngineAPI*);
    // Copied process-service slots from the externally pinned engine module.
    WKUWPEngineAPI m_api { };
    // Constructing owner thread; all non-observational operations validate it.
    DWORD m_owner { GetCurrentThreadId() };
    // Live instance leases; unload changes no lease count.
    unsigned m_clients { 0 };
    // Provider table has been validated and copied.
    bool m_bound { false };
    // Explicit app suspension state, independent of any visual unload.
    bool m_suspended { false };
    // Prevent recursive RunLoop cycles while provider service dispatches callbacks.
    bool m_servicing { false };
    // First frame epoch has been recorded, including a valid zero epoch.
    bool m_hasFrame { false };
    // Last shared CompositionTarget epoch in SDK 100-nanosecond units.
    int64_t m_lastFrame { 0 };
    // Original service result shared by every participant in the same frame epoch.
    HRESULT m_frameResult { S_OK };
    // Number of actual provider service calls.
    uint64_t m_serviceCount { 0 };
    // Number of recursive requests deferred until a later owner dispatch.
    uint64_t m_deferredServiceCount { 0 };
};

// Per-view frame eligibility/lease state; no network completion is inferred from rendering.
class FrameSchedulerUWP {
public:
    // Construct a detached view lease on its intended owner thread.
    FrameSchedulerUWP() = default;
    // A live service lease has exactly one release owner.
    FrameSchedulerUWP(const FrameSchedulerUWP&) = delete;
    // Prevent duplicate ordinary-close bookkeeping.
    FrameSchedulerUWP& operator=(const FrameSchedulerUWP&) = delete;
    // Ordinary owner-thread lease release; dispatcher-loss cleanup is a runtime-owner gate.
    ~FrameSchedulerUWP();
    // Attach one live lease to the shared process service pump, initially visually paused.
    HRESULT attach(const std::shared_ptr<OwnerThreadServicePumpUWP>&);
    // Pause/resume only presentation while keeping service and loading clocks alive.
    HRESULT setPresentationEnabled(bool);
    // Admit at most one visible unsuspended presentation callback per SDK frame epoch.
    HRESULT beginFrame(int64_t renderingTime100ns);
    // Release this instance once; a surviving peer keeps its shared service lease.
    HRESULT close();
    // Captured owner affinity for native caller/driver checks.
    HRESULT checkThread() const;
    // True only while this instance may participate in presentation callbacks.
    bool presentationEnabled() const { return m_presenting && !m_closed; }
    // Number of admitted per-view frames, separate from shared service calls.
    uint64_t frameCount() const { return m_frameCount; }

private:
    // Constructing owner thread for local lease/visibility mutation.
    DWORD m_owner { GetCurrentThreadId() };
    // Shared native service state; this is not a strong XAML/control subscription.
    std::shared_ptr<OwnerThreadServicePumpUWP> m_pump;
    // Initially detached/closed until an explicit service lease is attached.
    bool m_closed { true };
    // Visibility affects presentation alone, never service/client ownership.
    bool m_presenting { false };
    // This view has observed a presentation epoch.
    bool m_hasFrame { false };
    // Last admitted per-view SDK rendering epoch.
    int64_t m_lastFrame { 0 };
    // Successfully admitted callbacks for diagnostic ownership verification.
    uint64_t m_frameCount { 0 };
};

} // namespace WebKit
