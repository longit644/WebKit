/*
 * CallbackContextW10M.cpp - consumer-side callback context and state snapshot.
 *
 * Implements the ownership, copying and retry rules documented in
 * CallbackContextW10M.h. Like the module consumer this file is dependency-free
 * plain C++: nothrow allocation is the only heap use, and no entry point lets
 * an exception cross the boundary because none is ever thrown.
 */

#include "CallbackContextW10M.h"

#include <new>
#include <string.h>

namespace WebKit {

// See CallbackContextW10M.h: one setup reference, empty verdicts, no hooks.
PageCallbackContextW10M::PageCallbackContextW10M()
    : m_refCount(1)
    , m_retainCalls(0)
    , m_releaseCalls(0)
    , m_startingCalls(0)
    , m_notifyCalls(0)
    , m_suppressedCalls(0)
    , m_cancelVerdict(false)
    , m_failResultSet(false)
    , m_failResult(WKW10MResultOK)
    , m_detachHook(nullptr)
    , m_detachHookContext(nullptr)
    , m_detachFired(false)
    , m_lastDetachResult(WKW10MResultOK)
    , m_policyHook(nullptr)
    , m_policyHookContext(nullptr)
    , m_policyFired(false)
    , m_lastPolicyResult(WKW10MResultOK)
    , m_destroyedHook(nullptr)
    , m_destroyedHookContext(nullptr)
    , m_lastUri(nullptr)
    , m_lastUriLength(0)
    , m_lastTitle(nullptr)
    , m_lastTitleLength(0)
    , m_lastEventKind(0)
    , m_lastSequence(0)
{
}

// See CallbackContextW10M.h: payload copies die with the context, after the hook.
PageCallbackContextW10M::~PageCallbackContextW10M()
{
    if (m_destroyedHook)
        m_destroyedHook(m_destroyedHookContext);
    delete[] m_lastUri;
    delete[] m_lastTitle;
}

// See CallbackContextW10M.h: the engine's ownership protocol entry point.
void PageCallbackContextW10M::retain()
{
    ++m_refCount;
    ++m_retainCalls;
}

// See CallbackContextW10M.h: storage is freed only by this final release.
void PageCallbackContextW10M::release()
{
    ++m_releaseCalls;
    if (--m_refCount == 0)
        delete this;
}

// See CallbackContextW10M.h for the counter meanings.
uint32_t PageCallbackContextW10M::refCount() const
{
    return m_refCount;
}

// See CallbackContextW10M.h for the counter meanings.
uint32_t PageCallbackContextW10M::retainCalls() const
{
    return m_retainCalls;
}

// See CallbackContextW10M.h for the counter meanings.
uint32_t PageCallbackContextW10M::releaseCalls() const
{
    return m_releaseCalls;
}

// See CallbackContextW10M.h: refuse by default, allow when armed by policy.
void PageCallbackContextW10M::setCancelVerdict(bool cancel)
{
    m_cancelVerdict = cancel;
}

// See CallbackContextW10M.h: OK clears the override back to verdict mode.
void PageCallbackContextW10M::setFailResult(WKW10MResult result)
{
    m_failResultSet = result != WKW10MResultOK;
    m_failResult = result;
}

// See CallbackContextW10M.h: armed by tests that detach during dispatch.
void PageCallbackContextW10M::setDetachHook(DetachHookFunction hook, void* hookContext)
{
    m_detachHook = hook;
    m_detachHookContext = hookContext;
    m_detachFired = false;
}

// See CallbackContextW10M.h for the hook observation meanings.
bool PageCallbackContextW10M::detachFired() const
{
    return m_detachFired;
}

// See CallbackContextW10M.h for the hook observation meanings.
WKW10MResult PageCallbackContextW10M::lastDetachResult() const
{
    return m_lastDetachResult;
}

// See CallbackContextW10M.h: armed by tests that act during policy dispatch.
void PageCallbackContextW10M::setPolicyHook(PolicyHookFunction hook, void* hookContext)
{
    m_policyHook = hook;
    m_policyHookContext = hookContext;
    m_policyFired = false;
}

// See CallbackContextW10M.h for the hook observation meanings.
bool PageCallbackContextW10M::policyFired() const
{
    return m_policyFired;
}

// See CallbackContextW10M.h for the hook observation meanings.
WKW10MResult PageCallbackContextW10M::lastPolicyResult() const
{
    return m_lastPolicyResult;
}

// See CallbackContextW10M.h: proves storage survived until acknowledgment.
void PageCallbackContextW10M::setDestroyedHook(DestroyedHookFunction hook, void* hookContext)
{
    m_destroyedHook = hook;
    m_destroyedHookContext = hookContext;
}

// See CallbackContextW10M.h: every pointer borrows nothing outside this object.
WKW10MResult PageCallbackContextW10M::buildTable(WKW10MPageCallbacks* callbacks)
{
    if (!callbacks)
        return WKW10MResultInvalidArgument;
    memset(callbacks, 0, sizeof(*callbacks));
    callbacks->header.structureSize = sizeof(*callbacks);
    callbacks->header.structureVersion = WKW10M_STRUCTURE_VERSION;
    callbacks->context = this;
    callbacks->retainContext = retainThunk;
    callbacks->releaseContext = releaseThunk;
    callbacks->starting = startingThunk;
    callbacks->notify = notifyThunk;
    return WKW10MResultOK;
}

// See CallbackContextW10M.h for the snapshot accessor meanings.
const uint8_t* PageCallbackContextW10M::lastUriBytes() const
{
    return m_lastUri;
}

// See CallbackContextW10M.h for the snapshot accessor meanings.
uint32_t PageCallbackContextW10M::lastUriLength() const
{
    return m_lastUriLength;
}

// See CallbackContextW10M.h for the snapshot accessor meanings.
const uint8_t* PageCallbackContextW10M::lastTitleBytes() const
{
    return m_lastTitle;
}

// See CallbackContextW10M.h for the snapshot accessor meanings.
uint32_t PageCallbackContextW10M::lastTitleLength() const
{
    return m_lastTitleLength;
}

// See CallbackContextW10M.h for the snapshot accessor meanings.
uint32_t PageCallbackContextW10M::lastEventKind() const
{
    return m_lastEventKind;
}

// See CallbackContextW10M.h for the snapshot accessor meanings.
uint64_t PageCallbackContextW10M::lastSequence() const
{
    return m_lastSequence;
}

// See CallbackContextW10M.h for the counter meanings.
uint32_t PageCallbackContextW10M::startingCalls() const
{
    return m_startingCalls;
}

// See CallbackContextW10M.h for the counter meanings.
uint32_t PageCallbackContextW10M::notifyCalls() const
{
    return m_notifyCalls;
}

// See CallbackContextW10M.h for the counter meanings.
uint32_t PageCallbackContextW10M::suppressedCalls() const
{
    return m_suppressedCalls;
}

// See CallbackContextW10M.h: malformed spans fail, allocation failure reports.
bool PageCallbackContextW10M::copySpan(const WKW10MString& span, uint8_t** storage, uint32_t* length)
{
    // A nonzero length with no data violates the borrowed-span rule, so the
    // delivery is malformed rather than empty.
    if (span.length > 0 && !span.data)
        return false;
    if (span.length == 0) {
        delete[] *storage;
        *storage = nullptr;
        *length = 0;
        return true;
    }
    uint8_t* copy = new (std::nothrow) uint8_t[span.length];
    if (!copy)
        return false;
    memcpy(copy, span.data, span.length);
    delete[] *storage;
    *storage = copy;
    *length = span.length;
    return true;
}

// See CallbackContextW10M.h: total order metadata travels with the payload copy.
void PageCallbackContextW10M::recordDelivery(const WKW10MPageEvent& event)
{
    m_lastEventKind = event.eventKind;
    m_lastSequence = event.callbackSequence;
}

// See CallbackContextW10M.h: null context is ignored, never dereferenced.
void __stdcall PageCallbackContextW10M::retainThunk(void* context)
{
    if (!context)
        return;
    static_cast<PageCallbackContextW10M*>(context)->retain();
}

// See CallbackContextW10M.h: null context is ignored, never dereferenced.
void __stdcall PageCallbackContextW10M::releaseThunk(void* context)
{
    if (!context)
        return;
    static_cast<PageCallbackContextW10M*>(context)->release();
}

// See CallbackContextW10M.h: malformed policy input fails, never throws.
WKW10MResult __stdcall PageCallbackContextW10M::startingThunk(
    void* context, const WKW10MPageEvent* event, uint32_t* cancel)
{
    if (!context || !event || !cancel)
        return WKW10MResultInvalidArgument;
    PageCallbackContextW10M* self = static_cast<PageCallbackContextW10M*>(context);
    ++self->m_startingCalls;
    if (!self->copySpan(event->uri, &self->m_lastUri, &self->m_lastUriLength)
        || !self->copySpan(event->title, &self->m_lastTitle, &self->m_lastTitleLength))
        return WKW10MResultInvalidArgument;
    self->recordDelivery(*event);
    // An armed hook acts reentrantly from inside this dispatch, before the
    // verdict is applied; whatever it observes is recorded, and the verdict
    // still governs the answer. This is how a close during policy dispatch is
    // driven without the context knowing what a module object is.
    if (self->m_policyHook) {
        self->m_policyFired = true;
        self->m_lastPolicyResult = self->m_policyHook(self->m_policyHookContext, event->pageId, event);
    }

    // A configured handler failure travels as its result so the engine can map
    // it; nothing here throws it across the boundary.
    if (self->m_failResultSet)
        return self->m_failResult;
    *cancel = self->m_cancelVerdict ? 1 : 0;
    return WKW10MResultOK;
}

// See CallbackContextW10M.h: suppression records, and the hook may detach.
void __stdcall PageCallbackContextW10M::notifyThunk(void* context, const WKW10MPageEvent* event)
{
    if (!context || !event) {
        if (context)
            ++static_cast<PageCallbackContextW10M*>(context)->m_suppressedCalls;
        return;
    }
    PageCallbackContextW10M* self = static_cast<PageCallbackContextW10M*>(context);
    if (!self->copySpan(event->uri, &self->m_lastUri, &self->m_lastUriLength)
        || !self->copySpan(event->title, &self->m_lastTitle, &self->m_lastTitleLength)) {
        ++self->m_suppressedCalls;
        return;
    }
    ++self->m_notifyCalls;
    self->recordDelivery(*event);
    // An armed hook detaches reentrantly from inside this dispatch; the Pending
    // answer it observes is recorded, and this object's storage stays alive
    // because the release acknowledging it has not run yet.
    if (self->m_detachHook) {
        self->m_detachFired = true;
        self->m_lastDetachResult = self->m_detachHook(self->m_detachHookContext, event->pageId);
    }
}

// See CallbackContextW10M.h: empty snapshot, no storage, nothing attempted.
PageStateSnapshotW10M::PageStateSnapshotW10M()
    : m_storage(nullptr)
    , m_storageBytes(0)
    , m_attempts(0)
    , m_requiredBytes(0)
{
    memset(&m_state, 0, sizeof(m_state));
}

// See CallbackContextW10M.h: owned storage dies with the snapshot.
PageStateSnapshotW10M::~PageStateSnapshotW10M()
{
    delete[] m_storage;
}

// See CallbackContextW10M.h for the bounded retry contract.
WKW10MResult PageStateSnapshotW10M::fetch(const WKW10MEngineAPI* table, WKW10MPageHandle page)
{
    if (!table || !table->getState)
        return WKW10MResultInvalidArgument;

    m_attempts = 0;
    m_requiredBytes = 0;
    delete[] m_storage;
    m_storage = new (std::nothrow) uint8_t[kInitialStorageBytes];
    if (!m_storage)
        return WKW10MResultBudgetExceeded;
    m_storageBytes = kInitialStorageBytes;

    for (;;) {
        memset(&m_state, 0, sizeof(m_state));
        m_state.header.structureSize = sizeof(m_state);
        m_state.header.structureVersion = WKW10M_STRUCTURE_VERSION;
        uint32_t required = 0;
        WKW10MResult result = table->getState(page, &m_state, m_storage, m_storageBytes, &required);
        ++m_attempts;
        if (result != WKW10MResultBufferTooSmall) {
            m_requiredBytes = required;
            return result;
        }

        // A need that did not grow means the provider cannot make progress;
        // retrying the same size forever would be the caller's bug, not proof.
        if (required <= m_storageBytes || m_attempts >= kMaxFetchAttempts) {
            m_requiredBytes = required;
            return WKW10MResultBufferTooSmall;
        }

        // An absurd need fails fast instead of attempting a giant allocation.
        if (required > kLargestStorageBytes) {
            m_requiredBytes = required;
            return WKW10MResultBudgetExceeded;
        }
        uint8_t* grown = new (std::nothrow) uint8_t[required];
        if (!grown) {
            m_requiredBytes = required;
            return WKW10MResultBudgetExceeded;
        }
        delete[] m_storage;
        m_storage = grown;
        m_storageBytes = required;
    }
}

// See CallbackContextW10M.h for the accessor meanings.
const WKW10MPageState* PageStateSnapshotW10M::state() const
{
    return &m_state;
}

// See CallbackContextW10M.h for the accessor meanings.
const uint8_t* PageStateSnapshotW10M::storage() const
{
    return m_storage;
}

// See CallbackContextW10M.h for the accessor meanings.
uint32_t PageStateSnapshotW10M::storageBytes() const
{
    return m_storageBytes;
}

// See CallbackContextW10M.h for the accessor meanings.
uint32_t PageStateSnapshotW10M::attempts() const
{
    return m_attempts;
}

// See CallbackContextW10M.h for the accessor meanings.
uint32_t PageStateSnapshotW10M::requiredBytes() const
{
    return m_requiredBytes;
}

} // namespace WebKit


