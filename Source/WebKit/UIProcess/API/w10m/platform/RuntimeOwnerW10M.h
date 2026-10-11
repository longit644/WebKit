#pragma once

#include "ClipboardRouteW10M.h"
#include "../native/EngineModuleW10M.h"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <windows.h>

namespace WebKit {

// Host shutdown hook invoked once when revoked; Closed on shutdown, and the
// hook must not destroy the owner for the same reason route completions must
// not destroy the route.
typedef void(__stdcall* RuntimeHookCallback)(void* context, uint64_t hookId, WKW10MResult result);

// Process runtime owner: engine pinning, clipboard routing, cleanup tickets,
// failed-enqueue accounting, quiescence and host shutdown hooks.
//
// Each accepted clipboard operation opens a ticket; its single completion
// (normal or Closed-on-cancel) acknowledges and closes the ticket, so shutdown
// can prove every accepted completion was acknowledged. Hooks open tickets
// that close on revocation. Quiescence means no open tickets, no pending route
// operations and no registered hooks. The dispatcher-loss flag models host
// teardown: new wake enqueues fail DispatcherUnavailable while synchronous
// cancellation still works, so the last release with a lost dispatcher drops
// nothing silently and crashes nothing.
//
// The owner builds the engine-facing platform table itself with its own
// context: request/cancel forward to the current service, retain/release count
// table references the engine must drop after shutdown quiescence, and postWake
// enqueues one wake through the dispatcher port (null until this owner exists,
// per the 7.1 notes). Engine-driven requests after the shutdown latch fail
// ShuttingDown; cancels still pass through because cancels are cleanup.
//
// The engine module binds once and pins for process lifetime; destroying this
// owner never unloads it. Destruction runs shutdown first, so pending work is
// acknowledged while fully alive. Completions and hook callbacks must not
// destroy the owner. This domain compiles without exception unwinding on ARM.
class RuntimeOwnerW10M {
public:
    // Own the pipeline for one page; ports move into the owned service.
    RuntimeOwnerW10M(WKW10MPageHandle page, std::unique_ptr<IServiceDispatcherW10M>&& dispatcher,
        std::unique_ptr<IClipboardTransportW10M>&& transport);
    // Runtime state has exactly one owner.
    RuntimeOwnerW10M(const RuntimeOwnerW10M&) = delete;
    // Prevent duplicated ticket registries.
    RuntimeOwnerW10M& operator=(const RuntimeOwnerW10M&) = delete;
    // Shut down first (every accepted completion acknowledged), then abandon.
    ~RuntimeOwnerW10M();

    // Publish copied bytes; each acceptance opens one ticket.
    HRESULT copy(const WKW10MString* text, ClipboardRouteCompletion completion, void* context);
    // Publish cut bytes with the cut marker; selection deletion is engine work.
    HRESULT cut(const WKW10MString* text, ClipboardRouteCompletion completion, void* context);
    // Read system clipboard text; completion carries the ticket's operation ID.
    HRESULT paste(ClipboardRouteCompletion completion, void* context);
    // Cancel every pending route operation; each ticket is acknowledged Closed.
    HRESULT closeClipboard();
    // Close, then replace the service and route with fresh ports; totals carry,
    // pending resets, generation advances. ShuttingDown once shutdown has begun.
    HRESULT recreateClipboard(std::unique_ptr<IServiceDispatcherW10M>&& dispatcher,
        std::unique_ptr<IClipboardTransportW10M>&& transport);
    // Fill the engine-facing table with owner context and a working postWake.
    HRESULT buildTable(WKW10MPlatformServices* table);
    // Enqueue one owner wake; DispatcherUnavailable when lost or refused.
    WKW10MResult postWake();
    // Register one shutdown hook; revocation acknowledges it Closed exactly once.
    HRESULT registerHook(RuntimeHookCallback callback, void* context, uint64_t* hookId);
    // Revoke one hook now with a single Closed acknowledgment.
    HRESULT revokeHook(uint64_t hookId);
    // Latch shutdown: acknowledge every ticket Closed, revoke every hook, lose
    // the dispatcher, then forward engine shutdown when bound. Host steps run
    // before the forward, so acknowledgment never depends on the engine.
    HRESULT beginShutdown();
    // Mark the dispatcher lost without shutting down: wakes fail from here on
    // while synchronous cancellation still works. Idempotent; test and teardown
    // use only, full shutdown owns the real teardown order.
    HRESULT loseDispatcher();
    // Forward engine shutdown progress; S_OK when unbound.
    WKW10MResult pollShutdown();
    // No open tickets, no pending route operations, no registered hooks.
    bool quiescent() const;
    // Bind and pin the engine module; ShuttingDown once shutdown has begun.
    WKW10MResult bindEngine(const wchar_t* modulePath, uint32_t minimumMinor, uint64_t requiredCapabilities);
    // True after a successful bind; the pin survives this owner.
    bool isEngineBound() const;
    // Check owner affinity before any registry or provider access.
    HRESULT checkThread() const;
    // Current clipboard generation; recreateClipboard advances it.
    uint64_t generation() const;
    // Latched once beginShutdown runs.
    bool isShuttingDown() const;
    // Latched once the dispatcher is lost.
    bool dispatcherLost() const;
    // Route-issued operations still awaiting completion.
    unsigned pendingCount() const;
    // Tickets accepted but not yet acknowledged.
    unsigned openTickets() const;
    // Hooks registered but not yet revoked.
    unsigned hookCount() const;
    // Borrowed-table retains the engine still owes.
    unsigned tableRefCount() const;
    // Accepted operations acknowledged normally (non-Closed results).
    unsigned completedTotal() const;
    // Accepted operations acknowledged Closed through cancel or shutdown.
    unsigned canceledTotal() const;
    // Transport completions dropped for lack of a dispatcher enqueue, carried
    // across clipboard recreations.
    unsigned droppedTotal() const;
    // Hooks acknowledged through revoke or shutdown.
    unsigned hooksRevokedTotal() const;
    // Last forwarded engine shutdown result; OK when never forwarded.
    WKW10MResult engineShutdownResult() const;

private:
    // One accepted clipboard operation; erased when its completion is acked.
    struct Ticket {
        // Owning runtime for ticket lookup; valid until acknowledgment or shutdown.
        RuntimeOwnerW10M* owner { nullptr };
        // Caller completion invoked exactly once through this ticket.
        ClipboardRouteCompletion completion { nullptr };
        // Caller context retained by the caller until that completion returns.
        void* callerContext { nullptr };
        // Clipboard generation that accepted this operation.
        uint64_t generation { 0 };
    };

    // One registered hook; erased when revoked.
    struct Hook {
        // Revocation callback invoked exactly once with Closed.
        RuntimeHookCallback callback { nullptr };
        // Caller context retained by the caller until revocation returns.
        void* callerContext { nullptr };
    };

    // Build one service plus route pair and refresh the non-owning views.
    void createClipboard(std::unique_ptr<IServiceDispatcherW10M> dispatcher,
        std::unique_ptr<IClipboardTransportW10M> transport);
    // Issue one clipboard operation with a retained ticket.
    HRESULT issue(bool isRead, bool cut, const WKW10MString* text,
        ClipboardRouteCompletion completion, void* context);
    // Erase and return one ticket; null when already acknowledged.
    std::shared_ptr<Ticket> takeTicket(Ticket* ticket);
    // Route completion routing; acknowledges the ticket, then delivers.
    static void __stdcall clipboardThunk(void* context, uint64_t operationId, uint32_t kind,
        bool cut, WKW10MResult result, const WKW10MString* text);
    // Retain the borrowed engine table context; void per contract.
    void retainTable();
    // Release a previous table retain; never self-deletes.
    void releaseTable();
    // Table routing with owner context.
    static void __stdcall retainTableThunk(void* context);
    // Table release routing; a null context is ignored.
    static void __stdcall releaseTableThunk(void* context);
    // Engine request routing; ShuttingDown once the latch is set.
    static WKW10MResult __stdcall requestTableThunk(void* context, WKW10MPageHandle page,
        uint64_t operationId, uint32_t kind, const WKW10MString* text,
        WKW10MClipboardCompletion completion, void* completionContext);
    // Engine cancel routing; cancels are cleanup and always pass through.
    static WKW10MResult __stdcall cancelTableThunk(void* context, uint64_t operationId);
    // Wake routing for the borrowed table.
    static WKW10MResult __stdcall postWakeTableThunk(void* context);

    // Guards tickets, hooks and counters against worker-thread completions.
    mutable std::mutex m_lock;
    // Owner thread for all registry mutation and completion delivery.
    DWORD m_owner { GetCurrentThreadId() };
    // Bound-or-unbound engine module; pins the image for process lifetime.
    EngineModuleW10M m_module;
    // Owned clipboard route; recreated as a whole on recreateClipboard.
    std::unique_ptr<ClipboardRouteW10M> m_route;
    // Non-owning wake port into the current service; refreshed on recreate.
    IServiceDispatcherW10M* m_wakePort { nullptr };
    // Non-owning service view for engine table forwarding; refreshed too.
    ClipboardServiceW10M* m_serviceView { nullptr };
    // Page bound to every clipboard stack this owner builds.
    WKW10MPageHandle m_page { 0 };
    // Accepted operations awaiting acknowledgment, by ticket pointer.
    std::map<Ticket*, std::shared_ptr<Ticket>> m_tickets;
    // Registered hooks awaiting revocation, by hook ID.
    std::map<uint64_t, std::shared_ptr<Hook>> m_hooks;
    // Next hook ID; monotonic so hook IDs are never reused.
    uint64_t m_nextHookId { 1 };
    // Current clipboard generation; starts at zero, recreate advances it.
    uint64_t m_generation { 0 };
    // Shutdown latch; set before any cancellation or revocation runs.
    bool m_shuttingDown { false };
    // Dispatcher-loss latch; wakes fail once set, cancels still work.
    bool m_dispatcherLost { false };
    // Borrowed-table retains the engine still owes.
    unsigned m_tableRefs { 0 };
    // Accepted operations acknowledged normally.
    unsigned m_completedTotal { 0 };
    // Accepted operations acknowledged Closed.
    unsigned m_canceledTotal { 0 };
    // Dropped enqueues carried across recreations.
    unsigned m_droppedCarried { 0 };
    // Hooks acknowledged through revoke or shutdown.
    unsigned m_hooksRevokedTotal { 0 };
    // Last forwarded engine shutdown result.
    WKW10MResult m_engineShutdownResult { WKW10MResultOK };
};
} // namespace WebKit

