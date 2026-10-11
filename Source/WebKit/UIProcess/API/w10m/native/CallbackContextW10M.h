/*
 * CallbackContextW10M.h - consumer-side per-page callback context and state
 * snapshot for the frozen V1 WKW10M contract.
 *
 * This is M10 task 3.4 of
 * openspec/changes/wkview-m10-uwp-cppcx-restructure/tasks.md. It implements the
 * consumer half of artifact/native-abi.md ("Callback schema and context
 * ownership" and the getState rule): a retained context object whose table the
 * engine copies and retains, borrowed-payload copying so spans never outlive
 * the call, and a caller-owned state snapshot with bounded retry.
 *
 * Ownership rules implemented here, in contract order:
 *
 *   - The table built by PageCallbackContextW10M carries context, retain/release
 *     and policy/notification entry points. The engine retains once before the
 *     table becomes active; the creator keeps its own setup reference
 *     separately and releases it when the engine no longer needs the context.
 *   - Event spans are borrowed for the call only. Both entry points copy URI
 *     and title into context-owned storage before returning, so a consumer that
 *     defers delivery never reads freed provider memory.
 *   - Detach deactivates immediately. When the engine answers Pending, the
 *     release that acknowledges detach arrives after the active dispatch
 *     unwinds; this object's storage is freed only by that release, never by
 *     the detach call itself.
 *   - No entry point throws across the boundary. Null and malformed inputs are
 *     reported with frozen result codes, and allocation uses nothrow forms
 *     whose failure is reported instead of propagated.
 *
 * Like the module consumer, this file is dependency-free plain C++ with no
 * WebCore configuration, WTF, STL or C++/CX type, so both boundary compilers
 * verify it standalone. It is owner-thread affine and takes no locks.
 */

#ifndef CALLBACK_CONTEXT_W10M_H
#define CALLBACK_CONTEXT_W10M_H

#include "WKW10MEngine.h"
#include "WKW10MNavigation.h"
#include "WKW10MPage.h"

namespace WebKit {

// Consumer-side retained page callback context.
//
// The engine retains this object once through retainThunk when its table is
// installed, and releases it exactly once as the detach acknowledgment. The
// creator's own setup reference is the initial count of one, released
// separately. When the count reaches zero the object deletes itself, so no
// owner may touch it after its final release.
class PageCallbackContextW10M {
public:
    // Reentrant detach hook invoked from inside the notify entry point.
    //
    // Lets a test drive detachCallbacks during dispatch and observe the Pending
    // answer plus the deferred release, without the context knowing what a
    // module object is. Null by default, in which case delivery only records.
    typedef WKW10MResult(__stdcall* DetachHookFunction)(void* hookContext, WKW10MPageHandle page);

    // Destruction hook invoked from the destructor with the registered context.
    //
    // Lets a test prove the object's storage survived until the detach
    // acknowledgment instead of being freed by the detach call. Null by
    // default. Runs before member storage is released and never calls out.
    typedef void(__stdcall* DestroyedHookFunction)(void* hookContext);

    // Builds a context with a setup reference count of one and empty verdicts.
    PageCallbackContextW10M();

    // Frees owned payload copies, then reports destruction through the hook.
    ~PageCallbackContextW10M();

    // Non-copyable owner identity; share the object instead of copying it.
    PageCallbackContextW10M(const PageCallbackContextW10M&) = delete;

    // Non-copyable owner identity; share the object instead of copying it.
    PageCallbackContextW10M& operator=(const PageCallbackContextW10M&) = delete;

    // Intrusive retain entry point for the engine's ownership protocol.
    void retain();

    // Intrusive release entry point; deletes the object at zero references.
    void release();

    // Current reference count, for tests that prove retain/release pairing.
    uint32_t refCount() const;

    // Engine retain-thunk calls observed, proving the provider retained once.
    uint32_t retainCalls() const;

    // Engine release-thunk calls observed, proving exactly-once acknowledgment.
    uint32_t releaseCalls() const;

    // Policy verdict the starting entry point reports; refuse by default.
    void setCancelVerdict(bool cancel);

    // Handler failure the starting entry point reports instead of a verdict.
    //
    // Unset by default, in which case starting answers with the verdict. Once
    // set, starting answers with this result so a test can prove a handler
    // failure travels without throwing. Clear by passing WKW10MResultOK.
    void setFailResult(WKW10MResult result);

    // Arms the reentrant detach hook invoked from inside the notify entry.
    void setDetachHook(DetachHookFunction hook, void* hookContext);

    // Whether the detach hook fired during the last notification.
    bool detachFired() const;

    // Answer the detach hook observed; meaningful only when detachFired.
    WKW10MResult lastDetachResult() const;

    // Reentrant policy hook invoked from inside the starting entry point.
    //
    // Lets a test drive a close (or any reentrant call) during policy dispatch
    // and observe the answer, without the context knowing what a module object
    // is. The hook runs after the payload copy and before the verdict is
    // applied, so the verdict still governs the answer. Null by default, in
    // which case policy delivery only records.
    typedef WKW10MResult(__stdcall* PolicyHookFunction)(
        void* hookContext, WKW10MPageHandle page, const WKW10MPageEvent* event);

    // Arms the reentrant policy hook invoked from inside the starting entry.
    void setPolicyHook(PolicyHookFunction hook, void* hookContext);

    // Whether the policy hook fired during the last policy dispatch.
    bool policyFired() const;

    // Answer the policy hook observed; meaningful only when policyFired.
    WKW10MResult lastPolicyResult() const;

    // Arms the destruction hook invoked from the destructor.
    void setDestroyedHook(DestroyedHookFunction hook, void* hookContext);

    // Fills a callback table that borrows nothing: every pointer is this
    // object's thunk or this object itself, and the header declares the V1
    // structure the provider validates before reading.
    WKW10MResult buildTable(WKW10MPageCallbacks* callbacks);

    // Owned copy of the last delivered URI; valid while this object lives.
    const uint8_t* lastUriBytes() const;

    // Length of the owned URI copy in bytes.
    uint32_t lastUriLength() const;

    // Owned copy of the last delivered title; valid while this object lives.
    const uint8_t* lastTitleBytes() const;

    // Length of the owned title copy in bytes.
    uint32_t lastTitleLength() const;

    // Event kind of the last delivered notification.
    uint32_t lastEventKind() const;

    // Callback sequence of the last delivered notification.
    uint64_t lastSequence() const;

    // Starting entry calls observed, including refused and failed ones.
    uint32_t startingCalls() const;

    // Notify entry calls observed with a usable event.
    uint32_t notifyCalls() const;

    // Null or malformed deliveries suppressed without throwing.
    uint32_t suppressedCalls() const;

    // Retain entry point exposed to the engine through the built table.
    static void __stdcall retainThunk(void* context);

    // Release entry point exposed to the engine through the built table.
    static void __stdcall releaseThunk(void* context);

    // Policy entry point exposed to the engine through the built table.
    static WKW10MResult __stdcall startingThunk(void* context, const WKW10MPageEvent* event, uint32_t* cancel);

    // Notification entry point exposed to the engine through the built table.
    static void __stdcall notifyThunk(void* context, const WKW10MPageEvent* event);

private:
    // Current intrusive reference count; one at construction for the setup ref.
    uint32_t m_refCount;

    // Thunk invocation counters proving each ownership transition happened once.
    uint32_t m_retainCalls;
    uint32_t m_releaseCalls;

    // Delivery counters proving every notification was recorded or suppressed.
    uint32_t m_startingCalls;
    uint32_t m_notifyCalls;
    uint32_t m_suppressedCalls;

    // Configured policy answer for the starting entry point.
    bool m_cancelVerdict;

    // Whether a handler failure overrides the verdict, and which result.
    bool m_failResultSet;
    WKW10MResult m_failResult;

    // Reentrant detach hook state, armed by tests that detach during dispatch.
    DetachHookFunction m_detachHook;
    void* m_detachHookContext;
    bool m_detachFired;
    WKW10MResult m_lastDetachResult;

    // Reentrant policy hook state, armed by tests that act during dispatch.
    PolicyHookFunction m_policyHook;
    void* m_policyHookContext;
    bool m_policyFired;
    WKW10MResult m_lastPolicyResult;

    // Destruction hook state, proving storage survived until acknowledgment.
    DestroyedHookFunction m_destroyedHook;
    void* m_destroyedHookContext;

    // Owned copies of the last delivered payload; null with zero length when
    // the last delivery carried nothing or was suppressed.
    uint8_t* m_lastUri;
    uint32_t m_lastUriLength;
    uint8_t* m_lastTitle;
    uint32_t m_lastTitleLength;

    // Kind and sequence of the last delivered notification.
    uint32_t m_lastEventKind;
    uint64_t m_lastSequence;

    // Copies one borrowed span into context-owned storage, freeing the
    // previous copy. False for a malformed span or a failed allocation, in
    // which case the previous copy is kept so no evidence is destroyed.
    bool copySpan(const WKW10MString& span, uint8_t** storage, uint32_t* length);

    // Records the kind and sequence of a delivered notification.
    void recordDelivery(const WKW10MPageEvent& event);
};

// Caller-owned page-state snapshot with bounded size-probing retry.
//
// getState first computes the needed bytes and reports BufferTooSmall with only
// requiredBytes written when the caller's storage is short. This helper starts
// small, grows to exactly the reported need, and gives up instead of looping
// forever when a provider keeps growing its answer. It owns its storage on its
// own heap and frees it on destruction.
class PageStateSnapshotW10M {
public:
    // Maximum fetch attempts before giving up, bounding a growing provider.
    static constexpr uint32_t kMaxFetchAttempts = 5;

    // First storage size; smaller than any real need, so the retry path that
    // grows to the reported need is always exercised instead of succeeding
    // immediately on a lucky initial size.
    static constexpr uint32_t kInitialStorageBytes = 16;

    // Largest storage ever allocated; larger needs fail fast instead.
    static constexpr uint32_t kLargestStorageBytes = 1 << 20;

    // Builds an empty snapshot with no storage and no attempts recorded.
    PageStateSnapshotW10M();

    // Frees owned storage; the borrowed state view dies with it.
    ~PageStateSnapshotW10M();

    // Non-copyable snapshot ownership; fetch again instead of copying it.
    PageStateSnapshotW10M(const PageStateSnapshotW10M&) = delete;

    // Non-copyable snapshot ownership; fetch again instead of copying it.
    PageStateSnapshotW10M& operator=(const PageStateSnapshotW10M&) = delete;

    // Fetches the state through the negotiated table with bounded retry.
    //
    // Retries only on BufferTooSmall with a strictly larger reported need; any
    // other engine answer, a non-progressing need, an absurd need or an
    // allocation failure ends the fetch with that answer. Null table reports
    // InvalidArgument before touching anything.
    WKW10MResult fetch(const WKW10MEngineAPI* table, WKW10MPageHandle page);

    // Last fetched state; meaningful only after fetch reports success.
    const WKW10MPageState* state() const;

    // Owned string storage the state's spans point into.
    const uint8_t* storage() const;

    // Owned storage size in bytes.
    uint32_t storageBytes() const;

    // Engine calls performed by the last fetch, proving retry happened.
    uint32_t attempts() const;

    // Last need the engine reported, proving the growth was exact.
    uint32_t requiredBytes() const;

private:
    // Last fetched state; the string spans point into m_storage while it lives.
    WKW10MPageState m_state;

    // Owned storage for the state's strings, grown to the reported need.
    uint8_t* m_storage;

    // Current owned storage size in bytes.
    uint32_t m_storageBytes;

    // Engine calls performed by the last fetch.
    uint32_t m_attempts;

    // Last need the engine reported.
    uint32_t m_requiredBytes;
};

} // namespace WebKit

#endif // CALLBACK_CONTEXT_W10M_H


