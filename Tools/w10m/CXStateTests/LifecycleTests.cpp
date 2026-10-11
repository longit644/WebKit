// Diagnostic-only real-control lifetime tests through a retained frozen V1 table.
#include "WebKitWebView.h"

#include <atomic>
#include <cstdio>
#include <inspectable.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::UI::Xaml::Data;
using namespace WebKit;

namespace {

// Versioned fail-closed lifetime assertion aggregation.
struct LifecycleReport {
    // Number of real-control lifecycle assertions evaluated.
    unsigned checks { 0 };
    // Failures retained independently of subsequent passes.
    unsigned failures { 0 };
    // Full ASCII assertion lines saved to LocalState.
    std::string lines;
    // Record one assertion without masking earlier failures.
    void check(bool value, const char* name)
    {
        ++checks;
        if (!value) ++failures;
        lines += value ? "PASS: " : "FAIL: ";
        lines += name;
        lines += "\r\n";
    }
};

// Exact WinRT error oracle, also used by worker calls with no UI-thread mutation.
template<typename F> HRESULT lifecycleResult(F operation)
{
    try { operation(); return S_OK; }
    catch (Exception^ exception) { return exception->HResult; }
    catch (...) { return E_FAIL; }
}

// Native/CX-safe registry entry forward declaration for the test provider.
struct LifecyclePage;
// Context-free V1 signatures route only by their explicit diagnostic page handle.
std::map<uint64_t, LifecyclePage*> lifecyclePages;

// Controlled provider with asynchronous loading and explicit Pending close acknowledgement.
struct LifecyclePage {
    // Real authored XAML control whose public API/handlers are tested.
    WebKitWebView^ view { ref new WebKitWebView() };
    // Frozen engine table adopted by the production native controller.
    WKUWPEngineAPI api { };
    // Provider-owned retained callback attachment.
    WKUWPPageCallbacks callbacks { };
    // Extra native-only reference preserving a stale-table safety oracle after engine release.
    PageCallbacksUWP* retained { nullptr };
    // Distinct page identity for peer isolation.
    uint64_t page;
    // Active provider navigation and monotonic main/sequence/document identities.
    uint64_t active { 0 }, nextId { 0 }, sequence { 0 }, generation { 0 };
    // Native close calls, proving one-time cleanup across reentrant/repeated Close.
    unsigned closes { 0 };
    // Provider work calls, proving closed/foreign-thread mutations had no native effects.
    unsigned commands { 0 };
    // Public terminal deliveries and ordered event/native-cleanup trace.
    unsigned completions { 0 };
    std::vector<std::string> trace;
    // Last copied public terminal arguments.
    WebKitNavigationCompletedEventArgs^ completed;
    // Current provider proposal URI; callback packets borrow it only during the call.
    std::string target;
    // Whether engine ownership still requires release acknowledgement.
    bool attached { false };
    // Return Pending instead of releasing the engine reference inline at Close.
    bool pendingClose { false };

    // Bind a real control and retain both engine ownership and a native-only late-call oracle.
    explicit LifecyclePage(uint64_t id, bool pending = false) : page(id), pendingClose(pending)
    {
        lifecyclePages[page] = this;
        api.header = { sizeof(api), WKUWP_STRUCTURE_VERSION };
        api.abiMajor = 1;
        api.capabilities = WKUWPCapabilityStructuredNotifications | WKUWPCapabilityHostHtml | WKUWPCapabilityCertificateDecisions;
        api.setCallbacks = &attach;
        api.navigate = &navigate;
        api.loadHtml = &html;
        api.goBack = &operation;
        api.goForward = &operation;
        api.reload = &operation;
        api.stop = &stop;
        api.proceedWithCertificateFailure = &proceed;
        api.closePage = &close;
        view->SetCommandHandler(&initialize, this);
        auto ready = view->EnsureInitializedAsync();
        if (FAILED(view->BindPageController(&api, page))) throw Exception::CreateException(E_FAIL);
        retained = static_cast<PageCallbacksUWP*>(callbacks.context);
        retained->retain();
        view->CompleteInitialization(view->InitializationAttemptId(), page, S_OK);
        ready->GetResults();
        view->NavigationCompleted += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationCompletedEventArgs^>(
            [this](WebKitWebView^, WebKitNavigationCompletedEventArgs^ args) { completed = args; ++completions; trace.push_back("completed"); });
    }

    // Suppress expired fixture captures, close/release provider ownership and erase routing.
    ~LifecyclePage()
    {
        if (retained) retained->deactivate();
        view->Close();
        if (attached) acknowledge();
        if (retained) retained->release();
        lifecyclePages.erase(page);
    }

    // Retrieve only a known provider handle, never a globally selected instance.
    static LifecyclePage* find(uint64_t page)
    {
        auto item = lifecyclePages.find(page);
        return item == lifecyclePages.end() ? nullptr : item->second;
    }

    // Build a strictly ordered current-page V1 signal.
    WKUWPPageEvent event(uint32_t kind)
    {
        WKUWPPageEvent value { };
        value.header = { sizeof(value), WKUWP_STRUCTURE_VERSION };
        value.pageId = page;
        value.navigationId = active;
        value.callbackSequence = ++sequence;
        value.documentGeneration = generation;
        value.eventKind = kind;
        value.isLoading = !!active;
        value.uri = { reinterpret_cast<const uint8_t*>(target.data()), static_cast<uint32_t>(target.size()) };
        return value;
    }

    // Emit one terminal with copied related URI before native identity can be reused.
    void finish(uint32_t outcome = WKUWPOutcomeCompleted)
    {
        if (!active) return;
        auto value = event(WKUWPEventCompleted);
        auto related = target;
        value.uri = { reinterpret_cast<const uint8_t*>(related.data()), static_cast<uint32_t>(related.size()) };
        value.outcome = outcome;
        value.isLoading = 0;
        active = 0;
        callbacks.notify(callbacks.context, &value);
    }

    // Commit without finishing loading so Close can cancel a committed main document.
    void commit()
    {
        ++generation;
        auto value = event(WKUWPEventSourceChanged);
        // Stable ASCII provider title whose copied final value is checked after cleanup.
        constexpr char title[] = "Final readable title";
        value.title = { reinterpret_cast<const uint8_t*>(title), sizeof(title) - 1 };
        callbacks.notify(callbacks.context, &value);
    }

    // Release the provider reference; the production weak attachment acknowledges cleanup.
    void acknowledge()
    {
        if (!attached) return;
        attached = false;
        trace.push_back("acknowledged");
        callbacks.releaseContext(callbacks.context);
    }

    // Controlled shared setup is separate from navigation and cleanup counters.
    static HRESULT __stdcall initialize(void*, SourceActionUWP kind, const WKUWPString*)
    {
        return kind == SourceActionUWP::Initialize ? S_OK : E_NOTIMPL;
    }

    // Attach one retained frozen callback reference exactly once.
    static WKUWPResult __stdcall attach(uint64_t page, const WKUWPPageCallbacks* callbacks)
    {
        auto self = find(page);
        if (!self || self->attached) return WKUWPResultInvalidArgument;
        self->callbacks = *callbacks;
        callbacks->retainContext(callbacks->context);
        self->attached = true;
        return WKUWPResultOK;
    }

    // Start asynchronous work and honor real public policy without completing it early.
    static WKUWPResult __stdcall navigate(uint64_t page, const WKUWPNavigationRequest* request, uint64_t* id)
    {
        auto self = find(page);
        if (!self || !request || !id) return WKUWPResultInvalidArgument;
        ++self->commands;
        self->finish(WKUWPOutcomeCancelled);
        self->target.assign(reinterpret_cast<const char*>(request->uri.data), request->uri.length);
        self->active = ++self->nextId;
        *id = self->active;
        auto value = self->event(WKUWPEventStarting);
        uint32_t cancel = 0;
        auto result = self->callbacks.starting(self->callbacks.context, &value, &cancel);
        if (cancel) self->finish(WKUWPOutcomeCancelled);
        else if (result != WKUWPResultOK) {
            auto failed = self->event(WKUWPEventCompleted);
            failed.outcome = WKUWPOutcomeFailed;
            failed.errorDomain = WKUWPErrorDomainPlatform;
            failed.errorCode = result;
            self->active = 0;
            self->callbacks.notify(self->callbacks.context, &failed);
        }
        return WKUWPResultOK;
    }

    // Unsupported fixture HTML still provides a nonnull conforming negotiated slot.
    static WKUWPResult __stdcall html(uint64_t, const WKUWPNavigationRequest*, uint64_t*) { return WKUWPResultUnsupported; }

    // Count only actual provider history/reload work for no-side-effect thread checks.
    static WKUWPResult __stdcall operation(uint64_t page)
    {
        auto self = find(page);
        if (!self) return WKUWPResultClosed;
        ++self->commands;
        return WKUWPResultOK;
    }

    // Stop outstanding work without inserting a new navigation identity.
    static WKUWPResult __stdcall stop(uint64_t page)
    {
        auto self = find(page);
        if (!self) return WKUWPResultClosed;
        ++self->commands;
        self->finish(WKUWPOutcomeCancelled);
        return WKUWPResultOK;
    }

    // Invalid tokens remain invalid and cannot produce a fixture navigation.
    static WKUWPResult __stdcall proceed(uint64_t, const WKUWPCertificateToken*, uint64_t*) { return WKUWPResultInvalidCertificateToken; }

    // Cancel active work once, then acknowledge immediately or retain Pending ownership.
    static WKUWPResult __stdcall close(uint64_t page)
    {
        auto self = find(page);
        if (!self) return WKUWPResultClosed;
        ++self->closes;
        self->trace.push_back("native-close");
        self->finish(WKUWPOutcomeCancelled);
        if (self->pendingClose) return WKUWPResultPending;
        self->acknowledge();
        return WKUWPResultOK;
    }
};

// Internal captured object with no public constructor/metadata, used for weak release oracles.
ref class LifecycleCaptureUWP sealed {
internal:
    // Create a marker whose lifetime is controlled only by registered delegate captures.
    LifecycleCaptureUWP() { }
};

// Shared native worker result/event ownership survives UI wait timeout without stack captures.
struct LifecycleWorkerUWP {
    // Auto-reset completion event for one bounded worker check.
    HANDLE done { CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS) };
    // Per-operation worker errors, written before the completion event signals.
    std::vector<HRESULT> results;
    // Frozen table with an extra native reference independent of engine ownership.
    WKUWPPageCallbacks callbacks { };
    // Protect the caller-supplied native context until worker execution actually ends.
    PageCallbacksUWP* retained { nullptr };
    // Release only native event/context state, never XAML, on the last shared reference.
    ~LifecycleWorkerUWP() { if (done) CloseHandle(done); if (retained) retained->release(); }
};

// Construct a real absolute URI for owner-thread/worker mutation inputs.
Uri^ lifecycleUri() { return ref new Uri(L"https://example.test/lifecycle"); }

// Verify lifecycle ordering, delayed acknowledgement, weak roots and explicit API errors.
void runLifecycle(LifecycleReport& report)
{
    {
        LifecyclePage f(501);
        f.view->Source = lifecycleUri();
        f.commit();
        f.view->Close();
        report.check(f.closes == 1 && f.completions == 1 && f.completed->Outcome == WebKitNavigationOutcome::Cancelled,
            "ordinary Close cancels one committed loading navigation and closes native page once");
        report.check(f.view->ControlPhase() == ControlPhaseUWP::Closed && !f.view->IsLoading
                && f.view->Source->AbsoluteUri == L"https://example.test/lifecycle" && f.view->DocumentTitle == L"Final readable title",
            "Closed final Source title loading snapshot remains readable after instance release");
        f.view->Close(); f.view->Close();
        report.check(f.closes == 1 && f.completions == 1, "repeated Close never repeats page release or terminal delivery");
        report.check(!f.view->ValidatedInitializationOptions() && !f.retained->engineReferences(),
            "ordinary Close releases per-instance validated assets and engine attachment ownership");
        auto late = f.event(WKUWPEventTitleChanged);
        f.callbacks.notify(f.callbacks.context, &late);
        report.check(f.view->DocumentTitle == L"Final readable title" && f.completions == 1, "retained stale callback cannot mutate closed final snapshot");
        uint32_t cancel = 0;
        late.eventKind = WKUWPEventStarting;
        report.check(f.callbacks.starting(f.callbacks.context, &late, &cancel) == WKUWPResultOK && cancel == 1,
            "inactive late policy fails closed without touching released page state");
        report.check(lifecycleResult([&] { f.view->Source = nullptr; }) == RO_E_CLOSED
                && lifecycleResult([&] { f.view->GoBack(); }) == RO_E_CLOSED && lifecycleResult([&] { f.view->GoForward(); }) == RO_E_CLOSED
                && lifecycleResult([&] { f.view->Reload(); }) == RO_E_CLOSED && lifecycleResult([&] { f.view->Stop(); }) == RO_E_CLOSED
                && lifecycleResult([&] { f.view->NavigateToString(L"closed"); }) == RO_E_CLOSED
                && lifecycleResult([&] { f.view->NavigateToString(L"closed", nullptr); }) == RO_E_CLOSED
                && lifecycleResult([&] { f.view->ProceedWithCertificateFailure(nullptr); }) == RO_E_CLOSED
                && lifecycleResult([&] { f.view->EnsureInitializedAsync(); }) == RO_E_CLOSED,
            "all closed public mutations reject before arguments or provider side effects");
        report.check(lifecycleResult([&] { f.view->NavigationCompleted += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationCompletedEventArgs^>([](WebKitWebView^, WebKitNavigationCompletedEventArgs^) { }); }) == RO_E_CLOSED,
            "closed event registration is rejected as new work");
    }
    {
        LifecyclePage f(502);
        bool closingError = false;
        f.view->PropertyChanged += ref new PropertyChangedEventHandler([&](Object^, PropertyChangedEventArgs^ args) {
            if (args->PropertyName == L"IsLoading" && !f.view->IsLoading) {
                f.view->Close();
                closingError = lifecycleResult([&] { f.view->Stop(); }) == RO_E_CLOSED;
            }
        });
        f.view->Source = lifecycleUri();
        f.commit();
        f.finish();
        report.check(closingError && f.completions == 1 && f.completed->Outcome == WebKitNavigationOutcome::Completed,
            "Close inside terminal PropertyChanged preserves already-owed Completed outcome");
        report.check(f.trace.size() >= 3 && f.trace[0] == "completed" && f.trace[1] == "native-close" && f.closes == 1,
            "owed terminal dispatch precedes handler revocation and native cleanup trace");
    }
    {
        LifecyclePage f(503);
        f.view->NavigationStarting += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationStartingEventArgs^>(
            [&f](WebKitWebView^, WebKitNavigationStartingEventArgs^) { f.view->Close(); });
        f.view->Source = lifecycleUri();
        report.check(f.completions == 1 && f.completed->Outcome == WebKitNavigationOutcome::Cancelled && f.closes == 1
                && f.view->ControlPhase() == ControlPhaseUWP::Closed,
            "Close inside Starting delivers its sole cancellation before revocation");
    }
    {
        LifecyclePage f(504);
        unsigned observed = 0;
        f.view->NavigationCompleted += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationCompletedEventArgs^>(
            [&f, &observed](WebKitWebView^, WebKitNavigationCompletedEventArgs^) { ++observed; f.view->Close(); });
        f.view->Source = lifecycleUri();
        f.commit();
        f.finish();
        report.check(observed == 1 && f.completions == 1 && f.completed->Outcome == WebKitNavigationOutcome::Completed && f.closes == 1,
            "Close inside Completed cannot invent another terminal for an already-final ID");
    }
    {
        LifecyclePage f(505, true);
        f.view->Source = lifecycleUri();
        f.commit();
        f.view->Close();
        report.check(f.view->ControlPhase() == ControlPhaseUWP::Closing && f.closes == 1 && f.completions == 1
                && f.retained->engineReferences() == 1, "Pending native close keeps cleanup ownership after owed cancellation");
        f.view->Close();
        report.check(f.closes == 1 && f.view->ControlPhase() == ControlPhaseUWP::Closing, "repeated pending Close does not reissue native close");
        f.acknowledge();
        report.check(f.view->ControlPhase() == ControlPhaseUWP::Closed && !f.retained->engineReferences()
                && !f.view->ValidatedInitializationOptions(), "engine retained-context release acknowledges pending cleanup and releases instance once");
    }
    {
        LifecyclePage a(506), b(507);
        a.view->Source = lifecycleUri(); b.view->Source = lifecycleUri();
        a.commit(); b.commit();
        a.view->Close();
        auto late = a.event(WKUWPEventHistoryChanged);
        late.canGoBack = 1;
        a.callbacks.notify(a.callbacks.context, &late);
        report.check(b.view->IsLoading && b.completions == 0 && b.closes == 0 && !b.view->CanGoBack,
            "closing and late-callback activity in one page leaves its peer live and isolated");
        b.finish();
        report.check(b.completions == 1 && !b.view->IsLoading, "surviving peer receives its normal completion exactly once");
    }
    {
        auto control = ref new WebKitWebView();
        auto captured = ref new LifecycleCaptureUWP();
        WeakReference weakCapture(captured);
        control->NavigationCompleted += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationCompletedEventArgs^>(
            [captured](WebKitWebView^, WebKitNavigationCompletedEventArgs^) { (void)captured; });
        captured = nullptr;
        report.check(weakCapture.Resolve<LifecycleCaptureUWP>() != nullptr, "live public subscription retains its captured object");
        control->Close();
        report.check(weakCapture.Resolve<LifecycleCaptureUWP>() == nullptr, "final Close revokes delegate roots and releases captured subscribers");
    }
    {
        auto control = ref new WebKitWebView();
        WeakReference weak(control);
        control->SourceChanged += ref new TypedEventHandler<WebKitWebView^, WebKitSourceChangedEventArgs^>(
            [control](WebKitWebView^, WebKitSourceChangedEventArgs^) { (void)control; });
        control->Close();
        control = nullptr;
        report.check(weak.Resolve<WebKitWebView>() == nullptr, "Close breaks control-capturing delegate cycle instead of rooting a dead instance");
    }
    {
        auto control = ref new WebKitWebView();
        WKUWPPageCallbacks callbacks { };
        control->BuildPageCallbacks(&callbacks);
        callbacks.retainContext(callbacks.context);
        WeakReference weak(control);
        control = nullptr;
        report.check(weak.Resolve<WebKitWebView>() == nullptr, "retained native callback attachment holds only a weak control subscription");
        WKUWPPageEvent late { };
        late.header = { sizeof(late), 1 };
        late.eventKind = WKUWPEventStarting;
        uint32_t cancel = 0;
        report.check(callbacks.starting(callbacks.context, &late, &cancel) == WKUWPResultOK && cancel,
            "expired weak callback target is rejected without resurrection or stale access");
        callbacks.releaseContext(callbacks.context);
    }
    {
        LifecyclePage f(508);
        auto worker = std::make_shared<LifecycleWorkerUWP>();
        worker->callbacks = f.callbacks;
        worker->retained = f.retained;
        worker->retained->retain();
        auto view = f.view;
        auto uri = lifecycleUri();
        auto commands = f.commands;
        Windows::System::Threading::ThreadPool::RunAsync(ref new Windows::System::Threading::WorkItemHandler(
            [worker, view, uri](IAsyncAction^) {
                worker->results.push_back(lifecycleResult([&] { view->Source = uri; }));
                worker->results.push_back(lifecycleResult([&] { view->GoBack(); }));
                worker->results.push_back(lifecycleResult([&] { view->GoForward(); }));
                worker->results.push_back(lifecycleResult([&] { view->Reload(); }));
                worker->results.push_back(lifecycleResult([&] { view->Stop(); }));
                worker->results.push_back(lifecycleResult([&] { view->NavigateToString(L"worker"); }));
                worker->results.push_back(lifecycleResult([&] { view->NavigateToString(L"worker", nullptr); }));
                worker->results.push_back(lifecycleResult([&] { view->ProceedWithCertificateFailure(nullptr); }));
                worker->results.push_back(lifecycleResult([&] { view->EnsureInitializedAsync(); }));
                worker->results.push_back(lifecycleResult([&] { view->Close(); }));
                WKUWPPageEvent value { };
                uint32_t cancel = 0;
                worker->results.push_back(worker->callbacks.starting(worker->callbacks.context, &value, &cancel));
                worker->callbacks.notify(worker->callbacks.context, &value);
                SetEvent(worker->done);
            }));
        auto finished = WaitForSingleObjectEx(worker->done, 5000, FALSE) == WAIT_OBJECT_0;
        bool wrongThread = finished && worker->results.size() == 11;
        if (finished) for (auto result : worker->results) wrongThread = wrongThread && result == RPC_E_WRONG_THREAD;
        report.check(wrongThread, "all worker public mutations and frozen policy callbacks report RPC_E_WRONG_THREAD");
        report.check(f.commands == commands && f.closes == 0 && f.view->ControlPhase() == ControlPhaseUWP::Ready
                && f.retained->lastResult() == RPC_E_WRONG_THREAD, "wrong-thread attempts leave owner page registrations and snapshots unchanged");
    }
}

} // namespace

// Diagnostic-only owner-thread entry: verify ordinary close/weak/thread contracts.
extern "C" __declspec(dllexport) HRESULT __stdcall WKUWPTestLifecycle(const wchar_t* path)
{
    LifecycleReport report;
    try { runLifecycle(report); }
    catch (Exception^ exception) {
        report.check(false, "uncaught lifecycle exception");
        report.lines += "HRESULT=" + std::to_string(exception->HResult) + "\r\n";
    } catch (...) { report.check(false, "uncaught native lifecycle exception"); }
    char header[256];
    sprintf_s(header, "version=" TEST_PACKAGE_VERSION "\r\nresult=%s\r\nchecks=%u\r\nfailures=%u\r\n", report.failures ? "FAIL" : "PASS", report.checks, report.failures);
    report.lines.insert(0, header);
    auto file = CreateFile2(path, GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, nullptr);
    if (file == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    DWORD written = 0;
    auto saved = WriteFile(file, report.lines.data(), static_cast<DWORD>(report.lines.size()), &written, nullptr);
    FlushFileBuffers(file);
    CloseHandle(file);
    return saved && written == report.lines.size() && !report.failures ? S_OK : E_FAIL;
}
