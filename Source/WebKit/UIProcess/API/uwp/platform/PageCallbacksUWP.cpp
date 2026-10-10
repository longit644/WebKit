#include "PageCallbacksUWP.h"
#include "WebKitWebView.h"

#include <new>
#include <memory>

namespace WebKit {
namespace {

// Keep a callback context valid even if Close/engine detach releases other owners.
struct CallbackDispatchLifetimeUWP {
    // Context protected until this callback's final stack unwind.
    PageCallbacksUWP* context;
    // Retain before resolving/dispatching the weak target.
    explicit CallbackDispatchLifetimeUWP(PageCallbacksUWP* value) : context(value) { context->retain(); }
    // Release only after policy/notification dispatch fully unwinds.
    ~CallbackDispatchLifetimeUWP() { context->release(); }
};

} // namespace

// Bind a callback attachment to one weak real control and its current UI thread.
PageCallbacksUWP::PageCallbacksUWP(WebKitWebView^ target)
    : m_target(target)
    , m_ownerThread(GetCurrentThreadId())
    , m_dispatcher(target ? target->Dispatcher : nullptr)
{
    if (!target)
        throw ref new Platform::InvalidArgumentException();
}

// Weak/native fields can be released without foreign-thread XAML teardown.
PageCallbacksUWP::~PageCallbacksUWP() = default;

// Fill only the frozen V1 extent; the provider retains context when attaching it.
void PageCallbacksUWP::buildTable(WKUWPPageCallbacks* output) const
{
    *output = { { sizeof(WKUWPPageCallbacks), WKUWP_STRUCTURE_VERSION },
        const_cast<PageCallbacksUWP*>(this), &retainThunk, &releaseThunk, &startingThunk, &notifyThunk };
}

// Suppress late callbacks while preserving engine acknowledgement ownership.
void PageCallbacksUWP::deactivate() { m_active.store(false); }

// Acquire an independent engine/setup/dispatch reference.
void PageCallbacksUWP::retain() { m_references.fetch_add(1); }

// Destroy context storage only after all ownership domains release it.
void PageCallbacksUWP::release()
{
    if (m_references.fetch_sub(1) == 1)
        delete this;
}

// Tolerate null retain inputs without touching foreign memory.
void __stdcall PageCallbacksUWP::retainThunk(void* context)
{
    if (!context) return;
    auto self = static_cast<PageCallbacksUWP*>(context);
    self->retain();
    self->m_engineReferences.fetch_add(1);
}

// Tolerate null release inputs and acknowledge only native context ownership.
void __stdcall PageCallbacksUWP::releaseThunk(void* context)
{
    if (!context) return;
    auto self = static_cast<PageCallbacksUWP*>(context);
    if (self->m_engineReferences.fetch_sub(1) == 1)
        self->notifyReleased();
    self->release();
}

// Marshal cleanup acknowledgement to the owner while retaining native/weak state only.
void PageCallbacksUWP::notifyReleased()
{
    try {
        if (GetCurrentThreadId() == m_ownerThread) {
            auto target = m_target.Resolve<WebKitWebView>();
            if (target) target->PageCallbacksReleased();
            return;
        }
        retain();
        auto lifetime = std::shared_ptr<PageCallbacksUWP>(this, [](PageCallbacksUWP* value) { value->release(); });
        m_dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal,
            ref new Windows::UI::Core::DispatchedHandler([lifetime] {
                auto target = lifetime->m_target.Resolve<WebKitWebView>();
                if (target) target->PageCallbacksReleased();
            }));
    } catch (...) { m_lastResult.store(WKUWPResultDispatcherUnavailable); }
}

// Enforce owner-thread policy, preserve originating handler HRESULT and fail closed.
WKUWPResult __stdcall PageCallbacksUWP::startingThunk(void* context, const WKUWPPageEvent* event, uint32_t* cancel)
{
    if (!context || !event || !cancel)
        return WKUWPResultInvalidArgument;
    auto self = static_cast<PageCallbacksUWP*>(context);
    CallbackDispatchLifetimeUWP lifetime(self);
    *cancel = 1;
    if (GetCurrentThreadId() != self->m_ownerThread) {
        self->m_lastResult.store(RPC_E_WRONG_THREAD);
        return static_cast<WKUWPResult>(RPC_E_WRONG_THREAD);
    }
    if (!self->m_active.load())
        return WKUWPResultOK;
    try {
        auto target = self->m_target.Resolve<WebKitWebView>();
        if (!target)
            return WKUWPResultOK;
        auto result = target->HandleStarting(*event, cancel);
        self->m_lastResult.store(result);
        return result;
    } catch (Platform::Exception^ exception) {
        self->m_lastResult.store(exception->HResult);
        return static_cast<WKUWPResult>(exception->HResult);
    } catch (const std::bad_alloc&) {
        self->m_lastResult.store(E_OUTOFMEMORY);
        return static_cast<WKUWPResult>(E_OUTOFMEMORY);
    } catch (...) {
        self->m_lastResult.store(E_FAIL);
        return static_cast<WKUWPResult>(E_FAIL);
    }
}

// Deliver only on the owner thread; payload copying/transactions live in the target.
void __stdcall PageCallbacksUWP::notifyThunk(void* context, const WKUWPPageEvent* event)
{
    if (!context || !event)
        return;
    auto self = static_cast<PageCallbacksUWP*>(context);
    CallbackDispatchLifetimeUWP lifetime(self);
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
void PageCallbacksOwnerDeleterUWP::operator()(PageCallbacksUWP* context) const
{
    if (!context) return;
    context->deactivate();
    context->release();
}

} // namespace WebKit
