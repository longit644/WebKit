/*
 * PlatformServiceW10M.cpp - consumer-side platform service context.
 *
 * Implements the exactly-once completion, Write-text copying and ownership
 * rules documented in PlatformServiceW10M.h. Like its neighbours this file is
 * dependency-free plain C++: nothrow allocation is the only heap use, and no
 * entry point lets an exception cross the boundary because none is ever thrown.
 */

#include "PlatformServiceW10M.h"

#include <new>
#include <string.h>

namespace WebKit {

// Cancellation result reported to a canceled operation's completion.
//
// The contract names no Cancelled operation code, so cancellation travels as
// the platform HRESULT for ERROR_CANCELLED, which is negative and therefore
// keeps its identity under the contract's HRESULT rule.
WKW10MResult canceledClipboardResult()
{
    HRESULT cancelled = HRESULT_FROM_WIN32(ERROR_CANCELLED);
    return static_cast<WKW10MResult>(static_cast<int32_t>(cancelled));
}

// See PlatformServiceW10M.h: one setup reference, no operations, no hooks.
PlatformServiceContextW10M::PlatformServiceContextW10M()
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

// See PlatformServiceW10M.h: copied text dies with the service, after the hook.
PlatformServiceContextW10M::~PlatformServiceContextW10M()
{
    if (m_destroyedHook)
        m_destroyedHook(m_destroyedHookContext);
    for (uint32_t index = 0; index < kMaxPendingOperations; ++index)
        delete[] m_operations[index].textCopy;
}

// See PlatformServiceW10M.h: the engine's ownership protocol entry point.
void PlatformServiceContextW10M::retain()
{
    ++m_refCount;
    ++m_retainCalls;
}

// See PlatformServiceW10M.h: storage is freed only by this final release.
void PlatformServiceContextW10M::release()
{
    ++m_releaseCalls;
    if (--m_refCount == 0)
        delete this;
}

// See PlatformServiceW10M.h for the counter meanings.
uint32_t PlatformServiceContextW10M::refCount() const
{
    return m_refCount;
}

// See PlatformServiceW10M.h for the counter meanings.
uint32_t PlatformServiceContextW10M::retainCalls() const
{
    return m_retainCalls;
}

// See PlatformServiceW10M.h for the counter meanings.
uint32_t PlatformServiceContextW10M::releaseCalls() const
{
    return m_releaseCalls;
}

// See PlatformServiceW10M.h: proves survival until quiescence.
void PlatformServiceContextW10M::setDestroyedHook(DestroyedHookFunction hook, void* hookContext)
{
    m_destroyedHook = hook;
    m_destroyedHookContext = hookContext;
}

// See PlatformServiceW10M.h: every pointer borrows nothing outside this object.
WKW10MResult PlatformServiceContextW10M::buildTable(WKW10MPlatformServices* services)
{
    if (!services)
        return WKW10MResultInvalidArgument;
    memset(services, 0, sizeof(*services));
    services->header.structureSize = sizeof(*services);
    services->header.structureVersion = WKW10M_STRUCTURE_VERSION;
    services->context = this;
    services->retainContext = retainThunk;
    services->releaseContext = releaseThunk;
    services->requestClipboard = requestClipboardThunk;
    services->cancelClipboard = cancelClipboardThunk;
    services->postWake = postWakeThunk;
    return WKW10MResultOK;
}

// See PlatformServiceW10M.h for the counter meanings.
uint32_t PlatformServiceContextW10M::pendingCount() const
{
    uint32_t pending = 0;
    for (uint32_t index = 0; index < kMaxPendingOperations; ++index) {
        if (m_operations[index].active)
            ++pending;
    }
    return pending;
}

// See PlatformServiceW10M.h for the counter meanings.
uint32_t PlatformServiceContextW10M::wakeCalls() const
{
    return m_wakeCalls;
}

// See PlatformServiceW10M.h: the host's own I/O finishing, delivered once.
WKW10MResult PlatformServiceContextW10M::deliverCompletion(
    uint64_t operationId, WKW10MResult result, const WKW10MString* text)
{
    if (text && text->length > 0 && !text->data)
        return WKW10MResultInvalidArgument;
    uint32_t index = findOperation(operationId);
    if (index == kMaxPendingOperations)
        return WKW10MResultInvalidArgument;
    WKW10MClipboardCompletion completion = m_operations[index].completion;
    void* completionContext = m_operations[index].completionContext;
    // Finished before invoking, so a reentrant deliver or cancel for the same
    // identifier finds nothing and cannot complete twice.
    finishOperation(index);
    completion(completionContext, operationId, result, text);
    return WKW10MResultOK;
}

// See PlatformServiceW10M.h: test support proving the Write copy outlives calls.
bool PlatformServiceContextW10M::copyOutText(
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

// See PlatformServiceW10M.h for the slot meanings.
uint32_t PlatformServiceContextW10M::findOperation(uint64_t operationId) const
{
    for (uint32_t index = 0; index < kMaxPendingOperations; ++index) {
        if (m_operations[index].active && m_operations[index].id == operationId)
            return index;
    }
    return kMaxPendingOperations;
}

// See PlatformServiceW10M.h: inactivation precedes every completion call.
void PlatformServiceContextW10M::finishOperation(uint32_t index)
{
    m_operations[index].active = false;
    delete[] m_operations[index].textCopy;
    m_operations[index].textCopy = nullptr;
    m_operations[index].textLength = 0;
}

// See PlatformServiceW10M.h: null context is ignored, never dereferenced.
void __stdcall PlatformServiceContextW10M::retainThunk(void* context)
{
    if (!context)
        return;
    static_cast<PlatformServiceContextW10M*>(context)->retain();
}

// See PlatformServiceW10M.h: null context is ignored, never dereferenced.
void __stdcall PlatformServiceContextW10M::releaseThunk(void* context)
{
    if (!context)
        return;
    static_cast<PlatformServiceContextW10M*>(context)->release();
}

// See PlatformServiceW10M.h: every rejection completes nothing, ever.
WKW10MResult __stdcall PlatformServiceContextW10M::requestClipboardThunk(void* context, WKW10MPageHandle page,
    uint64_t operationId, uint32_t kind, const WKW10MString* text, WKW10MClipboardCompletion completion,
    void* completionContext)
{
    if (!context)
        return WKW10MResultInvalidArgument;
    PlatformServiceContextW10M* self = static_cast<PlatformServiceContextW10M*>(context);
    // A request that cannot be completed exactly once must not be accepted: a
    // null completion leaves no one to call, an unknown page or kind names no
    // valid operation, and a content-bearing read is malformed.
    if (!completion || page == 0 || kind > WKW10MClipboardWrite)
        return WKW10MResultInvalidArgument;
    const uint32_t textLength = text ? text->length : 0;
    if (textLength > 0 && !text->data)
        return WKW10MResultInvalidArgument;
    if (kind == WKW10MClipboardRead && textLength > 0)
        return WKW10MResultInvalidArgument;
    if (self->findOperation(operationId) != kMaxPendingOperations)
        return WKW10MResultInvalidState;
    uint32_t slot = kMaxPendingOperations;
    for (uint32_t index = 0; index < kMaxPendingOperations; ++index) {
        if (!self->m_operations[index].active) {
            slot = index;
            break;
        }
    }
    if (slot == kMaxPendingOperations)
        return WKW10MResultBudgetExceeded;
    // Write text is copied on acceptance so the caller's buffer may die with
    // the call; a failed copy rejects the request instead of accepting text
    // this object does not own.
    uint8_t* textCopy = nullptr;
    if (kind == WKW10MClipboardWrite && textLength > 0) {
        textCopy = new (std::nothrow) uint8_t[textLength];
        if (!textCopy)
            return WKW10MResultBudgetExceeded;
        memcpy(textCopy, text->data, textLength);
    }
    self->m_operations[slot].active = true;
    self->m_operations[slot].id = operationId;
    self->m_operations[slot].kind = kind;
    self->m_operations[slot].completion = completion;
    self->m_operations[slot].completionContext = completionContext;
    self->m_operations[slot].textCopy = textCopy;
    self->m_operations[slot].textLength = textLength;
    return WKW10MResultOK;
}

// See PlatformServiceW10M.h: cancellation still completes, exactly once.
WKW10MResult __stdcall PlatformServiceContextW10M::cancelClipboardThunk(void* context, uint64_t operationId)
{
    if (!context)
        return WKW10MResultInvalidArgument;
    PlatformServiceContextW10M* self = static_cast<PlatformServiceContextW10M*>(context);
    uint32_t index = self->findOperation(operationId);
    if (index == kMaxPendingOperations)
        return WKW10MResultInvalidArgument;
    WKW10MClipboardCompletion completion = self->m_operations[index].completion;
    void* completionContext = self->m_operations[index].completionContext;
    // Finished before invoking, so the canceled completion is the operation's
    // only completion even if it reenters cancel or delivery.
    self->finishOperation(index);
    completion(completionContext, operationId, canceledClipboardResult(), nullptr);
    return WKW10MResultOK;
}

// See PlatformServiceW10M.h: records the enqueue request without touching pages.
WKW10MResult __stdcall PlatformServiceContextW10M::postWakeThunk(void* context)
{
    if (!context)
        return WKW10MResultInvalidArgument;
    ++static_cast<PlatformServiceContextW10M*>(context)->m_wakeCalls;
    return WKW10MResultOK;
}

} // namespace WebKit

