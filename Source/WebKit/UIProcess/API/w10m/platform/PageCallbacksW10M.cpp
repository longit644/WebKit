#include "PageCallbacksW10M.h"
#include "WebKitWebView.h"

#include <new>
#include <memory>

namespace WebKit {
namespace {

// Keep a callback context valid even if Close/engine detach releases other owners.
struct CallbackDispatchLifetimeW10M {
    // Context protected until this callback's final stack unwind.
    PageCallbacksW10M* context;
    // Retain before resolving/dispatching the weak target.
    explicit CallbackDispatchLifetimeW10M(PageCallbacksW10M* value) : context(value) { context->retain(); }
    // Release only after policy/notification dispatch fully unwinds.
    ~CallbackDispatchLifetimeW10M() { context->release(); }
};

} // namespace

// Bind a callback attachment to one weak real control and its current UI thread.
PageCallbacksW10M::PageCallbacksW10M(WebKitWebView^ target)
    : m_target(target)
    , m_ownerThread(GetCurrentThreadId())
    , m_dispatcher(target ? target->Dispatcher : nullptr)
{
    if (!target)
        throw ref new Platform::InvalidArgumentException();
}

// Weak/native fields can be released without foreign-thread XAML teardown.
PageCallbacksW10M::~PageCallbacksW10M() = default;

// Fill only the frozen V1 extent; the provider retains context when attaching it.
void PageCallbacksW10M::buildTable(WKW10MPageCallbacks* output) const
{
    *output = { { sizeof(WKW10MPageCallbacks), WKW10M_STRUCTURE_VERSION },
        const_cast<PageCallbacksW10M*>(this), &retainThunk, &releaseThunk, &startingThunk, &notifyThunk };
}

// Suppress late callbacks while preserving engine acknowledgement ownership.
void PageCallbacksW10M::deactivate() { m_active.store(false); }

// Acquire an independent engine/setup/dispatch reference.
void PageCallbacksW10M::retain() { m_references.fetch_add(1); }

// Destroy context storage only after all ownership domains release it.
void PageCallbacksW10M::release()
{
    if (m_references.fetch_sub(1) == 1)
        delete this;
}

// Tolerate null retain inputs without touching foreign memory.
void __stdcall PageCallbacksW10M::retainThunk(void* context)
{
    if (!context) return;
    auto self = static_cast<PageCallbacksW10M*>(context);
    self->retain();
    self->m_engineReferences.fetch_add(1);
}

// Tolerate null release inputs and acknowledge only native context ownership.
void __stdcall PageCallbacksW10M::releaseThunk(void* context)
{
    if (!context) return;
    auto self = static_cast<PageCallbacksW10M*>(context);
    if (self->m_engineReferences.fetch_sub(1) == 1)
        self->notifyReleased();
    self->release();
}

// Marshal cleanup acknowledgement to the owner while retaining native/weak state only.
void PageCallbacksW10M::notifyReleased()
{
    try {
        if (GetCurrentThreadId() == m_ownerThread) {
            auto target = m_target.Resolve<WebKitWebView>();
            if (target) target->PageCallbacksReleased();
            return;
        }
        retain();
        auto lifetime = std::shared_ptr<PageCallbacksW10M>(this, [](PageCallbacksW10M* value) { value->release(); });
        m_dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal,
            ref new Windows::UI::Core::DispatchedHandler([lifetime] {
                auto target = lifetime->m_target.Resolve<WebKitWebView>();
                if (target) target->PageCallbacksReleased();
            }));
    } catch (...) { m_lastResult.store(WKW10MResultDispatcherUnavailable); }
}

// Enforce owner-thread policy, preserve originating handler HRESULT and fail closed.
WKW10MResult __stdcall PageCallbacksW10M::startingThunk(void* context, const WKW10MPageEvent* event, uint32_t* cancel)
{
    if (!context || !event || !cancel)
        return WKW10MResultInvalidArgument;
    auto self = static_cast<PageCallbacksW10M*>(context);
    CallbackDispatchLifetimeW10M lifetime(self);
    *cancel = 1;
    if (GetCurrentThreadId() != self->m_ownerThread) {
        self->m_lastResult.store(RPC_E_WRONG_THREAD);
        return static_cast<WKW10MResult>(RPC_E_WRONG_THREAD);
    }
    if (!self->m_active.load())
        return WKW10MResultOK;
    try {
        auto target = self->m_target.Resolve<WebKitWebView>();
        if (!target)
            return WKW10MResultOK;
        auto result = target->HandleStarting(*event, cancel);
        self->m_lastResult.store(result);
        return result;
    } catch (Platform::Exception^ exception) {
        self->m_lastResult.store(exception->HResult);
        return static_cast<WKW10MResult>(exception->HResult);
    } catch (const std::bad_alloc&) {
        self->m_lastResult.store(E_OUTOFMEMORY);
        return static_cast<WKW10MResult>(E_OUTOFMEMORY);
    } catch (...) {
        self->m_lastResult.store(E_FAIL);
        return static_cast<WKW10MResult>(E_FAIL);
    }
}

// Deliver only on the owner thread; payload copying/transactions live in the target.
void __stdcall PageCallbacksW10M::notifyThunk(void* context, const WKW10MPageEvent* event)
{
    if (!context || !event)
        return;
    auto self = static_cast<PageCallbacksW10M*>(context);
    CallbackDispatchLifetimeW10M lifetime(self);
    if (GetCurrentThreadId() != self->m_ownerThread) {
        self->m_lastResult.store(RPC_E_WRONG_THREAD);
        return;
    }
    if (!self->m_active.load())
        return;
    try {
        auto target = self->m_target.Resolve<WebKitWebView>();
        if (target)
            self->m_lastResult.store(target->HandleNotification(*event));
    } catch (Platform::Exception^ exception) { self->m_lastResult.store(exception->HResult); }
    catch (const std::bad_alloc&) { self->m_lastResult.store(E_OUTOFMEMORY); }
    catch (...) { self->m_lastResult.store(E_FAIL); }
}

// Drop setup ownership while retained engine/dispatch contexts stay safely inactive.
void PageCallbacksOwnerDeleterW10M::operator()(PageCallbacksW10M* context) const
{
    if (!context) return;
    context->deactivate();
    context->release();
}

} // namespace WebKit

