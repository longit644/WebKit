// Diagnostic-only frozen-table tests of actual copied CX event/token objects.
#include "WebKitWebView.h"

#include <algorithm>
#include <cstdio>
#include <inspectable.h>
#include <string>
#include <vector>

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::UI::Xaml::Data;
using namespace WebKit;

namespace {

// Aggregate every real-control assertion; one failed assertion fails this versioned run.
struct EventReport {
    // Assertions evaluated through the frozen callback table.
    unsigned checks { 0 };
    // Failure count retained independently of later successful assertions.
    unsigned failures { 0 };
    // ASCII per-assertion result lines.
    std::string lines;
    // Append a fail-closed assertion outcome.
    void check(bool value, const char* name)
    {
        ++checks;
        if (!value) ++failures;
        lines += value ? "PASS: " : "FAIL: ";
        lines += name;
        lines += "\r\n";
    }
};

// Encode owned fixture strings into borrowed V1 spans.
std::string eventBytes(const std::wstring& text)
{
    if (text.empty()) return { };
    auto length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), &result[0], length, nullptr, nullptr);
    return result;
}

// Verify exact owner-thread exception results, not arbitrary failures.
template<typename F> bool eventThrows(F operation, HRESULT expected)
{
    try { operation(); }
    catch (Exception^ exception) { return exception->HResult == expected; }
    return false;
}

// Per-page driver using retained WKW10MPageCallbacks rather than private dispatch calls.
struct EventDriver {
    // Real XAML/CX control supplying all public event objects.
    WebKitWebView^ view { ref new WebKitWebView() };
    // Engine-owned copied callback table with balanced context retain/release.
    WKW10MPageCallbacks callbacks { };
    // Distinct page identity for callback routing and token binding.
    uint64_t page;
    // Monotonic main ID allocated only by this controlled provider.
    uint64_t nextId { 0 };
    // Current started main navigation, or zero after terminal emission.
    uint64_t active { 0 };
    // Committed document generation.
    uint64_t generation { 0 };
    // Strict callback ordering across all event kinds.
    uint64_t sequence { 0 };
    // Current navigation's fixed Remote/HostHtml content policy.
    uint32_t kind { WKW10MContentRemote };
    // Last proposal target, separate from the control's restored Source.
    std::wstring target;
    // Copied last public completion retained after provider buffer mutation.
    WebKitNavigationCompletedEventArgs^ completed;
    // Copied last synchronous proposal retained to check late Cancel rejection.
    WebKitNavigationStartingEventArgs^ starting;
    // Copied last committed URI attribution.
    WebKitSourceChangedEventArgs^ changed;
    // Copied last document-title notification.
    WebKitDocumentTitleChangedEventArgs^ titled;
    // Last original policy result for explicit handler-error mapping.
    WKW10MResult policyResult { WKW10MResultOK };
    // Public completions observed, proving duplicate/stale delivery suppression.
    unsigned completions { 0 };
    // Ordered property/typed-event observations for transaction assertions.
    std::vector<std::string> trace;

    // Bind/retain the real frozen callback attachment and resolve controlled readiness.
    explicit EventDriver(uint64_t identity) : page(identity)
    {
        view->SetCommandHandler(&command, this);
        if (FAILED(view->BuildPageCallbacks(&callbacks))) throw Exception::CreateException(E_FAIL);
        callbacks.retainContext(callbacks.context);
        view->PropertyChanged += ref new PropertyChangedEventHandler([this](Object^, PropertyChangedEventArgs^ args) {
            if (args->PropertyName == L"Source") trace.push_back("property:source");
            if (args->PropertyName == L"IsLoading") trace.push_back(view->IsLoading ? "property:loading=true" : "property:loading=false");
            if (args->PropertyName == L"DocumentTitle") trace.push_back("property:title");
            if (args->PropertyName == L"CanGoBack") trace.push_back("property:back");
        });
        view->NavigationStarting += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationStartingEventArgs^>(
            [this](WebKitWebView^, WebKitNavigationStartingEventArgs^ args) { starting = args; trace.push_back("starting"); });
        view->NavigationCompleted += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationCompletedEventArgs^>(
            [this](WebKitWebView^, WebKitNavigationCompletedEventArgs^ args) { completed = args; ++completions; trace.push_back("completed"); });
        view->SourceChanged += ref new TypedEventHandler<WebKitWebView^, WebKitSourceChangedEventArgs^>(
            [this](WebKitWebView^, WebKitSourceChangedEventArgs^ args) { changed = args; trace.push_back("source-changed"); });
        view->DocumentTitleChanged += ref new TypedEventHandler<WebKitWebView^, WebKitDocumentTitleChangedEventArgs^>(
            [this](WebKitWebView^, WebKitDocumentTitleChangedEventArgs^ args) { titled = args; trace.push_back("title-changed"); });
        auto ready = view->EnsureInitializedAsync();
        view->CompleteInitialization(view->InitializationAttemptId(), page, S_OK);
        ready->GetResults();
    }

    // Stop fixture delivery before local subscriber captures expire; release engine ownership.
    ~EventDriver()
    {
        static_cast<PageCallbacksW10M*>(callbacks.context)->deactivate();
        view->SetCommandHandler(nullptr, nullptr);
        view->Close();
        callbacks.releaseContext(callbacks.context);
    }

    // Build one explicit frozen V1 signal with this page's monotonic identifiers.
    WKW10MPageEvent packet(uint32_t type)
    {
        WKW10MPageEvent event { };
        event.header = { sizeof(event), WKW10M_STRUCTURE_VERSION };
        event.pageId = page;
        event.callbackSequence = ++sequence;
        event.navigationId = active;
        event.documentGeneration = generation;
        event.eventKind = type;
        event.contentKind = kind;
        event.isLoading = !!active;
        return event;
    }

    // Propose a document through the actual retained synchronous policy entry.
    void start(const wchar_t* uri, uint32_t content = WKW10MContentRemote)
    {
        if (active) finish(WKW10MOutcomeCancelled);
        target = uri;
        kind = content;
        active = ++nextId;
        auto event = packet(WKW10MEventStarting);
        auto bytes = eventBytes(target);
        event.uri = { reinterpret_cast<const uint8_t*>(bytes.data()), static_cast<uint32_t>(bytes.size()) };
        uint32_t cancel = 0;
        policyResult = callbacks.starting(callbacks.context, &event, &cancel);
        std::fill(bytes.begin(), bytes.end(), '!');
        if (cancel) finish(WKW10MOutcomeCancelled);
        else if (policyResult != WKW10MResultOK) finish(WKW10MOutcomeFailed, WKW10MErrorDomainPlatform, policyResult);
    }

    // Emit a terminal with controlled domain/status/token data and scramble borrowed bytes.
    void finish(uint32_t outcome = WKW10MOutcomeCompleted, uint32_t domain = WKW10MErrorDomainNone, int code = 0,
        bool hasStatus = false, uint32_t status = 0, WKW10MCertificateToken token = { 0, 0 })
    {
        if (!active) return;
        auto event = packet(WKW10MEventCompleted);
        event.outcome = outcome;
        event.errorDomain = domain;
        event.errorCode = code;
        event.hasHttpStatus = hasStatus;
        event.httpStatus = status;
        event.certificateToken = token;
        event.isLoading = 0;
        auto bytes = eventBytes(target);
        event.uri = { reinterpret_cast<const uint8_t*>(bytes.data()), static_cast<uint32_t>(bytes.size()) };
        active = 0;
        callbacks.notify(callbacks.context, &event);
        std::fill(bytes.begin(), bytes.end(), '!');
    }

    // Emit an actual commit with copied UTF8 title/history state before terminal delivery.
    void commit(const wchar_t* uri, const wchar_t* title, uint32_t change = WKW10MChangeNewDocument)
    {
        ++generation;
        auto event = packet(WKW10MEventSourceChanged);
        event.changeKind = change;
        event.canGoBack = 1;
        auto uriBytes = eventBytes(uri), titleBytes = eventBytes(title);
        event.uri = { reinterpret_cast<const uint8_t*>(uriBytes.data()), static_cast<uint32_t>(uriBytes.size()) };
        event.title = { reinterpret_cast<const uint8_t*>(titleBytes.data()), static_cast<uint32_t>(titleBytes.size()) };
        callbacks.notify(callbacks.context, &event);
        std::fill(uriBytes.begin(), uriBytes.end(), '!');
        std::fill(titleBytes.begin(), titleBytes.end(), '!');
    }

    // Execute real-control queued work without deriving any event from diagnostic text.
    static HRESULT __stdcall command(void* context, SourceActionW10M action, const WKW10MString* uri)
    {
        auto& self = *static_cast<EventDriver*>(context);
        if (action == SourceActionW10M::Initialize) return S_OK;
        if (action == SourceActionW10M::Close || action == SourceActionW10M::Stop) { self.finish(WKW10MOutcomeCancelled); return S_OK; }
        if (action != SourceActionW10M::Navigate) return E_INVALIDARG;
        auto length = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(uri->data), static_cast<int>(uri->length), nullptr, 0);
        std::wstring target(length, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(uri->data), static_cast<int>(uri->length), &target[0], length);
        self.start(target.c_str());
        return S_OK;
    }
};

// Check a trace position without accepting unordered/property-after-event observations.
size_t position(const std::vector<std::string>& trace, const char* item)
{
    return static_cast<size_t>(std::find(trace.begin(), trace.end(), item) - trace.begin());
}

// Verify copied fields, policy HRESULT mapping, token binding and actual transaction traces.
void runEvents(EventReport& report)
{
    {
        EventDriver f(301);
        report.check(f.callbacks.header.structureSize == sizeof(WKW10MPageCallbacks) && f.callbacks.header.structureVersion == 1
                && f.callbacks.retainContext && f.callbacks.releaseContext && f.callbacks.starting && f.callbacks.notify,
            "retained frozen V1 table has all ownership/policy/notification entries");
        f.start(L"https://example.test/proposal");
        report.check(f.starting->NavigationId == 1 && f.starting->Uri->AbsoluteUri == L"https://example.test/proposal"
                && f.starting->ContentKind == WebKitNavigationContentKind::Remote && f.view->IsLoading,
            "Starting copies ID URI Remote kind before provider buffers are scrambled");
        report.check(eventThrows([&] { f.starting->Cancel = true; }, RO_E_CLOSED), "late Cancel write fails RO_E_CLOSED after synchronous dispatch");
        f.commit(L"https://example.test/committed", L"Copied \u00e9 \u4e2d title", WKW10MChangeHistoryTraversal);
        report.check(f.changed->Uri->AbsoluteUri == L"https://example.test/committed" && f.changed->ChangeKind == WebKitSourceChangeKind::HistoryTraversal
                && f.changed->IsNewDocument && f.view->DocumentTitle == L"Copied \u00e9 \u4e2d title", "commit copies UTF8 data and generation-derived new-document traversal flag");
        report.check(position(f.trace, "property:source") < position(f.trace, "source-changed")
                && position(f.trace, "property:title") < position(f.trace, "source-changed") && position(f.trace, "property:back") < position(f.trace, "source-changed"),
            "source title history properties precede SourceChanged in the trace");
        f.finish(WKW10MOutcomeCompleted, WKW10MErrorDomainNone, 0, true, 404);
        report.check(f.completed->Outcome == WebKitNavigationOutcome::Completed && f.completed->ErrorDomain == WebKitNavigationErrorDomain::None
                && !f.completed->ErrorCode && f.completed->HasHttpStatus && f.completed->HttpStatus == 404, "HTTP status is explicit and 404 does not fabricate transport failure");
        report.check(position(f.trace, "property:loading=false") < position(f.trace, "completed") && !f.view->IsLoading,
            "loading=false PropertyChanged precedes the owed Completed event");
        report.check(!f.completed->CertificateFailureToken && !f.completed->CanProceedWithCertificateFailure, "ordinary completion carries no certificate capability");
        auto old = f.packet(WKW10MEventCompleted);
        old.navigationId = 1;
        f.callbacks.notify(f.callbacks.context, &old);
        report.check(f.completions == 1 && f.view->LastPageCallbackResult() == S_FALSE, "duplicate terminal is absorbed without second public completion");
    }
    {
        EventDriver f(302);
        for (uint32_t domain : { WKW10MErrorDomainTransport, WKW10MErrorDomainNetwork, WKW10MErrorDomainEngine, WKW10MErrorDomainPlatform }) {
            f.start(L"https://example.test/failure");
            auto code = domain == WKW10MErrorDomainPlatform ? static_cast<int>(E_ACCESSDENIED) : 1;
            f.finish(WKW10MOutcomeFailed, domain, code, false, 999);
            auto expected = domain == WKW10MErrorDomainTransport ? WebKitNavigationErrorDomain::Transport
                : domain == WKW10MErrorDomainNetwork ? WebKitNavigationErrorDomain::Network
                : domain == WKW10MErrorDomainEngine ? WebKitNavigationErrorDomain::Engine : WebKitNavigationErrorDomain::Platform;
            report.check(f.completed->Outcome == WebKitNavigationOutcome::Failed && f.completed->ErrorDomain == expected && f.completed->ErrorCode == code,
                "domain is mapped explicitly and original code or HRESULT bits are retained");
            report.check(!f.completed->HasHttpStatus && !f.completed->HttpStatus && f.completed->Uri->AbsoluteUri == L"https://example.test/failure",
                "absent HTTP status is zero and terminal URI remains related target after Source restoration");
        }
    }
    {
        EventDriver f(303);
        f.start(L"about:blank", WKW10MContentHostHtml);
        report.check(f.starting->ContentKind == WebKitNavigationContentKind::HostHtml && f.starting->Uri->AbsoluteUri == L"about:blank", "HostHtml Starting preserves explicit content policy and blank target");
        f.commit(L"about:blank", L"Host title");
        f.finish();
        report.check(f.completed->ContentKind == WebKitNavigationContentKind::HostHtml && !f.completed->HasHttpStatus && !f.completed->HttpStatus
                && !f.completed->CertificateFailureToken, "HostHtml terminal has no implicit HTTP status or certificate decision");
    }
    {
        EventDriver a(304), b(305);
        a.start(L"https://example.test/certificate"); b.start(L"https://example.test/certificate");
        a.finish(WKW10MOutcomeFailed, WKW10MErrorDomainTransport, 60, false, 0, { 7001, 1 });
        b.finish(WKW10MOutcomeFailed, WKW10MErrorDomainTransport, 60, false, 0, { 7002, 1 });
        auto ta = a.completed->CertificateFailureToken, tb = b.completed->CertificateFailureToken;
        report.check(ta && tb && ta != tb && ta->FailedNavigationId == 1 && tb->FailedNavigationId == 1
                && ta->FailedUri->AbsoluteUri == L"https://example.test/certificate", "equal-local-ID failures issue distinct immutable diagnostic token objects");
        report.check(a.view->IsCurrentCertificateToken(ta) && b.view->IsCurrentCertificateToken(tb)
                && !a.view->IsCurrentCertificateToken(tb) && !b.view->IsCurrentCertificateToken(ta), "hidden object control issuer revision binding rejects cross-issued capabilities");
        report.check(a.completed->CanProceedWithCertificateFailure && b.completed->CanProceedWithCertificateFailure,
            "CanProceed is derived from copied token presence at argument creation");
        report.check(eventThrows([&] { a.view->Source = ref new Uri(L"file:///invalid"); }, E_INVALIDARG) && a.view->IsCurrentCertificateToken(ta),
            "invalid argument rejection has no current-token side effect");
        a.view->Source = nullptr;
        report.check(!a.view->IsCurrentCertificateToken(ta) && a.completed->CanProceedWithCertificateFailure,
            "Source-null invalidation preserves immutable old completion token-presence snapshot");
        b.view->Close();
        report.check(!b.view->IsCurrentCertificateToken(tb) && tb->FailedNavigationId == 1, "Close invalidates consent while retained token diagnostics stay readable");
    }
    {
        EventDriver f(306);
        f.view->NavigationStarting += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationStartingEventArgs^>(
            [](WebKitWebView^, WebKitNavigationStartingEventArgs^) { throw Exception::CreateException(E_ACCESSDENIED); });
        f.start(L"https://example.test/throw");
        report.check(f.policyResult == static_cast<WKW10MResult>(E_ACCESSDENIED) && f.completed->Outcome == WebKitNavigationOutcome::Failed
                && f.completed->ErrorDomain == WebKitNavigationErrorDomain::Platform && f.completed->ErrorCode == E_ACCESSDENIED,
            "Starting handler error returns unchanged HRESULT and maps to Failed Platform completion");
        report.check(f.view->HandlerFailures() == 1 && eventThrows([&] { f.starting->Cancel = false; }, RO_E_CLOSED),
            "failed Starting still freezes Cancel and records a contained handler failure");
    }
    {
        EventDriver f(307);
        f.view->NavigationStarting += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationStartingEventArgs^>(
            [](WebKitWebView^, WebKitNavigationStartingEventArgs^ args) { args->Cancel = true; throw Exception::CreateException(E_ACCESSDENIED); });
        f.start(L"https://example.test/cancel-and-throw");
        report.check(f.policyResult == WKW10MResultOK && f.completed->Outcome == WebKitNavigationOutcome::Cancelled
                && f.completed->ErrorDomain == WebKitNavigationErrorDomain::None, "explicit Cancel takes precedence over simultaneous handler failure");
    }
    {
        EventDriver f(308);
        f.view->NavigationStarting += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationStartingEventArgs^>(
            [&f](WebKitWebView^, WebKitNavigationStartingEventArgs^) { f.view->Close(); throw Exception::CreateException(E_ACCESSDENIED); });
        f.start(L"https://example.test/close-and-throw");
        report.check(f.completed && f.completed->Outcome == WebKitNavigationOutcome::Cancelled && f.completions == 1,
            "Close during Starting preserves one owed cancellation before retained table deactivation");
        auto late = f.packet(WKW10MEventTitleChanged);
        f.callbacks.notify(f.callbacks.context, &late);
        report.check(f.completions == 1 && f.view->DocumentTitle->IsEmpty(), "retained native context safely suppresses post-close late delivery");
    }
    {
        EventDriver f(309);
        f.start(L"https://example.test/notification-errors");
        f.view->SourceChanged += ref new TypedEventHandler<WebKitWebView^, WebKitSourceChangedEventArgs^>(
            [](WebKitWebView^, WebKitSourceChangedEventArgs^) { throw Exception::CreateException(E_FAIL); });
        f.view->NavigationCompleted += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationCompletedEventArgs^>(
            [](WebKitWebView^, WebKitNavigationCompletedEventArgs^) { throw Exception::CreateException(E_ACCESSDENIED); });
        f.commit(L"https://example.test/notification-errors", L"Retained title");
        f.finish();
        report.check(f.view->LastPageCallbackResult() == S_OK && f.view->HandlerFailures() == 2
                && f.completed->Outcome == WebKitNavigationOutcome::Completed && f.completions == 1,
            "nonpolicy subscriber errors stay diagnostic without changing or retrying terminal delivery");
    }
    {
        EventDriver f(310);
        f.start(L"http://example.test/noneligible");
        auto malformed = f.packet(WKW10MEventCompleted);
        malformed.outcome = WKW10MOutcomeFailed;
        malformed.errorDomain = WKW10MErrorDomainTransport;
        malformed.errorCode = 60;
        malformed.certificateToken = { 8001, 1 };
        auto bytes = eventBytes(L"http://example.test/noneligible");
        malformed.uri = { reinterpret_cast<const uint8_t*>(bytes.data()), static_cast<uint32_t>(bytes.size()) };
        f.callbacks.notify(f.callbacks.context, &malformed);
        report.check(f.view->LastPageCallbackResult() == E_INVALIDARG && !f.completions && f.view->IsLoading,
            "HTTP certificate-token payload is rejected before terminal/token mutation");
        malformed.certificateToken = { 8001, 0 };
        malformed.callbackSequence = ++f.sequence;
        f.callbacks.notify(f.callbacks.context, &malformed);
        report.check(f.view->LastPageCallbackResult() == E_INVALIDARG && !f.completions,
            "partial issuer decision pair fails closed");
        f.finish(WKW10MOutcomeFailed, WKW10MErrorDomainTransport, 60);
        report.check(f.completions == 1 && !f.completed->CertificateFailureToken, "valid noneligible failure still carries transport code without consent");
    }
    {
        EventDriver f(311);
        f.start(L"https://example.test/bad-span");
        auto malformed = f.packet(WKW10MEventTitleChanged);
        malformed.title = { nullptr, 4 };
        f.callbacks.notify(f.callbacks.context, &malformed);
        report.check(f.view->LastPageCallbackResult() == E_INVALIDARG && f.view->DocumentTitle->IsEmpty(), "malformed borrowed span is contained before publishing state");
        auto foreign = f.packet(WKW10MEventHistoryChanged);
        foreign.pageId = 999;
        foreign.canGoBack = 1;
        f.callbacks.notify(f.callbacks.context, &foreign);
        report.check(f.view->LastPageCallbackResult() == S_FALSE && !f.view->CanGoBack, "frozen table rejects foreign-page state despite valid scalar layout");
        f.finish();
    }
    {
        EventDriver f(312);
        bool frozen = false;
        f.view->DocumentTitleChanged += ref new TypedEventHandler<WebKitWebView^, WebKitDocumentTitleChangedEventArgs^>(
            [&f, &frozen](WebKitWebView^, WebKitDocumentTitleChangedEventArgs^ args) {
                if (args->DocumentTitle != L"First") return;
                auto next = f.packet(WKW10MEventTitleChanged);
                auto bytes = eventBytes(L"Copied deferred title");
                next.title = { reinterpret_cast<const uint8_t*>(bytes.data()), static_cast<uint32_t>(bytes.size()) };
                f.callbacks.notify(f.callbacks.context, &next);
                std::fill(bytes.begin(), bytes.end(), '!');
                frozen = f.view->DocumentTitle == L"First";
            });
        auto first = f.packet(WKW10MEventTitleChanged);
        auto bytes = eventBytes(L"First");
        first.title = { reinterpret_cast<const uint8_t*>(bytes.data()), static_cast<uint32_t>(bytes.size()) };
        f.callbacks.notify(f.callbacks.context, &first);
        report.check(frozen && f.view->DocumentTitle == L"Copied deferred title" && f.titled->DocumentTitle == L"Copied deferred title",
            "reentrant native signal keeps frozen getter and owns deferred bytes after provider scrambling");
    }
}

} // namespace

// Diagnostic-only native entry: run real retained-table events on the XAML owner.
extern "C" __declspec(dllexport) HRESULT __stdcall WKW10MTestEventCallbacks(const wchar_t* path)
{
    EventReport report;
    try { runEvents(report); }
    catch (Exception^ exception) {
        report.check(false, "uncaught event callback exception");
        report.lines += "HRESULT=" + std::to_string(exception->HResult) + "\r\n";
    } catch (...) { report.check(false, "uncaught native event callback exception"); }
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
