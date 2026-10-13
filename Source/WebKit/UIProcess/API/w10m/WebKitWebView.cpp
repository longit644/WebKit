#include "WebKitWebView.h"
#include "platform/FrameSchedulerDriverW10M.h"
#include "rendering/ViewportExtentW10M.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <new>
#include <utility>

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Data;
using namespace Windows::UI::Xaml::Interop;

namespace WebKit {
namespace {

// Publication kind that raises property changes without inventing an engine event.
constexpr unsigned int stateOnly = std::numeric_limits<unsigned int>::max();

// Hidden process-unique control identities; local page/navigation IDs are not capabilities.
std::atomic<unsigned long long> nextControlIdentity { 0 };

// Reserve a nonzero issuer identity without wrapping into reuse.
unsigned long long allocateControlIdentity()
{
    auto current = nextControlIdentity.load();
    do {
        if (current == std::numeric_limits<unsigned long long>::max())
            throw Exception::CreateException(HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW));
    } while (!nextControlIdentity.compare_exchange_weak(current, current + 1));
    return current + 1;
}

// Copy a WinRT string with its explicit UTF16 length; null becomes empty.
std::wstring wide(String^ value)
{
    return value ? std::wstring(value->Data(), value->Length()) : std::wstring();
}

// Create an immutable WinRT string from component-owned UTF16 storage.
String^ text(const std::wstring& value)
{
    return ref new String(value.data(), static_cast<unsigned int>(value.size()));
}

// Construct a URI payload; the empty native Source convention maps to null.
Uri^ uri(const std::wstring& value)
{
    return value.empty() ? nullptr : ref new Uri(text(value));
}

// Copy/strictly decode a borrowed V1 UTF8 span before public dispatch.
std::wstring decode(const WKW10MString& span)
{
    if (!span.length)
        return { };
    if (!span.data || span.length > static_cast<uint32_t>(std::numeric_limits<int>::max()))
        throw ref new InvalidArgumentException();
    auto bytes = reinterpret_cast<const char*>(span.data);
    auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, static_cast<int>(span.length), nullptr, 0);
    if (!count)
        throw ref new InvalidArgumentException(L"Malformed UTF8 event span");
    std::wstring output(count, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, static_cast<int>(span.length), &output[0], count))
        throw ref new InvalidArgumentException();
    return output;
}

// Encode an immutable command target for a borrowed synchronous controller span.
std::string encode(const std::wstring& value)
{
    if (value.empty())
        return { };
    auto count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (!count)
        throw ref new InvalidArgumentException();
    std::string output(count, '\0');
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), &output[0], count, nullptr, nullptr))
        throw ref new InvalidArgumentException();
    return output;
}

// Reject malformed headers/enums/flags/spans before any candidate-state mutation.
void validateEvent(const WKW10MPageEvent& event)
{
    if (event.header.structureVersion != WKW10M_STRUCTURE_VERSION || event.header.structureSize < sizeof(event)
        || event.eventKind > WKW10MEventRepaintRequested || event.contentKind > WKW10MContentHostHtml
        || event.outcome > WKW10MOutcomeCancelled || event.errorDomain > WKW10MErrorDomainPlatform
        || event.changeKind > WKW10MChangeHistoryTraversal || event.canGoBack > 1 || event.canGoForward > 1
        || event.isLoading > 1 || event.hasHttpStatus > 1
        || (event.uri.length && !event.uri.data) || (event.title.length && !event.title.data))
        throw ref new InvalidArgumentException(L"Malformed structured page event");
    if (!!event.certificateToken.issuer != !!event.certificateToken.decision
        || ((event.certificateToken.issuer || event.certificateToken.decision)
            && (event.eventKind != WKW10MEventCompleted || event.outcome != WKW10MOutcomeFailed
                || event.contentKind != WKW10MContentRemote || event.errorDomain != WKW10MErrorDomainTransport))
        || (event.contentKind == WKW10MContentHostHtml && (event.hasHttpStatus || event.httpStatus)))
        throw ref new InvalidArgumentException(L"Malformed status/certificate eligibility");
}

// Map the validated native origin policy explicitly into the public enum.
WebKitNavigationContentKind contentKind(uint32_t value)
{
    return value == WKW10MContentHostHtml ? WebKitNavigationContentKind::HostHtml : WebKitNavigationContentKind::Remote;
}

// Map native terminal classification without depending on unrelated error-code values.
WebKitNavigationOutcome outcome(uint32_t value)
{
    switch (value) {
    case WKW10MOutcomeCompleted: return WebKitNavigationOutcome::Completed;
    case WKW10MOutcomeFailed: return WebKitNavigationOutcome::Failed;
    default: return WebKitNavigationOutcome::Cancelled;
    }
}

// Map the validated error namespace while retaining its original numeric code.
WebKitNavigationErrorDomain errorDomain(uint32_t value)
{
    switch (value) {
    case WKW10MErrorDomainTransport: return WebKitNavigationErrorDomain::Transport;
    case WKW10MErrorDomainNetwork: return WebKitNavigationErrorDomain::Network;
    case WKW10MErrorDomainEngine: return WebKitNavigationErrorDomain::Engine;
    case WKW10MErrorDomainPlatform: return WebKitNavigationErrorDomain::Platform;
    default: return WebKitNavigationErrorDomain::None;
    }
}

// Map engine commit/same-document attribution independently of document generation.
WebKitSourceChangeKind changeKind(uint32_t value)
{
    switch (value) {
    case WKW10MChangeFragment: return WebKitSourceChangeKind::Fragment;
    case WKW10MChangePushState: return WebKitSourceChangeKind::PushState;
    case WKW10MChangeReplaceState: return WebKitSourceChangeKind::ReplaceState;
    case WKW10MChangeHistoryTraversal: return WebKitSourceChangeKind::HistoryTraversal;
    default: return WebKitSourceChangeKind::NewDocument;
    }
}

} // namespace

// Runtime Binding needs actual accessors when there is no generated app type
// provider. The accessor holds no control reference and cannot form a cycle.
ref class WebViewPropertyAccessorW10M sealed : ICustomProperty {
public:
    // Report the property's projected URI/String/Boolean type to runtime Binding.
    virtual property TypeName Type {
        // Return the SDK type matching the accessor's stored property name.
        TypeName get() { return m_name == L"Source" ? Uri::typeid : m_name == L"DocumentTitle" ? String::typeid : bool::typeid; }
    }

    // Return the exact observable property name this accessor serves.
    virtual property String^ Name { String^ get() { return m_name; } }
    // Every supported observable property has an owner-thread getter.
    virtual property bool CanRead { bool get() { return true; } }
    // Only Source is writable; loading/history/title are read-only snapshots.
    virtual property bool CanWrite { bool get() { return m_name == L"Source"; } }
    // Read the real public getter, preserving dispatcher and frozen-snapshot semantics.
    virtual Object^ GetValue(Object^ target)
    {
        auto view = safe_cast<WebKitWebView^>(target);
        if (m_name == L"Source") return view->Source;
        if (m_name == L"DocumentTitle") return view->DocumentTitle;
        if (m_name == L"CanGoBack") return view->CanGoBack;
        if (m_name == L"CanGoForward") return view->CanGoForward;
        return view->IsLoading;
    }

    // Route writable Source through the validated/deferred setter; reject read-only writes.
    virtual void SetValue(Object^ target, Object^ value)
    {
        if (!CanWrite)
            throw ref new InvalidArgumentException(L"Read-only state property");
        safe_cast<WebKitWebView^>(target)->Source = safe_cast<Uri^>(value);
    }

    // No indexed observable properties are exposed by this scalar control contract.
    virtual Object^ GetIndexedValue(Object^, Object^) { throw ref new NotImplementedException(); }
    // Indexed writes are unsupported for the scalar state contract.
    virtual void SetIndexedValue(Object^, Object^, Object^) { throw ref new NotImplementedException(); }
internal:
    // Retain only the immutable property name, never a strong control reference.
    WebViewPropertyAccessorW10M(String^ name) : m_name(name) { }
private:
    // Name used to select the public getter and its projected type.
    String^ m_name;
};

// Process-shared DP identity; all observable/page state remains per instance.
DependencyProperty^ WebKitWebView::s_sourceProperty = nullptr;

// Construct the real XAML control with copied containing-package default options.
WebKitWebView::WebKitWebView()
    : m_controlIdentity(allocateControlIdentity())
    , m_configuration(std::make_unique<WebViewConfigurationW10M>(WebViewOptionsW10M { }))
{
    CheckThread();
    SourceProperty;
    EnsurePanel();
}

// Construct a real XAML control with an immutable copy of application configuration.
WebKitWebView::WebKitWebView(WebKitWebViewOptions^ options)
    : m_controlIdentity(allocateControlIdentity())
    , m_configuration(std::make_unique<WebViewConfigurationW10M>(WebKitWebViewOptions::CopyToNative(options)))
{
    CheckThread();
    SourceProperty;
    EnsurePanel();
}

// Report RPC_E_WRONG_THREAD before touching this control's owner-thread state.
void WebKitWebView::CheckThread()
{
    if (!Dispatcher->HasThreadAccess)
        throw ref new WrongThreadException();
}

// Enforce dispatcher affinity and RO_E_CLOSED mutation semantics during/after Close.
void WebKitWebView::CheckOpen()
{
    CheckThread();
    if (m_closing || m_closed)
        throw ref new ObjectDisposedException();
}

// Lazily register the URI DP and its nonrecursive validation/publication callback.
DependencyProperty^ WebKitWebView::SourceProperty::get()
{
    if (!s_sourceProperty)
        s_sourceProperty = DependencyProperty::Register(L"Source", Uri::typeid, WebKitWebView::typeid,
            ref new PropertyMetadata(nullptr, ref new PropertyChangedCallback(&WebKitWebView::SourcePropertyChanged)));
    return s_sourceProperty;
}

// Return the published transaction URI rather than a temporary DP assignment.
Uri^ WebKitWebView::Source::get()
{
    CheckThread();
    return m_source;
}

// Validate/enqueue Source even when a reentrant request equals the frozen DP value.
void WebKitWebView::Source::set(Uri^ value)
{
    CheckOpen();
    SubmitSource(value);
}

// Install or restore the published URI under a nesting-safe internal-update guard.
void WebKitWebView::WriteSourceProperty()
{
    auto previous = m_updatingSource;
    m_updatingSource = true;
    try {
        SetValue(SourceProperty, m_source);
    } catch (...) {
        m_updatingSource = previous;
        throw;
    }
    m_updatingSource = previous;
}

// Roll back generic DP assignment before validating and queuing its copied intent.
void WebKitWebView::SourcePropertyChanged(DependencyObject^ sender, DependencyPropertyChangedEventArgs^ args)
{
    auto control = safe_cast<WebKitWebView^>(sender);
    if (control->m_updatingSource)
        return;
    // A generic DP/XAML assignment has already changed its value. Restore the
    // currently published snapshot before validation, enqueue or notifications.
    auto requested = args->NewValue;
    control->WriteSourceProperty();
    control->CheckOpen();
    Uri^ value = nullptr;
    try {
        value = safe_cast<Uri^>(requested);
    } catch (InvalidCastException^) {
        throw ref new InvalidArgumentException(L"Source must be a Uri");
    }
    control->SubmitSource(value);
}

// Accept null or absolute HTTP(S) targets with a host, preserving canonical spelling.
std::wstring WebKitWebView::ValidateSource(Uri^ value)
{
    if (!value)
        return { };
    if ((value->SchemeName != L"http" && value->SchemeName != L"https") || value->Host->IsEmpty())
        throw ref new InvalidArgumentException(L"Source requires an absolute HTTP(S) URI");
    auto result = wide(value->AbsoluteUri);
    if (result.empty() || result.find(L'\0') != std::wstring::npos)
        throw ref new InvalidArgumentException();
    return result;
}

// Enqueue validated Source FIFO, preserving active snapshots and policy interruption.
void WebKitWebView::SubmitSource(Uri^ value)
{
    CheckOpen();
    auto requested = ValidateSource(value);
    if (!requested.empty() && requested == m_state.snapshot().source && m_sourceCommands.empty())
        return;
    DeferredSource command { std::move(requested), DeferredOperationW10M::Source, 0 };
    command.deferred = m_notificationDepth || m_nativeCallDepth || m_draining || m_awaitingPolicyTerminal;
    m_sourceCommands.push_back(std::move(command));
    if (m_state.navigationPhase() == NavigationPhaseW10M::Starting)
        m_policyInterrupted = true;
    DrainCommands();
}

// Read the transaction's previous-neighbor availability on the owning dispatcher.
bool WebKitWebView::CanGoBack::get() { CheckThread(); return m_state.snapshot().canGoBack; }
// Read the transaction's next-neighbor availability on the owning dispatcher.
bool WebKitWebView::CanGoForward::get() { CheckThread(); return m_state.snapshot().canGoForward; }
// Read the active-main snapshot, including the stable final value after Close.
bool WebKitWebView::IsLoading::get() { CheckThread(); return m_state.snapshot().isLoading; }
// Return the immutable copied title, never a mutable/borrowed engine buffer.
String^ WebKitWebView::DocumentTitle::get() { CheckThread(); return m_title; }

// Supply real runtime Binding accessors with read-only history/loading/title policy.
ICustomProperty^ WebKitWebView::GetCustomProperty(String^ name)
{
    CheckThread();
    return name == L"Source" || name == L"DocumentTitle" || name == L"CanGoBack" || name == L"CanGoForward" || name == L"IsLoading"
        ? ref new WebViewPropertyAccessorW10M(name) : nullptr;
}

// Report no indexed properties for the scalar public state contract.
ICustomProperty^ WebKitWebView::GetIndexedProperty(String^, TypeName) { CheckThread(); return nullptr; }
// Return a stable class representation for runtime property discovery.
String^ WebKitWebView::GetStringRepresentation() { CheckThread(); return L"WebKit.WebKitWebView"; }
// Identify the actual authored public runtime class to the custom-property provider.
TypeName WebKitWebView::Type::get() { CheckThread(); return WebKitWebView::typeid; }

// Register one property observer, preserving owner-thread/closed mutation errors.
EventRegistrationToken WebKitWebView::PropertyChanged::add(PropertyChangedEventHandler^ handler)
{
    CheckOpen();
    return m_propertyChangedHandlers.add(handler);
}

// Remove a property subscription safely even after its roots were revoked by Close.
void WebKitWebView::PropertyChanged::remove(EventRegistrationToken token) { CheckThread(); m_propertyChangedHandlers.remove(token); }

// Register one policy handler in this control's independent event registry.
EventRegistrationToken WebKitWebView::NavigationStarting::add(TypedEventHandler<WebKitWebView^, WebKitNavigationStartingEventArgs^>^ handler)
{
    CheckOpen();
    return m_startingHandlers.add(handler);
}

// Remove a policy subscription without altering an in-progress invocation snapshot.
void WebKitWebView::NavigationStarting::remove(EventRegistrationToken token) { CheckThread(); m_startingHandlers.remove(token); }

// Register one actual committed-URI observer while the control is live.
EventRegistrationToken WebKitWebView::SourceChanged::add(TypedEventHandler<WebKitWebView^, WebKitSourceChangedEventArgs^>^ handler)
{
    CheckOpen();
    return m_sourceChangedHandlers.add(handler);
}

// Remove one URI-change subscription on the owner dispatcher.
void WebKitWebView::SourceChanged::remove(EventRegistrationToken token) { CheckThread(); m_sourceChangedHandlers.remove(token); }

// Register one copied-history observer while the control is live.
EventRegistrationToken WebKitWebView::HistoryChanged::add(TypedEventHandler<WebKitWebView^, WebKitHistoryChangedEventArgs^>^ handler)
{
    CheckOpen();
    return m_historyHandlers.add(handler);
}

// Remove one history subscription on the owner dispatcher.
void WebKitWebView::HistoryChanged::remove(EventRegistrationToken token) { CheckThread(); m_historyHandlers.remove(token); }

// Register one copied-title observer while the control is live.
EventRegistrationToken WebKitWebView::DocumentTitleChanged::add(TypedEventHandler<WebKitWebView^, WebKitDocumentTitleChangedEventArgs^>^ handler)
{
    CheckOpen();
    return m_titleHandlers.add(handler);
}

// Remove one document-title subscription on the owner dispatcher.
void WebKitWebView::DocumentTitleChanged::remove(EventRegistrationToken token) { CheckThread(); m_titleHandlers.remove(token); }

// Register one terminal observer before the control begins closing.
EventRegistrationToken WebKitWebView::NavigationCompleted::add(TypedEventHandler<WebKitWebView^, WebKitNavigationCompletedEventArgs^>^ handler)
{
    CheckOpen();
    return m_completedHandlers.add(handler);
}

// Remove a terminal subscription without losing another already-created owed delivery.
void WebKitWebView::NavigationCompleted::remove(EventRegistrationToken token) { CheckThread(); m_completedHandlers.remove(token); }

// Invoke one property notification; subscriber errors are diagnostic, not outcomes.
void WebKitWebView::NotifyProperty(String^ name)
{
    try {
        m_propertyChangedHandlers.invoke(this, ref new PropertyChangedEventArgs(name));
    } catch (...) {
        ++m_handlerFailures;
    }
}

// Prepare/install one immutable snapshot, then finish properties and its owed event.
void WebKitWebView::PublishState(NavigationStateW10M&& next, unsigned int kind, Object^ payload,
    std::vector<Agile<InitializationActionW10M^>>* resolvingActions, HRESULT initializationResult)
{
    auto before = m_state.snapshot();
    // Prepare allocations before installing any part of the new snapshot.
    auto publishedUri = next.snapshot().source == before.source ? m_source : uri(next.snapshot().source);
    auto publishedTitle = next.snapshot().title == before.title ? m_title : text(next.snapshot().title);
    WebKitWebView^ dispatchLifetime = this;
    m_state = std::move(next);
    m_source = publishedUri;
    m_title = publishedTitle;
    ++m_notificationDepth;
    try {
        // Readiness/failure is installed before all observer states are frozen;
        // no property or completion subscriber runs between these steps.
        if (resolvingActions) {
            for (auto& action : *resolvingActions)
                action.Get()->Resolve(initializationResult, false);
        }
        WriteSourceProperty();
        if (before.source != m_state.snapshot().source) NotifyProperty(L"Source");
        if (before.canGoBack != m_state.snapshot().canGoBack) NotifyProperty(L"CanGoBack");
        if (before.canGoForward != m_state.snapshot().canGoForward) NotifyProperty(L"CanGoForward");
        if (before.isLoading != m_state.snapshot().isLoading) NotifyProperty(L"IsLoading");
        if (before.title != m_state.snapshot().title) NotifyProperty(L"DocumentTitle");
        try {
            switch (kind) {
            case WKW10MEventSourceChanged: m_sourceChangedHandlers.invoke(this, safe_cast<WebKitSourceChangedEventArgs^>(payload)); break;
            case WKW10MEventHistoryChanged: m_historyHandlers.invoke(this, safe_cast<WebKitHistoryChangedEventArgs^>(payload)); break;
            case WKW10MEventTitleChanged: m_titleHandlers.invoke(this, safe_cast<WebKitDocumentTitleChangedEventArgs^>(payload)); break;
            case WKW10MEventCompleted: m_completedHandlers.invoke(this, safe_cast<WebKitNavigationCompletedEventArgs^>(payload)); break;
            default: break;
            }
        } catch (...) {
            ++m_handlerFailures;
        }
    } catch (...) {
        --m_notificationDepth;
        throw;
    }
    --m_notificationDepth;
    (void)dispatchLifetime;
}

// Bind an instance-owned controller port whose context survives call/dispatch unwind.
void WebKitWebView::SetCommandHandler(SourceCommandHandlerW10M handler, void* context)
{
    CheckThread();
    if ((!handler && context) || (handler && !context))
        throw ref new InvalidArgumentException();
    m_commandHandler = handler;
    m_commandContext = context;
}

// Attach an actual negotiated page table before publishing readiness or accepting work.
HRESULT WebKitWebView::BindPageController(const WKW10MEngineAPI* api, unsigned long long page)
{
    try {
        CheckOpen();
        if (m_pageController || !page || (m_state.pageId() && m_state.pageId() != page))
            return E_INVALIDARG;
        WKW10MPageCallbacks callbacks { };
        auto attached = BuildPageCallbacks(&callbacks);
        if (FAILED(attached)) return attached;
        auto controller = std::make_unique<PageControllerW10M>();
        attached = controller->bind(api, page, &callbacks, m_engineModule.get());
        if (FAILED(attached)) return attached;
        if (api->service) {
            attached = AttachFrameService(api);
            if (FAILED(attached)) { controller->close(); return attached; }
        }
        m_pageController = std::move(controller);
        // Presentation is best-effort: readiness does not depend on a laid-out visual.
        // SizeChanged and frame callbacks retry until the panel geometry arrives.
        EnsurePresentation();
        return S_OK;
    } catch (Exception^ exception) { return exception->HResult; }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_FAIL; }
}

// Bind a live service lease without conflating visual unload with page/session shutdown.
HRESULT WebKitWebView::AttachFrameService(const WKW10MEngineAPI* api)
{
    try {
        CheckOpen();
        auto driver = FrameSchedulerDriverW10M::Acquire(api, Dispatcher);
        auto scheduler = std::make_shared<FrameSchedulerW10M>();
        Platform::WeakReference weak(this);
        auto token = driver->AddClient(scheduler, [weak](int64_t epoch) {
            auto view = weak.Resolve<WebKitWebView>();
            if (!view || view->m_closing)
                return S_FALSE;
            return view->RenderPresentation(epoch);
        });
        m_frameDriver = driver;
        m_frameScheduler = std::move(scheduler);
        m_frameClient = token;
        m_frameScheduler->setPresentationEnabled(m_windowVisible && !!(api->capabilities & WKW10MCapabilityGPUCompositor));
        return S_OK;
    } catch (Platform::Exception^ exception) { return exception->HResult; }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_FAIL; }
}

// Create the visible SwapChainPanel once and hook layout retries.
void WebKitWebView::EnsurePanel()
{
    CheckThread();
    if (m_panel)
        return;
    if (!m_presenter)
        m_presenter = std::make_unique<SwapChainPresenterW10M>();
    auto panel = ref new Windows::UI::Xaml::Controls::SwapChainPanel();
    panel->HorizontalAlignment = Windows::UI::Xaml::HorizontalAlignment::Stretch;
    panel->VerticalAlignment = Windows::UI::Xaml::VerticalAlignment::Stretch;
    Content = panel;
    m_panel = panel;
    SizeChanged += ref new Windows::UI::Xaml::SizeChangedEventHandler(this, &WebKitWebView::OnControlSizeChanged);
    m_presentationWindow = Window::Current;
    if (m_presentationWindow) {
        WeakReference weak(this);
        m_windowVisible = m_presentationWindow->Visible;
        m_windowVisibilityToken = m_presentationWindow->VisibilityChanged += ref new WindowVisibilityChangedEventHandler(
            [weak](Object^, Windows::UI::Core::VisibilityChangedEventArgs^ args) {
                auto view = weak.Resolve<WebKitWebView>();
                if (!view || view->m_closing) return;
                view->m_windowVisible = args->Visible;
                if (args->Visible) view->m_presentationNeedsRevalidation = true;
                if (view->m_frameScheduler) view->m_frameScheduler->setPresentationEnabled(args->Visible);
            });
        m_windowActivationToken = m_presentationWindow->Activated += ref new WindowActivatedEventHandler(
            [weak](Object^, Windows::UI::Core::WindowActivatedEventArgs^ args) {
                auto view = weak.Resolve<WebKitWebView>();
                if (view && !view->m_closing && args->WindowActivationState != Windows::UI::Core::CoreWindowActivationState::Deactivated)
                    view->m_presentationNeedsRevalidation = true;
            });
        m_windowHooksInstalled = true;
    }
}

// Retry presentation after layout settles; layout itself never navigates.
void WebKitWebView::OnControlSizeChanged(Platform::Object^, Windows::UI::Xaml::SizeChangedEventArgs^)
{
    // ANGLE receives this event too. Engine work belongs to the subsequent
    // SDK frame, after all panel listeners have installed their geometry.
    if (m_frameScheduler && !m_closing)
        m_frameScheduler->setPresentationEnabled(m_windowVisible);
}

// Resize the engine page and attach the engine-owned compositor when laid out.
HRESULT WebKitWebView::EnsurePresentation()
{
    try {
        if (!Dispatcher->HasThreadAccess)
            return RPC_E_WRONG_THREAD;
        if (m_closing || m_closed || !m_pageController || !m_pageController->isBound() || !m_engineModule || !m_engineModule->api())
            return S_FALSE;
        EnsurePanel();
        if (!m_panel || !m_presenter)
            return S_FALSE;
        double actualWidth = ActualWidth;
        double actualHeight = ActualHeight;
        if (!std::isfinite(actualWidth) || !std::isfinite(actualHeight)
            || actualWidth > 2048 || actualHeight > 2048)
            return E_INVALIDARG;
        if (!(actualWidth >= 1.0) || !(actualHeight >= 1.0))
            return S_FALSE;
        unsigned width = enclosingCssViewportExtentW10M(actualWidth);
        unsigned height = enclosingCssViewportExtentW10M(actualHeight);
        if (!width || !height)
            return S_FALSE;
        float scaleX = m_panel->CompositionScaleX;
        float scaleY = m_panel->CompositionScaleY;
        if (!std::isfinite(scaleX) || !std::isfinite(scaleY) || scaleX <= 0 || scaleY <= 0)
            return E_INVALIDARG;
        double deviceX = scaleX;
        double deviceY = scaleY;
        // Steady geometry needs no engine work; resize unconditionally would
        // dirty every frame and spam the candidate trace.
        if (m_presentationAttached && width == m_presentationWidth && height == m_presentationHeight
            && deviceX == m_presentationScaleX && deviceY == m_presentationScaleY
            && !m_presentationNeedsRevalidation) {
            return S_OK;
        }

        // A scale change requires detach before resize; size-only changes do not.
        if (m_presentationAttached && (deviceX != m_presentationScaleX || deviceY != m_presentationScaleY)) {
            auto detached = m_presenter->detach();
            if (FAILED(detached))
                return detached;
            m_presentationAttached = false;
            m_presentationWidth = m_presentationHeight = 0;
            m_presentationScaleX = m_presentationScaleY = 0;
            if (m_frameScheduler)
                m_frameScheduler->setPresentationEnabled(false);
        }
        auto resized = m_pageController->resize(width, height, deviceX, deviceY);
        if (FAILED(resized))
            return resized;
        m_presentationWidth = width;
        m_presentationHeight = height;
        m_presentationScaleX = deviceX;
        m_presentationScaleY = deviceY;
        m_presentationNeedsRevalidation = false;
        if (!m_presentationAttached) {
            const auto* api = m_engineModule->api();
            auto page = m_pageController->page();
            IUnknown* rawPanel = reinterpret_cast<IUnknown*>(m_panel);
            auto attached = m_presenter->initializeForCompositor(api, page, rawPanel, deviceX, deviceY);
            if (FAILED(attached))
                return attached;
            m_presentationAttached = true;
            if (m_frameScheduler)
                m_frameScheduler->setPresentationEnabled(m_windowVisible);
        }
        return S_OK;
    } catch (Platform::Exception^ exception) { return exception->HResult; }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_FAIL; }
}

// Render one dirty engine frame; S_FALSE when nothing was eligible or dirty.
HRESULT WebKitWebView::RenderPresentation(int64_t)
{
    try {
        if (!Dispatcher->HasThreadAccess)
            return RPC_E_WRONG_THREAD;
        if (m_closing || m_closed || !m_windowVisible || !m_pageController || !m_pageController->isBound() || !m_presenter)
            return S_FALSE;
        // Protect the complete geometry/tick/paint operation from callback-driven Close.
        ++m_nativeCallDepth;
        HRESULT result = S_FALSE;
        try {
            result = EnsurePresentation();
            if (result == S_OK && m_presentationAttached && !m_closing)
                result = m_presenter->renderCompositor(0.0);
        } catch (...) {
            --m_nativeCallDepth;
            throw;
        }
        --m_nativeCallDepth;
        DrainCommands();
        return result;
    } catch (Platform::Exception^ exception) { return exception->HResult; }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_FAIL; }
}

// Detach the engine surface and release the visual before close/teardown.
HRESULT WebKitWebView::TeardownPresentation()
{
    try {
        if (!Dispatcher->HasThreadAccess)
            return RPC_E_WRONG_THREAD;
        if (m_frameScheduler)
            m_frameScheduler->setPresentationEnabled(false);
        if (m_presenter && m_presentationAttached) {
            auto detached = m_presenter->detach();
            if (detached != S_OK) return detached;
            m_presentationAttached = false;
            m_presentationWidth = m_presentationHeight = 0;
            m_presentationScaleX = m_presentationScaleY = 0;
        }
        return S_OK;
    } catch (Platform::Exception^ exception) { return exception->HResult; }
    catch (...) { return E_FAIL; }
}

// Create one retained weak-target callback attachment using the actual frozen V1 table.
HRESULT WebKitWebView::BuildPageCallbacks(WKW10MPageCallbacks* output)
{
    try {
        CheckOpen();
        if (!output)
            return E_INVALIDARG;
        if (!m_pageCallbacks)
            m_pageCallbacks.reset(new PageCallbacksW10M(this));
        m_pageCallbacks->buildTable(output);
        return S_OK;
    } catch (Exception^ exception) { return exception->HResult; }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_FAIL; }
}

// Read contained callback diagnostics without creating any browser event from text.
HRESULT WebKitWebView::LastPageCallbackResult()
{
    CheckThread();
    return m_pageCallbacks ? m_pageCallbacks->lastResult() : RO_E_CLOSED;
}

// Complete pending native ownership only after its retained attachment is released.
void WebKitWebView::PageCallbacksReleased()
{
    CheckThread();
    if (!m_closing || m_closed || !m_pageController)
        return;
    m_pageController->acknowledgeClose();
    DrainCommands();
}

// Verify exact object/control/issuer/revision identity; token presence alone is not consent.
bool WebKitWebView::IsCurrentCertificateToken(WebKitCertificateFailureToken^ token)
{
    CheckThread();
    if (m_closing || m_closed || !token || token != m_certificateToken || !token->IssuedBy(m_controlIdentity))
        return false;
    auto decision = token->EngineToken();
    return decision.issuer == m_certificateIssuer && decision.decision == m_certificateRevision;
}

// Drop current eligibility while immutable event/token objects retain diagnostic payloads.
void WebKitWebView::InvalidateCertificateToken()
{
    if (m_certificateEpoch == std::numeric_limits<uint64_t>::max())
        throw Exception::CreateException(HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW));
    ++m_certificateEpoch;
    m_certificateToken = nullptr;
}

// Execute a borrowed-span controller command while suppressing unsafe recursive drains.
void WebKitWebView::ExecuteNative(SourceActionW10M action, const std::wstring& target)
{
    if (action == SourceActionW10M::Initialize) {
        RequestInitialization();
        return;
    }
    auto bytes = encode(target);
    WKW10MString span { reinterpret_cast<const uint8_t*>(bytes.data()), static_cast<uint32_t>(bytes.size()) };
    ++m_nativeCallDepth;
    HRESULT result = S_OK;
    try {
        if (m_pageController) {
            uint64_t navigation = 0;
            switch (action) {
            case SourceActionW10M::Navigate: result = m_pageController->navigate(span, &navigation); break;
            case SourceActionW10M::Stop: result = m_pageController->stop(); break;
            case SourceActionW10M::Close:
                result = TeardownPresentation();
                if (result == S_OK) result = m_pageController->close();
                break;
            default: result = E_INVALIDARG; break;
            }
        } else if (m_commandHandler)
            result = m_commandHandler(m_commandContext, action, &span);
        else if (action == SourceActionW10M::Navigate || action == SourceActionW10M::Stop)
            result = E_NOTIMPL;
    } catch (Exception^ exception) {
        result = exception->HResult;
    } catch (const std::bad_alloc&) {
        result = E_OUTOFMEMORY;
    } catch (...) {
        result = E_FAIL;
    }
    --m_nativeCallDepth;
    if (FAILED(result)) {
        throw Exception::CreateException(result);
    }
}

// Revalidate a FIFO intent, publish its requested/restored state, then call native work.
void WebKitWebView::ExecuteSource(const std::wstring& requested)
{
    CheckOpen();
    // Revalidate FIFO commands against current lifetime and URI state.
    auto target = ValidateSource(uri(requested));
    auto next = m_state;
    auto previousSource = next.snapshot().source;
    auto admittedBefore = next.highestNavigationId();
    auto action = next.assignSource(target);
    if (target.empty() || target != previousSource) {
        AdvanceDocumentIntent();
        InvalidateCertificateToken();
    }
    if (action == SourceActionW10M::Initialize)
        BeginInitializationAttempt();
    PublishState(std::move(next), stateOnly, nullptr);
    if (!m_closing && action != SourceActionW10M::None) {
        try { ExecuteNative(action, target); }
        catch (...) {
            // Pre-admission failure has no started ID/terminal obligation; restore
            // the exact prior requested snapshot without losing a live older load.
            if (!m_closing && m_state.highestNavigationId() == admittedBefore) {
                auto restored = m_state;
                restored.restoreRequestedSource(previousSource);
                PublishState(std::move(restored), stateOnly, nullptr);
            }
            throw;
        }
    }
}

// Admit only the latest readiness intent; rejection restores the committed document.
void WebKitWebView::ExecuteInitialSource(unsigned long long intent)
{
    auto pending = m_state.takeInitialSource();
    if (pending.empty() || intent != m_documentIntent)
        return;
    auto admittedBefore = m_state.highestNavigationId();
    try { ExecuteNative(SourceActionW10M::Navigate, pending); }
    catch (...) {
        if (!m_closing && m_state.highestNavigationId() == admittedBefore) {
            auto restored = m_state;
            restored.assignSource({ });
            PublishState(std::move(restored), stateOnly, nullptr);
        }
        throw;
    }
}

// Finish copied native signals/owed terminals before revalidated FIFO commands and Close.
void WebKitWebView::DrainCommands()
{
    if (m_draining || m_notificationDepth || m_nativeCallDepth || m_closed)
        return;
    WebKitWebView^ dispatchLifetime = this;
    m_draining = true;
    try {
        while (!m_closed) {
            if (!m_deferredEvents.empty()) {
                auto copied = std::move(m_deferredEvents.front());
                m_deferredEvents.pop_front();
                copied.event.uri = { reinterpret_cast<const uint8_t*>(copied.uri.data()), static_cast<uint32_t>(copied.uri.size()) };
                copied.event.title = { reinterpret_cast<const uint8_t*>(copied.title.data()), static_cast<uint32_t>(copied.title.size()) };
                ProcessNotification(copied.event);
                continue;
            }
            if (m_awaitingPolicyTerminal)
                break;
            if (m_closing) {
                if (!m_closeIssued) {
                    m_closeIssued = true;
                    ExecuteNative(SourceActionW10M::Close, { });
                }
                if (!m_state.activeNavigationId() && (!m_pageController || !m_pageController->isBound()))
                    FinishClose();
                break;
            }
            if (m_sourceCommands.empty())
                break;
            auto command = std::move(m_sourceCommands.front());
            m_sourceCommands.pop_front();
            try {
                if (command.operation == DeferredOperationW10M::Initialize)
                    IssueInitialization(command.attempt);
                else if (command.operation == DeferredOperationW10M::InitialSource)
                    ExecuteInitialSource(command.attempt);
                else if (command.operation == DeferredOperationW10M::Source)
                    ExecuteSource(command.uri);
                else
                    ExecutePageCommand(command);
            } catch (Exception^ exception) {
                if (!command.deferred) throw;
                ++m_deferredCommandFailures;
                m_lastDeferredCommandError = exception->HResult;
            } catch (const std::bad_alloc&) {
                if (!command.deferred) throw;
                ++m_deferredCommandFailures;
                m_lastDeferredCommandError = E_OUTOFMEMORY;
            } catch (...) {
                if (!command.deferred) throw;
                ++m_deferredCommandFailures;
                m_lastDeferredCommandError = E_FAIL;
            }
        }
    } catch (...) {
        m_draining = false;
        throw;
    }
    m_draining = false;
    (void)dispatchLifetime;
}

// Adopt readiness/failure and queue the latest target without throwing to a native caller.
HRESULT WebKitWebView::CompleteInitialization(unsigned long long page, HRESULT error)
{
    return CompleteInitialization(m_initializationAttempt, page, error);
}

// Resolve only the captured generation and settle live observers before queued navigation.
HRESULT WebKitWebView::CompleteInitialization(unsigned long long attempt, unsigned long long page, HRESULT error)
{
    try {
        CheckThread();
        if (!m_initializationActive || attempt != m_initializationAttempt)
            return S_FALSE;
        auto next = m_state;
        if (!next.finishInitialization(page, error))
            return S_FALSE;
        auto actions = std::move(m_initializationActions);
        m_initializationActions.clear();
        m_initializationActive = false;
        m_initializationQueued = false;
        m_initializationIssued = false;
        ++m_nativeCallDepth;
        try {
            PublishState(std::move(next), stateOnly, nullptr, &actions, error);
            // All live observers were marked terminal before the first callback.
            for (auto& action : actions)
                action.Get()->Publish();
            if (SUCCEEDED(error) && !m_closing) {
                DeferredSource initial { { }, DeferredOperationW10M::InitialSource, m_documentIntent };
                initial.deferred = true;
                m_sourceCommands.push_back(std::move(initial));
            }
        } catch (...) {
            --m_nativeCallDepth;
            throw;
        }
        --m_nativeCallDepth;
        DrainCommands();
        return S_OK;
    } catch (Exception^ exception) { return exception->HResult; }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_FAIL; }
}

// Copy/evaluate synchronous policy with frozen getters and cancellation/error precedence.
WKW10MResult WebKitWebView::HandleStarting(const WKW10MPageEvent& event, uint32_t* cancel)
{
    try {
        CheckThread();
        if (!cancel)
            return WKW10MResultInvalidArgument;
        *cancel = 1;
        if (m_closing || m_closed)
            return WKW10MResultOK;
        validateEvent(event);
        if (event.eventKind != WKW10MEventStarting || m_notificationDepth)
            return WKW10MResultInvalidState;
        auto target = uri(decode(event.uri));
        if (!target)
            return WKW10MResultInvalidArgument;
        if (event.contentKind == WKW10MContentRemote)
            ValidateSource(target);
        else if (target->AbsoluteUri != L"about:blank")
            return WKW10MResultInvalidArgument;
        auto args = ref new WebKitNavigationStartingEventArgs(event.navigationId, target, contentKind(event.contentKind));
        auto next = m_state;
        if (!next.acceptSequence(event) || !next.starting(event, wide(target->AbsoluteUri)))
            return WKW10MResultInvalidState;
        if (event.contentKind == WKW10MContentRemote)
            InvalidateCertificateToken();
        else if (m_sendingHtml)
            m_proposedHtmlId = event.navigationId;
        // Readiness bookkeeping is not a user mutation and cannot veto a new proposal.
        m_policyInterrupted = std::any_of(m_sourceCommands.begin(), m_sourceCommands.end(), [](const DeferredSource& command) {
            return command.operation != DeferredOperationW10M::Initialize && command.operation != DeferredOperationW10M::InitialSource;
        });
        // Suppress draining between the loading-state and Starting transactions.
        ++m_nativeCallDepth;
        try { PublishState(std::move(next), stateOnly, nullptr); }
        catch (...) { --m_nativeCallDepth; throw; }
        ++m_notificationDepth;
        HRESULT handlerError = S_OK;
        try { m_startingHandlers.invoke(this, args); }
        catch (Exception^ exception) { handlerError = exception->HResult; }
        catch (const std::bad_alloc&) { handlerError = E_OUTOFMEMORY; }
        catch (...) { handlerError = E_FAIL; }
        if (FAILED(handlerError)) ++m_handlerFailures;
        args->EndDispatch();
        --m_notificationDepth;
        --m_nativeCallDepth;
        *cancel = args->Cancel || m_policyInterrupted || m_closing;
        m_awaitingPolicyTerminal = !!*cancel || FAILED(handlerError);
        if (!m_awaitingPolicyTerminal)
            m_state.acceptedStarting();
        return *cancel ? WKW10MResultOK : static_cast<WKW10MResult>(handlerError);
    } catch (Exception^ exception) { return static_cast<WKW10MResult>(exception->HResult); }
    catch (const std::bad_alloc&) { return static_cast<WKW10MResult>(E_OUTOFMEMORY); }
    catch (...) { return static_cast<WKW10MResult>(E_FAIL); }
}

// Apply one current structured signal and mark terminal obligations before app handlers.
HRESULT WebKitWebView::ProcessNotification(const WKW10MPageEvent& event)
{
    validateEvent(event);
    auto next = m_state;
    if (!next.acceptSequence(event))
        return S_FALSE;
    auto target = decode(event.uri);
    auto title = decode(event.title);
    Object^ payload = nullptr;
    bool accepted = false;
    switch (event.eventKind) {
    case WKW10MEventSourceChanged:
        if (target.empty()) throw ref new InvalidArgumentException();
        if (event.contentKind == WKW10MContentRemote) ValidateSource(uri(target));
        else if (uri(target)->AbsoluteUri != L"about:blank") throw ref new InvalidArgumentException();
        payload = ref new WebKitSourceChangedEventArgs(uri(target), changeKind(event.changeKind), event.documentGeneration > m_state.documentGeneration());
        accepted = next.sourceChanged(event, target, title);
        if (accepted) {
            m_committedContentKind = event.contentKind;
            if (event.contentKind == WKW10MContentRemote)
                m_hostHtml.clear();
            else if (event.navigationId == m_proposedHtmlId)
                m_hostHtml = m_proposedHtml;
        }
        break;
    case WKW10MEventHistoryChanged:
        payload = ref new WebKitHistoryChangedEventArgs(!!event.canGoBack, !!event.canGoForward);
        accepted = next.historyChanged(event);
        break;
    case WKW10MEventTitleChanged:
        payload = ref new WebKitDocumentTitleChangedEventArgs(text(title));
        accepted = next.titleChanged(event, title);
        break;
    case WKW10MEventCompleted: {
        // The terminal obligation is created before property handlers can close
        // or replace the control. Dispatch below is owed even after such calls.
        accepted = next.completed(event);
        if (!accepted) return S_FALSE;
        auto related = uri(target.empty() ? m_state.activeTarget() : target);
        WebKitCertificateFailureToken^ token = nullptr;
        if (event.certificateToken.issuer) {
            if (!related || related->SchemeName != L"https"
                || (m_certificateIssuer && m_certificateIssuer != event.certificateToken.issuer)
                || event.certificateToken.decision <= m_certificateRevision)
                throw ref new InvalidArgumentException(L"Noncurrent certificate decision binding");
            token = ref new WebKitCertificateFailureToken(m_controlIdentity, event.certificateToken, event.navigationId, related);
        }
        payload = ref new WebKitNavigationCompletedEventArgs(event.navigationId, related, contentKind(event.contentKind),
            outcome(event.outcome), errorDomain(event.errorDomain), event.errorCode, !!event.hasHttpStatus, event.httpStatus, token);
        if (token) {
            m_certificateIssuer = event.certificateToken.issuer;
            m_certificateRevision = event.certificateToken.decision;
            m_certificateToken = token;
        }
        m_awaitingPolicyTerminal = false;
        break;
    }
    default: return S_FALSE;
    }
    if (!accepted)
        return S_FALSE;
    PublishState(std::move(next), event.eventKind, payload);
    return S_OK;
}

// Copy deferred borrowed payloads and contain all failures at the native entry boundary.
HRESULT WebKitWebView::HandleNotification(const WKW10MPageEvent& event)
{
    try {
        CheckThread();
        validateEvent(event);
        if (m_notificationDepth) {
            DeferredEvent copied { event, { }, { } };
            if (event.uri.length) copied.uri.assign(reinterpret_cast<const char*>(event.uri.data), event.uri.length);
            if (event.title.length) copied.title.assign(reinterpret_cast<const char*>(event.title.data), event.title.length);
            m_deferredEvents.push_back(std::move(copied));
            return S_OK;
        }
        auto result = ProcessNotification(event);
        DrainCommands();
        return result;
    } catch (Exception^ exception) { return exception->HResult; }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_FAIL; }
}

// Validate/freeze package options once, retaining actual CA bytes for later retry/page use.
void WebKitWebView::PrepareConfiguration()
{
    if (m_validatedOptions)
        return;
    auto package = Windows::ApplicationModel::Package::Current->InstalledLocation->Path;
    auto result = m_configuration->validate(std::wstring(package->Data(), package->Length()), m_validatedOptions);
    if (FAILED(result))
        throw Exception::CreateException(result);
}

// Enter one monotonic generation without rooting caller actions or allocating a main ID.
void WebKitWebView::BeginInitializationAttempt()
{
    CheckOpen();
    if (m_initializationActive)
        return;
    if (m_initializationAttempt == std::numeric_limits<unsigned long long>::max())
        throw Exception::CreateException(HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW));
    ++m_initializationAttempt;
    m_initializationActive = true;
    m_initializationQueued = false;
    m_initializationIssued = false;
}

// Queue controller setup once after the current publication/async callback unwinds.
void WebKitWebView::RequestInitialization()
{
    if (m_closing || m_closed || m_state.controlPhase() != ControlPhaseW10M::Initializing)
        return;
    BeginInitializationAttempt();
    if (!m_initializationQueued && !m_initializationIssued) {
        m_sourceCommands.push_back({ { }, DeferredOperationW10M::Initialize, m_initializationAttempt });
        m_initializationQueued = true;
    }
    DrainCommands();
}

// Validate frozen configuration before controller work; bind completion to its generation.
void WebKitWebView::IssueInitialization(unsigned long long attempt)
{
    if (!m_initializationActive || attempt != m_initializationAttempt || m_initializationIssued || m_closing)
        return;
    m_initializationQueued = false;
    m_initializationIssued = true;
    ++m_nativeCallDepth;
    HRESULT result = S_OK;
    try {
        PrepareConfiguration();
        if (m_commandHandler) {
            WKW10MString empty { nullptr, 0 };
            result = m_commandHandler(m_commandContext, SourceActionW10M::Initialize, &empty);
        } else {
            constexpr uint64_t required = WKW10MCapabilityStructuredNotifications | WKW10MCapabilityHostHtml;
            m_engineModule = std::make_unique<EngineModuleW10M>();
            auto negotiated = m_engineModule->bind(L"WebCore.dll", WKW10M_ABI_MINOR_VERSION, required);
            if (negotiated == WKW10MResultMissingCapability) {
                // Preserve per-caller async readiness while the device gate keeps
                // unverified engine bits unadvertised.
                WeakReference weak(this);
                auto queued = Dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal,
                    ref new Windows::UI::Core::DispatchedHandler([weak, attempt] {
                        auto view = weak.Resolve<WebKitWebView>();
                        if (view)
                            view->CompleteInitialization(attempt, 0, E_NOTIMPL);
                    }));
                result = queued != nullptr ? S_OK : E_FAIL;
            } else if (negotiated != WKW10MResultOK)
                result = PageControllerW10M::resultToHRESULT(negotiated);
            if (negotiated == WKW10MResultOK && SUCCEEDED(result)) {
                auto* api = m_engineModule->api();
                auto engineOptions = m_validatedOptions->engineOptions(nullptr);
                result = PageControllerW10M::resultToHRESULT(api->initialize(&engineOptions));
                if (SUCCEEDED(result)) {
                    auto pageOptions = m_validatedOptions->pageOptions();
                    WKW10MPageHandle page = 0;
                    result = PageControllerW10M::resultToHRESULT(m_engineModule->createPage(&pageOptions, &page));
                    if (SUCCEEDED(result)) {
                        result = BindPageController(api, page);
                        if (FAILED(result))
                            m_engineModule->closePage(page);
                        else
                            result = CompleteInitialization(attempt, page, S_OK);
                    }
                }
            }
        }
    } catch (Exception^ exception) { result = exception->HResult; }
    catch (const std::bad_alloc&) { result = E_OUTOFMEMORY; }
    catch (...) { result = E_FAIL; }
    --m_nativeCallDepth;
    if (FAILED(result))
        CompleteInitialization(attempt, 0, result);
    else
        DrainCommands();
}

// Create one SDK IAsyncAction with independent state and weak owner-thread detachment.
InitializationActionW10M^ WebKitWebView::CreateInitializationAction()
{
    WeakReference weak(this);
    auto dispatcher = Dispatcher;
    return ref new InitializationActionW10M(dispatcher, [weak, dispatcher](unsigned int id) {
        auto detach = [weak, id] {
            auto view = weak.Resolve<WebKitWebView>();
            if (view)
                view->DetachInitializationAction(id);
        };
        if (dispatcher->HasThreadAccess)
            detach();
        else
            dispatcher->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal,
                ref new Windows::UI::Core::DispatchedHandler(detach));
    });
}

// Drop one ended observation's producer reference without changing shared-attempt state.
void WebKitWebView::DetachInitializationAction(unsigned int id)
{
    CheckThread();
    m_initializationActions.erase(std::remove_if(m_initializationActions.begin(), m_initializationActions.end(),
        [id](Agile<InitializationActionW10M^>& action) { return action.Get()->ObservationId() == id; }), m_initializationActions.end());
}

// Cancel every pending caller before invoking any handler, independently of its own Close.
void WebKitWebView::CancelInitializationActions()
{
    auto actions = std::move(m_initializationActions);
    m_initializationActions.clear();
    m_initializationActive = false;
    m_initializationQueued = false;
    m_initializationIssued = false;
    for (auto& action : actions)
        action.Get()->Resolve(E_ABORT, true);
    ++m_nativeCallDepth;
    for (auto& action : actions)
        action.Get()->Publish();
    --m_nativeCallDepth;
}

// Return a fresh per-caller observation over one generation-bound shared attempt.
IAsyncAction^ WebKitWebView::EnsureInitializedAsync()
{
    CheckOpen();
    auto action = CreateInitializationAction();
    if (m_state.controlPhase() == ControlPhaseW10M::Ready) {
        action->Resolve(S_OK, false);
        return action;
    }
    m_initializationActions.emplace_back(action);
    auto next = m_state;
    if (next.beginInitialization()) {
        BeginInitializationAttempt();
        PublishState(std::move(next), stateOnly, nullptr);
    }
    RequestInitialization();
    return action;
}

// Enqueue one copied mutation and invalidate an active Starting proposal on reentrancy.
void WebKitWebView::QueuePageCommand(DeferredSource&& command)
{
    CheckOpen();
    command.deferred = command.deferred || m_notificationDepth || m_nativeCallDepth || m_draining || m_awaitingPolicyTerminal;
    m_sourceCommands.push_back(std::move(command));
    if (m_state.navigationPhase() == NavigationPhaseW10M::Starting)
        m_policyInterrupted = true;
    DrainCommands();
}

// Resume copied host markup after shared readiness, with weak lifetime/revalidation.
void WebKitWebView::QueueAfterInitialization(DeferredSource&& command)
{
    AdvanceDocumentIntent();
    auto intent = m_documentIntent;
    // Latest host markup replaces an unstarted Source without a synthetic terminal.
    auto next = m_state;
    next.assignSource({ });
    PublishState(std::move(next), stateOnly, nullptr);
    auto pending = std::make_shared<DeferredSource>(std::move(command));
    auto initialized = EnsureInitializedAsync();
    WeakReference weak(this);
    initialized->Completed = ref new AsyncActionCompletedHandler([weak, pending, intent](IAsyncAction^ action, AsyncStatus status) {
        auto view = weak.Resolve<WebKitWebView>();
        if (!view || view->m_closing || status != AsyncStatus::Completed || view->m_documentIntent != intent)
            return;
        action->GetResults();
        pending->deferred = true;
        view->QueuePageCommand(std::move(*pending));
    });
}

// Invalidate only unstarted document continuations without inventing engine navigation IDs.
void WebKitWebView::AdvanceDocumentIntent()
{
    if (m_documentIntent == std::numeric_limits<unsigned long long>::max())
        throw Exception::CreateException(HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW));
    ++m_documentIntent;
}

// Revalidate state/token/availability and forward exact frozen-ABI command payloads.
void WebKitWebView::ExecutePageCommand(DeferredSource& command)
{
    CheckOpen();
    if (command.operation == DeferredOperationW10M::Html && m_state.controlPhase() != ControlPhaseW10M::Ready) {
        QueueAfterInitialization(std::move(command));
        return;
    }
    auto warning = command.warning.Get();
    if (command.operation == DeferredOperationW10M::Html && warning && !IsCurrentCertificateToken(warning))
        throw ref new InvalidArgumentException(L"Warning capability is no longer current");
    if (command.operation == DeferredOperationW10M::Retry
        && (command.attempt != m_certificateEpoch || command.decision.issuer != m_certificateIssuer
            || command.decision.decision != m_certificateRevision))
        throw ref new InvalidArgumentException(L"Retry reservation was invalidated before execution");
    if (command.operation != DeferredOperationW10M::Html || !warning) {
        if (command.operation != DeferredOperationW10M::Retry)
            InvalidateCertificateToken();
    }

    // A bound provider must see even an unavailable direction/no-document Reload:
    // its no-op command also invalidates native pending consent. Only an unbound
    // document-free control can decide these no-ops without an engine call.
    if (!m_pageController && command.operation == DeferredOperationW10M::Back && !CanGoBack) return;
    if (!m_pageController && command.operation == DeferredOperationW10M::Forward && !CanGoForward) return;
    if (!m_pageController && command.operation == DeferredOperationW10M::Reload && !m_state.hasDocument()) return;
    if (command.operation == DeferredOperationW10M::Stop) {
        AdvanceDocumentIntent();
        auto next = m_state;
        auto action = next.assignSource({ });
        PublishState(std::move(next), stateOnly, nullptr);
        if (action == SourceActionW10M::Stop && !m_closing)
            ExecuteNative(action, { });
        return;
    }
    if (!m_pageController)
        throw ref new NotImplementedException(L"Page controller is not bound");
    uint64_t navigation = 0;
    HRESULT result = E_INVALIDARG;
    ++m_nativeCallDepth;
    try {
        switch (command.operation) {
        case DeferredOperationW10M::Back: result = m_pageController->goBack(); break;
        case DeferredOperationW10M::Forward: result = m_pageController->goForward(); break;
        case DeferredOperationW10M::Reload:
            if (m_committedContentKind == WKW10MContentRemote) {
                result = m_pageController->reload();
                break;
            }
            command.html = m_hostHtml;
            // Transient Reload remains HostHtml/history-neutral and never preserves consent.
            [[fallthrough]];
        case DeferredOperationW10M::Html: {
            AdvanceDocumentIntent();
            m_proposedHtml = command.html;
            m_proposedHtmlId = 0;
            m_sendingHtml = true;
            WKW10MString html { reinterpret_cast<const uint8_t*>(command.html.data()), static_cast<uint32_t>(command.html.size()) };
            auto decision = warning ? warning->EngineToken() : WKW10MCertificateToken { 0, 0 };
            result = m_pageController->loadHtml(html, decision, &navigation);
            m_sendingHtml = false;
            break;
        }
        case DeferredOperationW10M::Retry: result = m_pageController->proceed(command.decision, &navigation); break;
        default: break;
        }
    } catch (...) {
        m_sendingHtml = false;
        --m_nativeCallDepth;
        throw;
    }
    --m_nativeCallDepth;
    if (FAILED(result))
        throw Exception::CreateException(result);
}

// Queue actual Back traversal; unavailable history remains a no-op at execution.
void WebKitWebView::GoBack()
{
    CheckOpen();
    QueuePageCommand({ { }, DeferredOperationW10M::Back, 0 });
}

// Queue actual Forward traversal; unavailable history remains a no-op at execution.
void WebKitWebView::GoForward()
{
    CheckOpen();
    QueuePageCommand({ { }, DeferredOperationW10M::Forward, 0 });
}

// Reload only a committed document; host markup replays retained text without history.
void WebKitWebView::Reload()
{
    CheckOpen();
    QueuePageCommand({ { }, DeferredOperationW10M::Reload, 0 });
}

// Queue cancellation/subresource Stop without fabricating main IDs or clearing documents.
void WebKitWebView::Stop()
{
    CheckOpen();
    QueuePageCommand({ { }, DeferredOperationW10M::Stop, 0 });
}

// Copy arbitrary host text by length; generic HTML invalidates eligibility on execution.
void WebKitWebView::NavigateToString(String^ html)
{
    CheckOpen();
    if (!html) throw ref new InvalidArgumentException(L"HTML must not be null");
    DeferredSource command { { }, DeferredOperationW10M::Html, 0 };
    command.html = encode(wide(html));
    if (command.html.size() > std::numeric_limits<uint32_t>::max()) throw ref new InvalidArgumentException();
    QueuePageCommand(std::move(command));
}

// Associate copied warning text only with this control's exact current decision object.
void WebKitWebView::NavigateToString(String^ html, WebKitCertificateFailureToken^ warning)
{
    CheckOpen();
    if (!html || !IsCurrentCertificateToken(warning)) throw ref new InvalidArgumentException(L"Invalid warning association");
    DeferredSource command { { }, DeferredOperationW10M::Html, 0 };
    command.html = encode(wide(html));
    if (command.html.size() > std::numeric_limits<uint32_t>::max()) throw ref new InvalidArgumentException();
    command.warning = warning;
    QueuePageCommand(std::move(command));
}

// Consume local object eligibility before enqueueing exact hidden-provider retry data.
void WebKitWebView::ProceedWithCertificateFailure(WebKitCertificateFailureToken^ token)
{
    CheckOpen();
    if (!IsCurrentCertificateToken(token)) throw ref new InvalidArgumentException(L"Invalid certificate capability");
    DeferredSource command { ValidateSource(token->FailedUri), DeferredOperationW10M::Retry, 0 };
    command.decision = token->EngineToken();
    InvalidateCertificateToken();
    command.attempt = m_certificateEpoch;
    QueuePageCommand(std::move(command));
}

// Drop public subscriber roots after all owed notifications and native cleanup complete.
void WebKitWebView::RevokePublicEvents()
{
    m_propertyChangedHandlers.clear();
    m_startingHandlers.clear();
    m_sourceChangedHandlers.clear();
    m_historyHandlers.clear();
    m_titleHandlers.clear();
    m_completedHandlers.clear();
}

// Finalize once, retaining only final observable values and process-pinned code ownership.
void WebKitWebView::FinishClose()
{
    if (m_closed || m_notificationDepth || m_nativeCallDepth || m_state.activeNavigationId())
        return;
    auto next = m_state;
    next.markClosed();
    PublishState(std::move(next), stateOnly, nullptr);
    m_closed = true;
    RevokePublicEvents();
    TeardownPresentation();
    if (m_windowHooksInstalled) {
        m_presentationWindow->VisibilityChanged -= m_windowVisibilityToken;
        m_presentationWindow->Activated -= m_windowActivationToken;
        m_windowHooksInstalled = false;
    }
    m_presentationWindow = nullptr;
    if (m_frameDriver && m_frameClient) m_frameDriver->RemoveClient(m_frameClient);
    m_frameClient = 0;
    m_frameScheduler.reset();
    m_frameDriver = nullptr;
    m_sourceCommands.clear();
    m_deferredEvents.clear();
    m_pageCallbacks.reset();
    m_pageController.reset();
    m_engineModule.reset();
    m_commandHandler = nullptr;
    m_commandContext = nullptr;
    m_validatedOptions.reset();
    m_configuration.reset();
    m_hostHtml.clear();
    m_proposedHtml.clear();
}

// Reject work immediately and finish pending public/native dispatch before one cleanup.
void WebKitWebView::Close()
{
    CheckThread();
    if (m_closed)
        return;
    if (m_closing) {
        DrainCommands();
        return;
    }
    m_closing = true;
    // Native detach must wait until any in-progress frame/callback stack unwinds.
    if (m_frameScheduler) m_frameScheduler->setPresentationEnabled(false);
    InvalidateCertificateToken();
    m_policyInterrupted = true;
    m_state.markClosing();
    m_sourceCommands.clear();
    m_hostHtml.clear();
    m_proposedHtml.clear();
    CancelInitializationActions();
    DrainCommands();
}

} // namespace WebKit

