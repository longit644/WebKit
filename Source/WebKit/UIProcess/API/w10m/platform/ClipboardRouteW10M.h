#pragma once

#include "ClipboardServiceW10M.h"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <windows.h>

namespace WebKit {

// Route completion delivered on the owner thread for one route-issued operation.
// kind carries the frozen clipboard kind; cut is true only for cut() writes.
// text is valid only during the call; the caller copies what it retains. The
// caller retains context until this returns. The token is erased before
// delivery, so the completion may close the route or issue new operations, but
// must not destroy the route: a close-driven completion runs inside close()'s
// cancellation loop, which touches route state after each delivery returns.
typedef void(__stdcall* ClipboardRouteCompletion)(
    void* context, uint64_t operationId, uint32_t kind, bool cut,
    WKW10MResult result, const WKW10MString* text);

// Owner-side clipboard routing over a caller-owned neutral service.
// Each copy/cut/paste issues one service operation with a retained token as its
// completion context. The token lives from acceptance until the single
// completion returns, on the owner thread only, so a transport completion that
// arrives after Close still finds valid state: Close cancels every pending
// route operation, each completes Closed exactly once, and late transport
// completions are ignored by the service registry. Engine-driven operations
// through the same borrowed table keep their own contexts and are unaffected
// by route Close. The service must be constructed on this route's owner thread.
// Destruction closes first; with a real dispatcher, drain pendingCount() to
// zero before destruction, because a queued dispatcher task cannot be recalled
// here (see 7.5). This domain compiles without exception unwinding on ARM.
class ClipboardRouteW10M {
public:
    // Own the service and bind route operations to one page.
    ClipboardRouteW10M(WKW10MPageHandle page, std::unique_ptr<ClipboardServiceW10M>&& service);
    // Route state has exactly one owner.
    ClipboardRouteW10M(const ClipboardRouteW10M&) = delete;
    // Prevent duplicated token registries.
    ClipboardRouteW10M& operator=(const ClipboardRouteW10M&) = delete;
    // Close first, then abandon; late transport completions find no record.
    ~ClipboardRouteW10M();

    // Publish copied bytes; selection handling belongs to the engine.
    HRESULT copy(const WKW10MString* text, ClipboardRouteCompletion completion, void* context);
    // Publish cut bytes; the engine deletes the selection, the host only publishes.
    HRESULT cut(const WKW10MString* text, ClipboardRouteCompletion completion, void* context);
    // Read system clipboard text into the single completion.
    HRESULT paste(ClipboardRouteCompletion completion, void* context);
    // Cancel every pending route operation with exactly-once Closed completion.
    // Idempotent; later copy/cut/paste fail Closed without completing.
    HRESULT close();
    // Fill a borrowed platform-services table from the owned service.
    HRESULT buildTable(WKW10MPlatformServices* table);
    // Check owner affinity before any registry or provider access.
    HRESULT checkThread() const;
    // Number of route-issued operations still awaiting completion.
    unsigned pendingCount() const;
    // Whether close() has latched; set before any cancellation runs.
    bool closed() const;

private:
    // One accepted route operation; erased before its single completion runs.
    struct Token {
        // Owning route for registry lookup; valid until completion or close.
        ClipboardRouteW10M* route { nullptr };
        // Caller completion invoked exactly once with kind/cut/result/span.
        ClipboardRouteCompletion completion { nullptr };
        // Caller context retained by the caller until that completion returns.
        void* callerContext { nullptr };
        // Frozen clipboard kind for this operation.
        uint32_t kind { 0 };
        // True for cut() writes; copy/paste report false.
        bool cut { false };
    };

    // Issue one validated write with a retained token.
    HRESULT write(bool cut, const WKW10MString* text, ClipboardRouteCompletion completion, void* context);
    // Erase and return one token; null when already completed or canceled.
    std::shared_ptr<Token> takeToken(uint64_t operationId, Token* token);
    // Service completion routing; delivers through the retained token.
    static void __stdcall completionThunk(void* context, uint64_t operationId,
        WKW10MResult result, const WKW10MString* text);

    // Guards the token registry against worker-thread transport completions.
    mutable std::mutex m_lock;
    // Owner thread for all registry mutation and completion delivery.
    DWORD m_owner { GetCurrentThreadId() };
    // Owned neutral service carrying the operation registry.
    std::unique_ptr<ClipboardServiceW10M> m_service;
    // Page bound to every route-issued operation.
    WKW10MPageHandle m_page { 0 };
    // Retained tokens awaiting their single completion, by operation ID.
    std::map<uint64_t, std::shared_ptr<Token>> m_tokens;
    // Next operation ID; monotonic so IDs are never reused by this route.
    uint64_t m_nextId { 1 };
    // Close latch; set before cancellation so reentry stays idempotent.
    bool m_closed { false };
};
} // namespace WebKit

