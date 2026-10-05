/*
 * PlatformServiceUWP.h - consumer-side platform service context for the frozen
 * V1 WKUWP contract.
 *
 * This is M10 task 3.4 of
 * openspec/changes/wkview-m10-uwp-cppcx-restructure/tasks.md. It implements the
 * host half of artifact/native-abi.md ("Platform service and completion
 * schema"): the service table the engine retains once on initialize, the
 * exactly-once completion rule for accepted clipboard operations including
 * cancellation, and the borrowed-read-span rule.
 *
 * Ownership rules implemented here, in contract order:
 *
 *   - The service context is retained once on successful initialize and
 *     released only after shutdown quiescence. This object counts both sides
 *     with the same intrusive protocol as the page contexts, and deletes
 *     itself at zero references.
 *   - An accepted requestClipboard completes exactly once, including when
 *     cancelled. The operation is marked finished before its completion runs,
 *     so a reentrant cancel or delivery for the same identifier finds nothing
 *     and reports InvalidArgument instead of completing twice. A rejected
 *     request never completes.
 *   - Write text is copied on acceptance, so the caller's buffer may die
 *     immediately. Read spans stay borrowed during the completion call only;
 *     the engine copies them, never this object.
 *   - Cancellation is reported with the platform HRESULT for ERROR_CANCELLED,
 *     which travels as a negative result under the contract's HRESULT rule.
 *   - A null completion callback cannot satisfy exactly-once delivery, so a
 *     request without one is rejected rather than accepted blindly.
 *
 * Like its neighbours this file is dependency-free plain C++ with no WebCore
 * configuration, WTF, STL or C++/CX type, so both boundary compilers verify it
 * standalone. It is owner-thread affine and takes no locks.
 */

#ifndef PLATFORM_SERVICE_UWP_H
#define PLATFORM_SERVICE_UWP_H

#include "WKUWPEngine.h"

#include <windows.h>

namespace WebKit {

// Consumer-side platform service with exactly-once clipboard completion.
//
// The engine retains this object once through retainThunk on initialize and
// releases it after shutdown quiescence; the creator's setup reference is the
// initial count of one. When the count reaches zero the object deletes itself,
// so no owner may touch it after its final release.
class PlatformServiceContextUWP {
public:
    // Maximum simultaneously pending clipboard operations.
    //
    // The table is fixed, never a heap container, because no cross-module
    // allocation may cross this boundary; the seventeenth concurrent request
    // fails with BudgetExceeded instead of growing silently.
    static constexpr uint32_t kMaxPendingOperations = 16;

    // Builds a service with a setup reference count of one and no operations.
    PlatformServiceContextUWP();

    // Frees copied Write text, then reports destruction through the hook.
    ~PlatformServiceContextUWP();

    // Non-copyable service identity; share the object instead of copying it.
    PlatformServiceContextUWP(const PlatformServiceContextUWP&) = delete;

    // Non-copyable service identity; share the object instead of copying it.
    PlatformServiceContextUWP& operator=(const PlatformServiceContextUWP&) = delete;

    // Intrusive retain entry point for the engine's ownership protocol.
    void retain();

    // Intrusive release entry point; deletes the object at zero references.
    void release();

    // Current reference count, for tests that prove retain/release pairing.
    uint32_t refCount() const;

    // Engine retain-thunk calls observed, proving the provider retained once.
    uint32_t retainCalls() const;

    // Engine release-thunk calls observed, proving release pairing.
    uint32_t releaseCalls() const;

    // Destruction hook invoked from the destructor with the registered context.
    //
    // Lets a test prove the service survived until quiescence instead of being
    // freed while operations were still pending. Null by default. Runs before
    // member storage is released and never calls out.
    typedef void(__stdcall* DestroyedHookFunction)(void* hookContext);

    // Arms the destruction hook invoked from the destructor.
    void setDestroyedHook(DestroyedHookFunction hook, void* hookContext);

    // Fills a service table that borrows nothing: every pointer is this
    // object's thunk or this object itself, and the header declares the V1
    // structure the provider validates before reading.
    WKUWPResult buildTable(WKUWPPlatformServices* services);

    // Operations currently accepted and not yet completed or cancelled.
    uint32_t pendingCount() const;

    // postWake enqueue requests observed.
    uint32_t wakeCalls() const;

    // Delivers an asynchronous finish for a pending operation, exactly once.
    //
    // This entry models the host's own I/O finishing: it finds the pending
    // operation, marks it finished before invoking anything, frees its copied
    // text, and then runs the completion with the borrowed result span.
    // Unknown or already-finished identifiers report InvalidArgument without
    // invoking anything, so a stray second delivery can never complete twice.
    WKUWPResult deliverCompletion(uint64_t operationId, WKUWPResult result, const WKUWPString* text);

    // Copies a pending operation's accepted Write text out for verification.
    //
    // Test support proving the Write copy survives the caller's buffer: false
    // for unknown or finished operations, or when capacity is short, in which
    // case needed still reports the full length, mirroring the snapshot rule.
    bool copyOutText(uint64_t operationId, uint8_t* out, uint32_t capacity, uint32_t* needed);

    // Retain entry point exposed to the engine through the built table.
    static void __stdcall retainThunk(void* context);

    // Release entry point exposed to the engine through the built table.
    static void __stdcall releaseThunk(void* context);

    // Clipboard request entry point exposed through the built table.
    static WKUWPResult __stdcall requestClipboardThunk(void* context, WKUWPPageHandle page, uint64_t operationId,
        uint32_t kind, const WKUWPString* text, WKUWPClipboardCompletion completion, void* completionContext);

    // Clipboard cancel entry point exposed through the built table.
    static WKUWPResult __stdcall cancelClipboardThunk(void* context, uint64_t operationId);

    // Owner-thread wake entry point exposed through the built table.
    static WKUWPResult __stdcall postWakeThunk(void* context);

private:
    // One accepted clipboard operation awaiting completion or cancellation.
    struct Operation {
        // Whether this slot currently owns an unfinished operation.
        bool active;

        // Engine-assigned identifier; never reused while active.
        uint64_t id;

        // WKUWPClipboardKind this operation was accepted for.
        uint32_t kind;

        // Completion to invoke exactly once, with its retained context.
        WKUWPClipboardCompletion completion;
        void* completionContext;

        // Accepted Write text copy; null for reads, freed at finish.
        uint8_t* textCopy;

        // Accepted Write text length in bytes.
        uint32_t textLength;
    };

    // Current intrusive reference count; one at construction for the setup ref.
    uint32_t m_refCount;

    // Thunk invocation counters proving each ownership transition happened.
    uint32_t m_retainCalls;
    uint32_t m_releaseCalls;

    // postWake enqueue requests observed.
    uint32_t m_wakeCalls;

    // Destruction hook state, proving survival until quiescence.
    DestroyedHookFunction m_destroyedHook;
    void* m_destroyedHookContext;

    // Pending operation slots; inactive slots hold no references and no text.
    Operation m_operations[kMaxPendingOperations];

    // Index of the operation owning id, or kMaxPendingOperations when absent.
    uint32_t findOperation(uint64_t operationId) const;

    // Marks a slot finished and frees its copied text, before any completion.
    void finishOperation(uint32_t index);
};

} // namespace WebKit

#endif // PLATFORM_SERVICE_UWP_H
