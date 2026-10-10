/*
 * PlatformServiceUWP.cpp - consumer-side platform service context.
 *
 * Implements the exactly-once completion, Write-text copying and ownership
 * rules documented in PlatformServiceUWP.h. Like its neighbours this file is
 * dependency-free plain C++: nothrow allocation is the only heap use, and no
 * entry point lets an exception cross the boundary because none is ever thrown.
 */

#include "PlatformServiceUWP.h"

#include <new>
#include <string.h>

namespace WebKit {

// Cancellation result reported to a canceled operation's completion.
//
// The contract names no Cancelled operation code, so cancellation travels as
// the platform HRESULT for ERROR_CANCELLED, which is negative and therefore
// keeps its identity under the contract's HRESULT rule.
WKUWPResult canceledClipboardResult()
{
    HRESULT cancelled = HRESULT_FROM_WIN32(ERROR_CANCELLED);
    return static_cast<WKUWPResult>(static_cast<int32_t>(cancelled));
}

// See PlatformServiceUWP.h: one setup reference, no operations, no hooks.
PlatformServiceContextUWP::PlatformServiceContextUWP()
    : m_refCount(1)
    , m_retainCalls(0)
    , m_releaseCalls(0)
    , m_wakeCalls(0)
    , m_destroyedHook(nullptr)
    , m_destroyedHookContext(nullptr)
{
    for (uint32_t index = 0; index < kMaxPendingOperations; ++index) {
        m_operations[index].active = false;
        m_operations[index].id = 0;
        m_operations[index].kind = 0;
        m_operations[index].completion = nullptr;
        m_operations[index].completionContext = nullptr;
        m_operations[index].textCopy = nullptr;
        m_operations[index].textLength = 0;
    }
}

// See PlatformServiceUWP.h: copied text dies with the service, after the hook.
PlatformServiceContextUWP::~PlatformServiceContextUWP()
{
    if (m_destroyedHook)
        m_destroyedHook(m_destroyedHookContext);
    for (uint32_t index = 0; index < kMaxPendingOperations; ++index)
        delete[] m_operations[index].textCopy;
}

// See PlatformServiceUWP.h: the engine's ownership protocol entry point.
void PlatformServiceContextUWP::retain()
{
    ++m_refCount;
    ++m_retainCalls;
}

// See PlatformServiceUWP.h: storage is freed only by this final release.
void PlatformServiceContextUWP::release()
{
    ++m_releaseCalls;
    if (--m_refCount == 0)
        delete this;
}

// See PlatformServiceUWP.h for the counter meanings.
uint32_t PlatformServiceContextUWP::refCount() const
{
    return m_refCount;
}

// See PlatformServiceUWP.h for the counter meanings.
uint32_t PlatformServiceContextUWP::retainCalls() const
{
    return m_retainCalls;
}

// See PlatformServiceUWP.h for the counter meanings.
uint32_t PlatformServiceContextUWP::releaseCalls() const
{
    return m_releaseCalls;
}

// See PlatformServiceUWP.h: proves survival until quiescence.
void PlatformServiceContextUWP::setDestroyedHook(DestroyedHookFunction hook, void* hookContext)
{
    m_destroyedHook = hook;
    m_destroyedHookContext = hookContext;
}

// See PlatformServiceUWP.h: every pointer borrows nothing outside this object.
WKUWPResult PlatformServiceContextUWP::buildTable(WKUWPPlatformServices* services)
{
    if (!services)
        return WKUWPResultInvalidArgument;
    memset(services, 0, sizeof(*services));
    services->header.structureSize = sizeof(*services);
    services->header.structureVersion = WKUWP_STRUCTURE_VERSION;
    services->context = this;
    services->retainContext = retainThunk;
    services->releaseContext = releaseThunk;
    services->requestClipboard = requestClipboardThunk;
    services->cancelClipboard = cancelClipboardThunk;
    services->postWake = postWakeThunk;
    return WKUWPResultOK;
}

// See PlatformServiceUWP.h for the counter meanings.
uint32_t PlatformServiceContextUWP::pendingCount() const
{
    uint32_t pending = 0;
    for (uint32_t index = 0; index < kMaxPendingOperations; ++index) {
        if (m_operations[index].active)
            ++pending;
    }
    return pending;
}

// See PlatformServiceUWP.h for the counter meanings.
uint32_t PlatformServiceContextUWP::wakeCalls() const
{
    return m_wakeCalls;
}

// See PlatformServiceUWP.h: the host's own I/O finishing, delivered once.
WKUWPResult PlatformServiceContextUWP::deliverCompletion(
    uint64_t operationId, WKUWPResult result, const WKUWPString* text)
{
    if (text && text->length > 0 && !text->data)
        return WKUWPResultInvalidArgument;
    uint32_t index = findOperation(operationId);
    if (index == kMaxPendingOperations)
        return WKUWPResultInvalidArgument;
    WKUWPClipboardCompletion completion = m_operations[index].completion;
    void* completionContext = m_operations[index].completionContext;
    // Finished before invoking, so a reentrant deliver or cancel for the same
    // identifier finds nothing and cannot complete twice.
    finishOperation(index);
    completion(completionContext, operationId, result, text);
    return WKUWPResultOK;
}

// See PlatformServiceUWP.h: test support proving the Write copy outlives calls.
bool PlatformServiceContextUWP::copyOutText(
    uint64_t operationId, uint8_t* out, uint32_t capacity, uint32_t* needed)
{
    uint32_t index = findOperation(operationId);
    if (index == kMaxPendingOperations)
        return false;
    if (needed)
        *needed = m_operations[index].textLength;
    if (!out || capacity < m_operations[index].textLength)
        return false;
    if (m_operations[index].textLength > 0)
        memcpy(out, m_operations[index].textCopy, m_operations[index].textLength);
    return true;
}

// See PlatformServiceUWP.h for the slot meanings.
uint32_t PlatformServiceContextUWP::findOperation(uint64_t operationId) const
{
    for (uint32_t index = 0; index < kMaxPendingOperations; ++index) {
        if (m_operations[index].active && m_operations[index].id == operationId)
            return index;
    }
    return kMaxPendingOperations;
}

// See PlatformServiceUWP.h: inactivation precedes every completion call.
void PlatformServiceContextUWP::finishOperation(uint32_t index)
{
    m_operations[index].active = false;
    delete[] m_operations[index].textCopy;
    m_operations[index].textCopy = nullptr;
    m_operations[index].textLength = 0;
}

// See PlatformServiceUWP.h: null context is ignored, never dereferenced.
void __stdcall PlatformServiceContextUWP::retainThunk(void* context)
{
    if (!context)
        return;
    static_cast<PlatformServiceContextUWP*>(context)->retain();
}

// See PlatformServiceUWP.h: null context is ignored, never dereferenced.
void __stdcall PlatformServiceContextUWP::releaseThunk(void* context)
{
    if (!context)
        return;
    static_cast<PlatformServiceContextUWP*>(context)->release();
}

// See PlatformServiceUWP.h: every rejection completes nothing, ever.
WKUWPResult __stdcall PlatformServiceContextUWP::requestClipboardThunk(void* context, WKUWPPageHandle page,
    uint64_t operationId, uint32_t kind, const WKUWPString* text, WKUWPClipboardCompletion completion,
    void* completionContext)
{
    if (!context)
        return WKUWPResultInvalidArgument;
    PlatformServiceContextUWP* self = static_cast<PlatformServiceContextUWP*>(context);
    // A request that cannot be completed exactly once must not be accepted: a
    // null completion leaves no one to call, an unknown page or kind names no
    // valid operation, and a content-bearing read is malformed.
    if (!completion || page == 0 || kind > WKUWPClipboardWrite)
        return WKUWPResultInvalidArgument;
    const uint32_t textLength = text ? text->length : 0;
    if (textLength > 0 && !text->data)
        return WKUWPResultInvalidArgument;
    if (kind == WKUWPClipboardRead && textLength > 0)
        return WKUWPResultInvalidArgument;
    if (self->findOperation(operationId) != kMaxPendingOperations)
        return WKUWPResultInvalidState;
    uint32_t slot = kMaxPendingOperations;
    for (uint32_t index = 0; index < kMaxPendingOperations; ++index) {
        if (!self->m_operations[index].active) {
            slot = index;
            break;
        }
    }
    if (slot == kMaxPendingOperations)
        return WKUWPResultBudgetExceeded;
    // Write text is copied on acceptance so the caller's buffer may die with
    // the call; a failed copy rejects the request instead of accepting text
    // this object does not own.
    uint8_t* textCopy = nullptr;
    if (kind == WKUWPClipboardWrite && textLength > 0) {
        textCopy = new (std::nothrow) uint8_t[textLength];
        if (!textCopy)
            return WKUWPResultBudgetExceeded;
        memcpy(textCopy, text->data, textLength);
    }
    self->m_operations[slot].active = true;
    self->m_operations[slot].id = operationId;
    self->m_operations[slot].kind = kind;
    self->m_operations[slot].completion = completion;
    self->m_operations[slot].completionContext = completionContext;
    self->m_operations[slot].textCopy = textCopy;
    self->m_operations[slot].textLength = textLength;
    return WKUWPResultOK;
}

// See PlatformServiceUWP.h: cancellation still completes, exactly once.
WKUWPResult __stdcall PlatformServiceContextUWP::cancelClipboardThunk(void* context, uint64_t operationId)
{
    if (!context)
        return WKUWPResultInvalidArgument;
    PlatformServiceContextUWP* self = static_cast<PlatformServiceContextUWP*>(context);
    uint32_t index = self->findOperation(operationId);
    if (index == kMaxPendingOperations)
        return WKUWPResultInvalidArgument;
    WKUWPClipboardCompletion completion = self->m_operations[index].completion;
    void* completionContext = self->m_operations[index].completionContext;
    // Finished before invoking, so the canceled completion is the operation's
    // only completion even if it reenters cancel or delivery.
    self->finishOperation(index);
    completion(completionContext, operationId, canceledClipboardResult(), nullptr);
    return WKUWPResultOK;
}

// See PlatformServiceUWP.h: records the enqueue request without touching pages.
WKUWPResult __stdcall PlatformServiceContextUWP::postWakeThunk(void* context)
{
    if (!context)
        return WKUWPResultInvalidArgument;
    ++static_cast<PlatformServiceContextUWP*>(context)->m_wakeCalls;
    return WKUWPResultOK;
}

} // namespace WebKit
