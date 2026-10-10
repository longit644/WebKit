// Compiled into a diagnostic-only component copy. The shipped component has no
// test exports. Assertions exercise the real control/DP/events on a XAML thread;
// the command driver supplies deterministic frozen V1 signals without networking.
#include "WebKitWebView.h"

#include <algorithm>
#include <cstdio>
#include <inspectable.h>
#include <memory>
#include <string>
#include <vector>

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Data;
using namespace WebKit;

namespace {

// Aggregate every device assertion; a single failure makes the versioned run fail.
struct Report {
    // Number of assertions evaluated on the actual XAML control.
    unsigned checks { 0 };
    // Number of failed assertions, retained through the complete run.
    unsigned failures { 0 };
    // Per-assertion ASCII results saved with package identity.
    std::string lines;
    // Append an assertion outcome and retain failure independently of later success.
    void check(bool result, const char* name)
    {
        ++checks;
        if (!result) ++failures;
        lines += result ? "PASS: " : "FAIL: ";
        lines += name;
        lines += "\r\n";
    }
};

// Read the public published URI using the same null convention as the contract.
std::wstring source(WebKitWebView^ view)
{
    auto value = view->Source;
    return value ? std::wstring(value->AbsoluteUri->Data(), value->AbsoluteUri->Length()) : std::wstring();
}

// Construct a test URI through the real Foundation projection.
Uri^ target(const wchar_t* value) { return ref new Uri(ref new String(value)); }
// Compare the real DP value with the public frozen Source getter.
bool dpMatches(WebKitWebView^ view)
{
    auto value = safe_cast<Uri^>(view->GetValue(WebKitWebView::SourceProperty));
    return value ? view->Source && value->AbsoluteUri == view->Source->AbsoluteUri : !view->Source;
}

// Encode deterministic UTF16 fixture payloads into borrowed frozen-ABI spans.
std::string bytes(const std::wstring& value)
{
    if (value.empty()) return { };
    auto length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), &result[0], length, nullptr, nullptr);
    return result;
}

// Per-control deterministic command port supplying real frozen V1 page signals.
struct Driver {
    // Real authored XAML object under test, never a replacement property model.
    WebKitWebView^ view;
    // Distinct simulated page identity for routing/isolation assertions.
    uint64_t page;
    // Strictly increasing sequence across this page's event kinds.
    uint64_t sequence { 0 };
    // Current committed document generation.
    uint64_t generation { 0 };
    // Highest main ID allocated by this driver.
    uint64_t nextId { 0 };
    // Main ID currently awaiting terminal delivery.
    uint64_t active { 0 };
    // Initialization commands observed, including explicit retries.
    unsigned initializes { 0 };
    // Remote proposals observed, excluding internal DP publication.
    unsigned navigates { 0 };
    // Outstanding/subresource Stop commands observed.
    unsigned stops { 0 };
    // Native Close commands observed for idempotence checks.
    unsigned closes { 0 };
    // Actual public terminal events delivered by the control.
    unsigned completions { 0 };
    // Actual public SourceChanged events delivered by the control.
    unsigned commits { 0 };
    // Engine previous-neighbor availability used by controlled signals.
    bool back { false };
    // Engine next-neighbor availability used by controlled signals.
    bool forward { false };
    // Current proposed target, distinct from committed/restored control Source.
    std::wstring requested;
    // Current controlled document title, copied into borrowed event spans.
    std::wstring title;
    // Ordered native proposals proving deferred command FIFO behavior.
    std::vector<std::wstring> targets;
    // Native proposal/public terminal trace used to verify transaction boundaries.
    std::vector<std::string> trace;

    // Create/bind a real control and subscribe observable assertion counters.
    explicit Driver(uint64_t id, WebKitWebView^ existing = nullptr)
        : view(existing ? existing : ref new WebKitWebView())
        , page(id)
    {
        view->SetCommandHandler(&command, this);
        view->NavigationCompleted += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationCompletedEventArgs^>(
            [this](WebKitWebView^, WebKitNavigationCompletedEventArgs^ args) {
                ++completions;
                trace.push_back("completed:" + std::to_string(args->NavigationId));
            });
        view->SourceChanged += ref new TypedEventHandler<WebKitWebView^, WebKitSourceChangedEventArgs^>(
            [this](WebKitWebView^, WebKitSourceChangedEventArgs^) { ++commits; });
    }

    // Clear the borrowed command context before releasing the diagnostic fixture.
    ~Driver()
    {
        view->SetCommandHandler(nullptr, nullptr);
        view->Close();
    }

    // Create a current-page V1 event with explicit generation/sequence/history state.
    WKUWPPageEvent event(uint32_t kind)
    {
        WKUWPPageEvent result { };
        result.header = { sizeof(result), WKUWP_STRUCTURE_VERSION };
        result.pageId = page;
        result.callbackSequence = ++sequence;
        result.documentGeneration = generation;
        result.navigationId = active;
        result.eventKind = kind;
        result.canGoBack = back;
        result.canGoForward = forward;
        result.isLoading = !!active;
        return result;
    }

    // Resolve controlled initialization so the latest initial Source may start.
    void ready()
    {
        if (view->ControlPhase() == ControlPhaseUWP::Created)
            view->Source = target(L"https://example.test/initial");
        view->CompleteInitialization(page, S_OK);
    }

    // Deliver one structured terminal; clear driver activity before reentrant callbacks.
    void finish(uint32_t outcome = WKUWPOutcomeCompleted, uint32_t domain = WKUWPErrorDomainNone, int code = 0)
    {
        if (!active) return;
        auto data = event(WKUWPEventCompleted);
        data.outcome = outcome;
        data.errorDomain = domain;
        data.errorCode = code;
        data.isLoading = 0;
        auto uriBytes = bytes(requested);
        data.uri = { reinterpret_cast<const uint8_t*>(uriBytes.data()), static_cast<uint32_t>(uriBytes.size()) };
        active = 0;
        view->HandleNotification(data);
    }

    // Commit copied URI/title and scramble borrowed buffers after the real delivery.
    HRESULT commit(const wchar_t* effective, const wchar_t* documentTitle = L"Document")
    {
        ++generation;
        title = documentTitle;
        auto data = event(WKUWPEventSourceChanged);
        auto uriBytes = bytes(effective);
        auto titleBytes = bytes(title);
        data.uri = { reinterpret_cast<const uint8_t*>(uriBytes.data()), static_cast<uint32_t>(uriBytes.size()) };
        data.title = { reinterpret_cast<const uint8_t*>(titleBytes.data()), static_cast<uint32_t>(titleBytes.size()) };
        auto result = view->HandleNotification(data);
        // Borrowed memory is intentionally invalidated after delivery.
        std::fill(uriBytes.begin(), uriBytes.end(), '!');
        std::fill(titleBytes.begin(), titleBytes.end(), '!');
        return result;
    }

    // Emit a genuine kind/generation-shaped same-document signal without a new main ID.
    HRESULT sameDocument(const wchar_t* effective, uint32_t kind)
    {
        auto data = event(WKUWPEventSourceChanged);
        data.navigationId = 0;
        data.changeKind = kind;
        auto uriBytes = bytes(effective);
        auto titleBytes = bytes(title);
        data.uri = { reinterpret_cast<const uint8_t*>(uriBytes.data()), static_cast<uint32_t>(uriBytes.size()) };
        data.title = { reinterpret_cast<const uint8_t*>(titleBytes.data()), static_cast<uint32_t>(titleBytes.size()) };
        return view->HandleNotification(data);
    }

    // Execute deterministic instance work and honor the real synchronous policy verdict.
    static HRESULT __stdcall command(void* context, SourceActionUWP action, const WKUWPString* span)
    {
        auto& self = *static_cast<Driver*>(context);
        if (action == SourceActionUWP::Initialize) { ++self.initializes; return S_OK; }
        if (action == SourceActionUWP::Stop) { ++self.stops; self.finish(WKUWPOutcomeCancelled); return S_OK; }
        if (action == SourceActionUWP::Close) { ++self.closes; self.finish(WKUWPOutcomeCancelled); return S_OK; }
        if (action != SourceActionUWP::Navigate) return E_INVALIDARG;
        self.finish(WKUWPOutcomeCancelled);
        ++self.navigates;
        auto length = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(span->data), static_cast<int>(span->length), nullptr, 0);
        self.requested.resize(length);
        MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(span->data), static_cast<int>(span->length), &self.requested[0], length);
        self.targets.push_back(self.requested);
        self.trace.push_back("navigate:" + std::to_string(self.nextId + 1));
        self.active = ++self.nextId;
        auto data = self.event(WKUWPEventStarting);
        auto uriBytes = bytes(self.requested);
        data.uri = { reinterpret_cast<const uint8_t*>(uriBytes.data()), static_cast<uint32_t>(uriBytes.size()) };
        uint32_t cancel = 0;
        auto result = self.view->HandleStarting(data, &cancel);
        if (cancel) self.finish(WKUWPOutcomeCancelled);
        else if (result != WKUWPResultOK) self.finish(WKUWPOutcomeFailed, WKUWPErrorDomainPlatform, result);
        return S_OK;
    }
};

// Verify the exact WinRT HRESULT from a public operation, rather than any exception.
template<typename F> bool failsWith(F operation, HRESULT expected)
{
    try { operation(); }
    catch (Exception^ error) { return error->HResult == expected; }
    return false;
}

// Exercise every null-state row, rollback, copied signals, transactions and live binding.
void run(Report& report, WebKitWebView^ primary, TextBlock^ titleBinding, Button^ backBinding)
{
    {
        Driver f(101);
        report.check(!f.view->Source && !f.view->CanGoBack && !f.view->CanGoForward && !f.view->IsLoading && f.view->DocumentTitle->IsEmpty(), "initial read-only snapshot");
        f.view->Source = nullptr;
        report.check(!f.initializes && !f.navigates && !f.stops && dpMatches(f.view), "Created null clears without initialization or ID");
        f.view->Source = target(L"https://example.test/a");
        f.view->Source = target(L"https://example.test/b");
        report.check(f.initializes == 1 && !f.navigates && source(f.view) == L"https://example.test/b" && !f.view->IsLoading, "Initializing retains latest requested Source without main ID");
        f.view->Source = nullptr;
        report.check(f.view->ControlPhase() == ControlPhaseUWP::Initializing && !f.navigates && !f.stops && !f.view->Source, "Initializing null keeps shared attempt alive and clears queued Source");
        f.view->CompleteInitialization(f.page, S_OK);
        report.check(!f.navigates && !f.view->Source, "cleared initialization queue starts nothing at readiness");
    }
    {
        Driver f(102);
        f.view->Source = target(L"https://example.test/a");
        f.view->CompleteInitialization(0, E_ACCESSDENIED);
        report.check(f.view->ControlPhase() == ControlPhaseUWP::Faulted && !f.view->Source && !f.navigates, "initialization failure restores null without navigation terminal");
        f.view->Source = nullptr;
        report.check(f.initializes == 1 && !f.stops, "Faulted null starts no retry");
        f.view->Source = target(L"https://example.test/retry");
        f.view->Source = target(L"https://example.test/latest");
        f.view->CompleteInitialization(f.page, S_OK);
        report.check(f.initializes == 2 && f.navigates == 1 && f.targets.back() == L"https://example.test/latest", "retry/readiness starts only latest queued Source");
        f.view->Source = nullptr;
        report.check(!f.view->Source && !f.view->IsLoading && f.completions == 1, "first provisional null cancels once and restores null");
    }
    {
        Driver f(103);
        f.ready();
        report.check(f.view->IsLoading && source(f.view) == L"https://example.test/initial", "Starting publishes requested/loading before engine commit");
        f.commit(L"https://example.test/redirected", L"Copied title");
        report.check(source(f.view) == L"https://example.test/redirected" && f.view->DocumentTitle == L"Copied title" && f.navigates == 1 && dpMatches(f.view), "effective commit copies URI/title and never recursively navigates");
        f.view->Source = nullptr;
        report.check(source(f.view) == L"https://example.test/redirected" && !f.view->IsLoading && f.completions == 1, "committed-loading null preserves document and cancels once");
        f.view->Source = nullptr;
        report.check(f.stops == 2 && f.completions == 1 && f.commits == 1 && dpMatches(f.view), "terminal-document null stops subresources without new terminal or source commit");
        f.view->Source = target(L"https://example.test/redirected");
        report.check(f.navigates == 1, "same nonnull assignment is not Reload");
        auto before = source(f.view);
        report.check(failsWith([&] { f.view->Source = target(L"file:///test"); }, E_INVALIDARG), "invalid scheme setter reports E_INVALIDARG");
        report.check(source(f.view) == before && dpMatches(f.view) && f.navigates == 1, "invalid setter preserves Source/DP/controller");
        report.check(failsWith([&] { f.view->SetValue(WebKitWebView::SourceProperty, target(L"about:blank")); }, E_INVALIDARG), "invalid direct DP assignment reports E_INVALIDARG");
        report.check(source(f.view) == before && dpMatches(f.view) && f.navigates == 1, "invalid DP assignment rolls back under internal guard");
        f.view->Source = target(L"https://example.test/failing");
        f.finish(WKUWPOutcomeFailed, WKUWPErrorDomainTransport, 60);
        report.check(source(f.view) == before && !f.view->IsLoading && f.completions == 2 && f.commits == 1, "provisional failure restores prior commit without SourceChanged");
        f.view->Source = target(L"https://example.test/stopped");
        f.view->Source = nullptr;
        report.check(source(f.view) == before && f.completions == 3 && !f.view->IsLoading, "provisional null restores previous committed Source");
        auto count = f.navigates;
        for (auto kind : { WKUWPChangeFragment, WKUWPChangePushState, WKUWPChangeReplaceState, WKUWPChangeHistoryTraversal })
            report.check(f.sameDocument(L"https://example.test/redirected#same", kind) == S_OK && dpMatches(f.view), "same-document URI uses structured signal");
        report.check(f.navigates == count, "same-document publication never recursively navigates");
        auto stale = f.event(WKUWPEventTitleChanged);
        stale.documentGeneration = 0;
        report.check(f.view->HandleNotification(stale) == S_FALSE && f.view->DocumentTitle == L"Copied title", "stale document signal preserves copied title");
        auto foreign = f.event(WKUWPEventHistoryChanged);
        foreign.pageId = 999;
        report.check(f.view->HandleNotification(foreign) == S_FALSE && !f.view->CanGoBack, "foreign-page state cannot leak into control");
    }
    {
        Driver f(104);
        bool coherent = false;
        bool notified = false;
        f.view->PropertyChanged += ref new PropertyChangedEventHandler([&](Object^, PropertyChangedEventArgs^ args) {
            if (args->PropertyName == L"IsLoading" && f.view->IsLoading) {
                notified = true;
                coherent = source(f.view) == L"https://example.test/initial" && dpMatches(f.view);
            }
        });
        f.view->NavigationStarting += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationStartingEventArgs^>(
            [&](WebKitWebView^, WebKitNavigationStartingEventArgs^) { coherent = coherent && notified; f.view->Source = nullptr; coherent = coherent && f.view->IsLoading && dpMatches(f.view); });
        f.ready();
        report.check(coherent && f.completions == 1 && !f.view->Source && !f.view->IsLoading, "Starting null freezes snapshot and cancels proposal exactly once");
    }
    {
        Driver f(105);
        f.ready();
        f.commit(L"https://example.test/a");
        bool frozen = false;
        bool completedFrozen = false;
        bool armed = true;
        f.view->PropertyChanged += ref new PropertyChangedEventHandler([&](Object^, PropertyChangedEventArgs^ args) {
            if (args->PropertyName == L"IsLoading" && !f.view->IsLoading && armed) {
                armed = false;
                auto count = f.navigates;
                f.view->Source = target(L"https://example.test/b");
                f.view->SetValue(WebKitWebView::SourceProperty, target(L"https://example.test/c"));
                frozen = source(f.view) == L"https://example.test/a" && dpMatches(f.view) && f.navigates == count;
            }
        });
        f.view->NavigationCompleted += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationCompletedEventArgs^>(
            [&](WebKitWebView^, WebKitNavigationCompletedEventArgs^ args) {
                if (args->NavigationId == 1) completedFrozen = source(f.view) == L"https://example.test/a" && !f.view->IsLoading && f.navigates == 1;
            });
        f.finish();
        report.check(frozen && completedFrozen, "terminal PropertyChanged defers setters through owed Completed snapshot");
        report.check(f.targets.size() == 3 && f.targets[1] == L"https://example.test/b" && f.targets[2] == L"https://example.test/c", "deferred Source commands execute FIFO after terminal dispatch");
        report.check(f.trace.size() >= 3 && f.trace[1] == "completed:1" && f.trace[2] == "navigate:2", "no replacement starts between loading=false and Completed(A)");
    }
    {
        Driver f(106);
        f.ready();
        f.commit(L"https://example.test/a");
        f.finish();
        bool froze = false;
        bool once = false;
        f.view->PropertyChanged += ref new PropertyChangedEventHandler([&](Object^, PropertyChangedEventArgs^ args) {
            if (args->PropertyName == L"Source" && !once) {
                once = true;
                auto published = f.view->Source;
                f.view->Source = target(L"https://example.test/c");
                f.view->Source = published;
                froze = source(f.view) == L"https://example.test/b" && dpMatches(f.view);
            }
        });
        f.view->Source = target(L"https://example.test/b");
        report.check(froze && f.targets.size() == 4 && f.targets[2] == L"https://example.test/c" && f.targets[3] == L"https://example.test/b", "FIFO revalidation preserves B,C,B reentrant intent even when last setter equals frozen DP");
    }
    {
        Driver f(107);
        f.ready();
        f.commit(L"https://example.test/a");
        unsigned owed = 0;
        bool closingRejected = false;
        f.view->PropertyChanged += ref new PropertyChangedEventHandler([&](Object^, PropertyChangedEventArgs^ args) {
            if (args->PropertyName == L"IsLoading" && !f.view->IsLoading) {
                f.view->Close();
                closingRejected = failsWith([&] { f.view->Source = nullptr; }, RO_E_CLOSED);
            }
        });
        f.view->NavigationCompleted += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationCompletedEventArgs^>(
            [&](WebKitWebView^, WebKitNavigationCompletedEventArgs^) { ++owed; });
        f.finish();
        report.check(owed == 1 && f.closes == 1, "Close in PropertyChanged preserves owed terminal before cleanup");
        report.check(closingRejected, "Closing Source mutation fails before dispatch unwind");
        f.view->Close();
        report.check(f.closes == 1 && source(f.view) == L"https://example.test/a" && !f.view->IsLoading, "repeated Close preserves final readable snapshot");
        report.check(failsWith([&] { f.view->Source = nullptr; }, RO_E_CLOSED), "closed Source mutation reports RO_E_CLOSED");
    }
    {
        Driver f(108), other(109);
        f.ready(); other.ready();
        f.commit(L"https://example.test/one", L"One");
        other.commit(L"https://example.test/two", L"Two");
        f.view->Close();
        report.check(other.view->IsLoading && source(other.view) == L"https://example.test/two" && other.view->DocumentTitle == L"Two", "closing one control preserves interleaved peer snapshot");
        other.finish();
        report.check(other.completions == 1 && !other.view->IsLoading, "surviving peer completes normally");
    }
    {
        Driver f(110);
        f.ready();
        f.commit(L"https://example.test/a");
        auto before = f.view->HandlerFailures();
        f.view->PropertyChanged += ref new PropertyChangedEventHandler([](Object^, PropertyChangedEventArgs^) { throw Exception::CreateException(E_ACCESSDENIED); });
        f.finish();
        report.check(f.completions == 1 && !f.view->IsLoading && f.view->HandlerFailures() > before, "property handler failure is diagnostic and cannot erase terminal");
    }
    {
        Driver f(111, primary);
        bool historyCoherent = false;
        bool titleCoherent = false;
        f.view->HistoryChanged += ref new TypedEventHandler<WebKitWebView^, WebKitHistoryChangedEventArgs^>(
            [&](WebKitWebView^, WebKitHistoryChangedEventArgs^ args) { historyCoherent = args->CanGoBack == f.view->CanGoBack && args->CanGoForward == f.view->CanGoForward; });
        f.view->DocumentTitleChanged += ref new TypedEventHandler<WebKitWebView^, WebKitDocumentTitleChangedEventArgs^>(
            [&](WebKitWebView^, WebKitDocumentTitleChangedEventArgs^ args) { titleCoherent = args->DocumentTitle == f.view->DocumentTitle; });
        f.ready();
        f.commit(L"https://example.test/bound", L"Bound committed title");
        f.finish();
        f.back = true;
        auto history = f.event(WKUWPEventHistoryChanged);
        history.navigationId = 0;
        f.view->HandleNotification(history);
        auto titled = f.event(WKUWPEventTitleChanged);
        auto titleBytes = bytes(L"Bound updated title");
        titled.title = { reinterpret_cast<const uint8_t*>(titleBytes.data()), static_cast<uint32_t>(titleBytes.size()) };
        f.view->HandleNotification(titled);
        report.check(historyCoherent && titleCoherent && f.view->CanGoBack && f.view->DocumentTitle == L"Bound updated title", "history/title snapshots precede typed events");
        auto provider = safe_cast<ICustomPropertyProvider^>(f.view);
        report.check(provider->GetCustomProperty(L"Source")->CanWrite && !provider->GetCustomProperty(L"IsLoading")->CanWrite
                && !provider->GetCustomProperty(L"DocumentTitle")->CanWrite && !provider->GetCustomProperty(L"Unknown"),
            "runtime property discovery enforces read-only state");
        report.check(titleBinding->Text == L"Bound updated title" && backBinding->IsEnabled, "loaded XAML bindings observe title and history notifications");
    }
}

} // namespace

// Diagnostic-only native entry: run on the XAML owner and save a fail-closed report.
extern "C" __declspec(dllexport) HRESULT __stdcall WKUWPTestSourceState(IInspectable* control, IInspectable* title, IInspectable* back, const wchar_t* path)
{
    Report report;
    try {
        run(report, safe_cast<WebKitWebView^>(reinterpret_cast<Object^>(control)),
            safe_cast<TextBlock^>(reinterpret_cast<Object^>(title)), safe_cast<Button^>(reinterpret_cast<Object^>(back)));
    } catch (Exception^ exception) {
        report.check(false, "uncaught test exception");
        report.lines += "HRESULT=" + std::to_string(exception->HResult) + "\r\n";
    } catch (...) {
        report.check(false, "uncaught native test exception");
    }
    char header[256];
    sprintf_s(header, "version=" TEST_PACKAGE_VERSION "\r\nresult=%s\r\nchecks=%u\r\nfailures=%u\r\n", report.failures ? "FAIL" : "PASS", report.checks, report.failures);
    report.lines.insert(0, header);
    auto file = CreateFile2(path, GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, nullptr);
    if (file == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());
    DWORD written = 0;
    auto saved = WriteFile(file, report.lines.data(), static_cast<DWORD>(report.lines.size()), &written, nullptr);
    FlushFileBuffers(file);
    CloseHandle(file);
    if (!saved || written != report.lines.size()) return E_FAIL;
    return report.failures ? E_FAIL : S_OK;
}
