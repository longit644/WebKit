/*
 * CallbackContextUWP.cpp - consumer-side callback context and state snapshot.
 *
 * Implements the ownership, copying and retry rules documented in
 * CallbackContextUWP.h. Like the module consumer this file is dependency-free
 * plain C++: nothrow allocation is the only heap use, and no entry point lets
 * an exception cross the boundary because none is ever thrown.
 */

#include "CallbackContextUWP.h"

#include <new>
#include <string.h>

namespace WebKit {

// See CallbackContextUWP.h: one setup reference, empty verdicts, no hooks.
PageCallbackContextUWP::PageCallbackContextUWP()
    : m_refCount(1)
    , m_retainCalls(0)
    , m_releaseCalls(0)
    , m_startingCalls(0)
    , m_notifyCalls(0)
    , m_suppressedCalls(0)
    , m_cancelVerdict(false)
    , m_failResultSet(false)
    , m_failResult(WKUWPResultOK)
    , m_detachHook(nullptr)
    , m_detachHookContext(nullptr)
    , m_detachFired(false)
    , m_lastDetachResult(WKUWPResultOK)
    , m_policyHook(nullptr)
    , m_policyHookContext(nullptr)
    , m_policyFired(false)
    , m_lastPolicyResult(WKUWPResultOK)
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

// See CallbackContextUWP.h: payload copies die with the context, after the hook.
PageCallbackContextUWP::~PageCallbackContextUWP()
{
    if (m_destroyedHook)
        m_destroyedHook(m_destroyedHookContext);
    delete[] m_lastUri;
    delete[] m_lastTitle;
}

// See CallbackContextUWP.h: the engine's ownership protocol entry point.
void PageCallbackContextUWP::retain()
{
    ++m_refCount;
    ++m_retainCalls;
}

// See CallbackContextUWP.h: storage is freed only by this final release.
void PageCallbackContextUWP::release()
{
    ++m_releaseCalls;
    if (--m_refCount == 0)
        delete this;
}

// See CallbackContextUWP.h for the counter meanings.
uint32_t PageCallbackContextUWP::refCount() const
{
    return m_refCount;
}

// See CallbackContextUWP.h for the counter meanings.
uint32_t PageCallbackContextUWP::retainCalls() const
{
    return m_retainCalls;
}

// See CallbackContextUWP.h for the counter meanings.
uint32_t PageCallbackContextUWP::releaseCalls() const
{
    return m_releaseCalls;
}

// See CallbackContextUWP.h: refuse by default, allow when armed by policy.
void PageCallbackContextUWP::setCancelVerdict(bool cancel)
{
    m_cancelVerdict = cancel;
}

// See CallbackContextUWP.h: OK clears the override back to verdict mode.
void PageCallbackContextUWP::setFailResult(WKUWPResult result)
{
    m_failResultSet = result != WKUWPResultOK;
    m_failResult = result;
}

// See CallbackContextUWP.h: armed by tests that detach during dispatch.
void PageCallbackContextUWP::setDetachHook(DetachHookFunction hook, void* hookContext)
{
    m_detachHook = hook;
    m_detachHookContext = hookContext;
    m_detachFired = false;
}

// See CallbackContextUWP.h for the hook observation meanings.
bool PageCallbackContextUWP::detachFired() const
{
    return m_detachFired;
}

// See CallbackContextUWP.h for the hook observation meanings.
WKUWPResult PageCallbackContextUWP::lastDetachResult() const
{
    return m_lastDetachResult;
}

// See CallbackContextUWP.h: armed by tests that act during policy dispatch.
void PageCallbackContextUWP::setPolicyHook(PolicyHookFunction hook, void* hookContext)
{
    m_policyHook = hook;
    m_policyHookContext = hookContext;
    m_policyFired = false;
}

// See CallbackContextUWP.h for the hook observation meanings.
bool PageCallbackContextUWP::policyFired() const
{
    return m_policyFired;
}

// See CallbackContextUWP.h for the hook observation meanings.
WKUWPResult PageCallbackContextUWP::lastPolicyResult() const
{
    return m_lastPolicyResult;
}

// See CallbackContextUWP.h: proves storage survived until acknowledgment.
void PageCallbackContextUWP::setDestroyedHook(DestroyedHookFunction hook, void* hookContext)
{
    m_destroyedHook = hook;
    m_destroyedHookContext = hookContext;
}

// See CallbackContextUWP.h: every pointer borrows nothing outside this object.
WKUWPResult PageCallbackContextUWP::buildTable(WKUWPPageCallbacks* callbacks)
{
    if (!callbacks)
        return WKUWPResultInvalidArgument;
    memset(callbacks, 0, sizeof(*callbacks));
    callbacks->header.structureSize = sizeof(*callbacks);
    callbacks->header.structureVersion = WKUWP_STRUCTURE_VERSION;
    callbacks->context = this;
    callbacks->retainContext = retainThunk;
    callbacks->releaseContext = releaseThunk;
    callbacks->starting = startingThunk;
    callbacks->notify = notifyThunk;
    return WKUWPResultOK;
}

// See CallbackContextUWP.h for the snapshot accessor meanings.
const uint8_t* PageCallbackContextUWP::lastUriBytes() const
{
    return m_lastUri;
}

// See CallbackContextUWP.h for the snapshot accessor meanings.
uint32_t PageCallbackContextUWP::lastUriLength() const
{
    return m_lastUriLength;
}

// See CallbackContextUWP.h for the snapshot accessor meanings.
const uint8_t* PageCallbackContextUWP::lastTitleBytes() const
{
    return m_lastTitle;
}

// See CallbackContextUWP.h for the snapshot accessor meanings.
uint32_t PageCallbackContextUWP::lastTitleLength() const
{
    return m_lastTitleLength;
}

// See CallbackContextUWP.h for the snapshot accessor meanings.
uint32_t PageCallbackContextUWP::lastEventKind() const
{
    return m_lastEventKind;
}

// See CallbackContextUWP.h for the snapshot accessor meanings.
uint64_t PageCallbackContextUWP::lastSequence() const
{
    return m_lastSequence;
}

// See CallbackContextUWP.h for the counter meanings.
uint32_t PageCallbackContextUWP::startingCalls() const
{
    return m_startingCalls;
}

// See CallbackContextUWP.h for the counter meanings.
uint32_t PageCallbackContextUWP::notifyCalls() const
{
    return m_notifyCalls;
}

// See CallbackContextUWP.h for the counter meanings.
uint32_t PageCallbackContextUWP::suppressedCalls() const
{
    return m_suppressedCalls;
}

// See CallbackContextUWP.h: malformed spans fail, allocation failure reports.
bool PageCallbackContextUWP::copySpan(const WKUWPString& span, uint8_t** storage, uint32_t* length)
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

// See CallbackContextUWP.h: total order metadata travels with the payload copy.
void PageCallbackContextUWP::recordDelivery(const WKUWPPageEvent& event)
{
    m_lastEventKind = event.eventKind;
    m_lastSequence = event.callbackSequence;
}

// See CallbackContextUWP.h: null context is ignored, never dereferenced.
void __stdcall PageCallbackContextUWP::retainThunk(void* context)
{
    if (!context)
        return;
    static_cast<PageCallbackContextUWP*>(context)->retain();
}

// See CallbackContextUWP.h: null context is ignored, never dereferenced.
void __stdcall PageCallbackContextUWP::releaseThunk(void* context)
{
    if (!context)
        return;
    static_cast<PageCallbackContextUWP*>(context)->release();
}

// See CallbackContextUWP.h: malformed policy input fails, never throws.
WKUWPResult __stdcall PageCallbackContextUWP::startingThunk(
    void* context, const WKUWPPageEvent* event, uint32_t* cancel)
{
    if (!context || !event || !cancel)
        return WKUWPResultInvalidArgument;
    PageCallbackContextUWP* self = static_cast<PageCallbackContextUWP*>(context);
    ++self->m_startingCalls;
    if (!self->copySpan(event->uri, &self->m_lastUri, &self->m_lastUriLength)
        || !self->copySpan(event->title, &self->m_lastTitle, &self->m_lastTitleLength))
        return WKUWPResultInvalidArgument;
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
    return WKUWPResultOK;
}

// See CallbackContextUWP.h: suppression records, and the hook may detach.
void __stdcall PageCallbackContextUWP::notifyThunk(void* context, const WKUWPPageEvent* event)
{
    if (!context || !event) {
        if (context)
            ++static_cast<PageCallbackContextUWP*>(context)->m_suppressedCalls;
        return;
    }
    PageCallbackContextUWP* self = static_cast<PageCallbackContextUWP*>(context);
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

// See CallbackContextUWP.h: empty snapshot, no storage, nothing attempted.
PageStateSnapshotUWP::PageStateSnapshotUWP()
    : m_storage(nullptr)
    , m_storageBytes(0)
    , m_attempts(0)
    , m_requiredBytes(0)
{
    memset(&m_state, 0, sizeof(m_state));
}

// See CallbackContextUWP.h: owned storage dies with the snapshot.
PageStateSnapshotUWP::~PageStateSnapshotUWP()
{
    delete[] m_storage;
}

// See CallbackContextUWP.h for the bounded retry contract.
WKUWPResult PageStateSnapshotUWP::fetch(const WKUWPEngineAPI* table, WKUWPPageHandle page)
{
    if (!table || !table->getState)
        return WKUWPResultInvalidArgument;

    m_attempts = 0;
    m_requiredBytes = 0;
    delete[] m_storage;
    m_storage = new (std::nothrow) uint8_t[kInitialStorageBytes];
    if (!m_storage)
        return WKUWPResultBudgetExceeded;
    m_storageBytes = kInitialStorageBytes;

    for (;;) {
        memset(&m_state, 0, sizeof(m_state));
        m_state.header.structureSize = sizeof(m_state);
        m_state.header.structureVersion = WKUWP_STRUCTURE_VERSION;
        uint32_t required = 0;
        WKUWPResult result = table->getState(page, &m_state, m_storage, m_storageBytes, &required);
        ++m_attempts;
        if (result != WKUWPResultBufferTooSmall) {
            m_requiredBytes = required;
            return result;
        }

        // A need that did not grow means the provider cannot make progress;
        // retrying the same size forever would be the caller's bug, not proof.
        if (required <= m_storageBytes || m_attempts >= kMaxFetchAttempts) {
            m_requiredBytes = required;
            return WKUWPResultBufferTooSmall;
        }

        // An absurd need fails fast instead of attempting a giant allocation.
        if (required > kLargestStorageBytes) {
            m_requiredBytes = required;
            return WKUWPResultBudgetExceeded;
        }
        uint8_t* grown = new (std::nothrow) uint8_t[required];
        if (!grown) {
            m_requiredBytes = required;
            return WKUWPResultBudgetExceeded;
        }
        delete[] m_storage;
        m_storage = grown;
        m_storageBytes = required;
    }
}

// See CallbackContextUWP.h for the accessor meanings.
const WKUWPPageState* PageStateSnapshotUWP::state() const
{
    return &m_state;
}

// See CallbackContextUWP.h for the accessor meanings.
const uint8_t* PageStateSnapshotUWP::storage() const
{
    return m_storage;
}

// See CallbackContextUWP.h for the accessor meanings.
uint32_t PageStateSnapshotUWP::storageBytes() const
{
    return m_storageBytes;
}

// See CallbackContextUWP.h for the accessor meanings.
uint32_t PageStateSnapshotUWP::attempts() const
{
    return m_attempts;
}

// See CallbackContextUWP.h for the accessor meanings.
uint32_t PageStateSnapshotUWP::requiredBytes() const
{
    return m_requiredBytes;
}

} // namespace WebKit
