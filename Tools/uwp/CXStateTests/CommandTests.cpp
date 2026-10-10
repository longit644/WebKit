// Diagnostic-only provider of the frozen page table. Assertions exercise actual
// public commands, copied callback objects and request spans on the real control.
#include "WebKitWebView.h"

#include <cstdio>
#include <inspectable.h>
#include <map>
#include <string>
#include <vector>

using namespace Platform;
using namespace Windows::Foundation;
using namespace WebKit;

namespace {

// Fail-closed per-command assertions saved with the diagnostic package version.
struct CommandReport {
    // Number of assertions evaluated on the real public control.
    unsigned checks { 0 };
    // Failure count retained independently of subsequent passes.
    unsigned failures { 0 };
    // Complete ASCII assertion result lines.
    std::string lines;
    // Record one assertion without masking earlier failure.
    void check(bool value, const char* name)
    {
        ++checks;
        if (!value) ++failures;
        lines += value ? "PASS: " : "FAIL: ";
        lines += name;
        lines += "\r\n";
    }
};

// Real Foundation URI constructor used by the public Source setter.
Uri^ commandUri(const wchar_t* value) { return ref new Uri(ref new String(value)); }

// Exact WinRT error oracle for invalid/consumed/wrong-control capability arguments.
template<typename F> bool commandThrows(F operation, HRESULT expected)
{
    try { operation(); }
    catch (Exception^ exception) { return exception->HResult == expected; }
    return false;
}

// Copy borrowed frozen UTF8 request/event spans by explicit byte length.
std::string copiedSpan(const WKUWPString& span)
{
    return span.length ? std::string(reinterpret_cast<const char*>(span.data), span.length) : std::string();
}

// Forward declaration of the diagnostic-only provider page registry entry.
struct CommandPage;
// Per-page provider routing required by context-free frozen table signatures.
std::map<uint64_t, CommandPage*> commandPages;

// Deterministic page/history/token provider; no such history vector lives in the control.
struct CommandPage {
    // Real public XAML/CX control under test.
    WebKitWebView^ view { ref new WebKitWebView() };
    // Frozen copied engine table bound through the production native controller.
    WKUWPEngineAPI api { };
    // Provider-owned retained callback attachment.
    WKUWPPageCallbacks callbacks { };
    // Opaque page identity used for all table operations.
    uint64_t page;
    // Monotonic main IDs, allocated by this provider rather than the control.
    uint64_t nextId { 0 };
    // Current main ID awaiting terminal completion.
    uint64_t active { 0 };
    // Committed document generation and callback sequence.
    uint64_t generation { 0 }, sequence { 0 };
    // Fixed active content policy copied into V1 events.
    uint32_t kind { WKUWPContentRemote };
    // Current proposed URI and actual committed provider URI.
    std::string target, committed;
    // Provider-owned remote history entries; host HTML never enters this list.
    std::vector<std::string> history;
    // Provider's current remote history cursor, unchanged by host markup.
    size_t cursor { 0 };
    // Current provider host text, preserving embedded NULs by length.
    std::string hostText;
    // Counters distinguishing remote, HTML, history, reload, stop and consent work.
    unsigned remoteCalls { 0 }, htmlCalls { 0 }, backCalls { 0 }, forwardCalls { 0 }, reloadCalls { 0 }, stopCalls { 0 }, retryCalls { 0 };
    // Public terminal events observed on the real control.
    unsigned completions { 0 };
    // Last copied public completion and policy arguments.
    WebKitNavigationCompletedEventArgs^ completed;
    WebKitNavigationStartingEventArgs^ starting;
    // Pending failure decision and monotonically increasing failure revision.
    WKUWPCertificateToken decision { 0, 0 };
    uint64_t revision { 0 };
    // Exact failed HTTPS target to which the pending decision is bound.
    std::string failedTarget;
    // Controlled next remote load fails with a structured certificate decision.
    bool failCertificate { false };
    // A validated provider retry arms only its own exact stored target.
    bool armed { false };
    // Native operations/results used as explicit request-shape oracles.
    bool requestShape { true }, attached { false };
    // Warning pair most recently passed through loadHtml.
    WKUWPCertificateToken warningSeen { 0, 0 };

    // Construct a distinct provider page and bind its actual frozen table/retained callbacks.
    explicit CommandPage(uint64_t id) : page(id)
    {
        commandPages[page] = this;
        api.header = { sizeof(api), WKUWP_STRUCTURE_VERSION };
        api.abiMajor = WKUWP_ABI_MAJOR_VERSION;
        api.abiMinor = WKUWP_ABI_MINOR_VERSION;
        api.capabilities = WKUWPCapabilityStructuredNotifications | WKUWPCapabilityHostHtml | WKUWPCapabilityCertificateDecisions;
        api.setCallbacks = &setCallbacks;
        api.navigate = &navigate;
        api.loadHtml = &loadHtml;
        api.goBack = &back;
        api.goForward = &forward;
        api.reload = &reload;
        api.stop = &stop;
        api.proceedWithCertificateFailure = &proceed;
        api.closePage = &close;
        view->SetCommandHandler(&initialize, this);
        auto observation = view->EnsureInitializedAsync();
        if (FAILED(view->BindPageController(&api, page))) throw Exception::CreateException(E_FAIL);
        view->CompleteInitialization(view->InitializationAttemptId(), page, S_OK);
        observation->GetResults();
        view->NavigationCompleted += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationCompletedEventArgs^>(
            [this](WebKitWebView^, WebKitNavigationCompletedEventArgs^ args) { completed = args; ++completions; });
        view->NavigationStarting += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationStartingEventArgs^>(
            [this](WebKitWebView^, WebKitNavigationStartingEventArgs^ args) { starting = args; });
    }

    // Close the fixture while its provider/context is alive, then erase per-page routing.
    ~CommandPage()
    {
        if (attached) static_cast<PageCallbacksUWP*>(callbacks.context)->deactivate();
        view->Close();
        if (attached) {
            callbacks.releaseContext(callbacks.context);
            attached = false;
        }
        commandPages.erase(page);
    }

    // Retrieve only the exact known provider page from the test registry.
    static CommandPage* find(uint64_t page)
    {
        auto entry = commandPages.find(page);
        return entry == commandPages.end() ? nullptr : entry->second;
    }

    // Build a current explicit V1 signal with actual provider history availability.
    WKUWPPageEvent event(uint32_t type)
    {
        WKUWPPageEvent value { };
        value.header = { sizeof(value), WKUWP_STRUCTURE_VERSION };
        value.pageId = page;
        value.callbackSequence = ++sequence;
        value.navigationId = active;
        value.documentGeneration = generation;
        value.eventKind = type;
        value.contentKind = kind;
        value.canGoBack = !history.empty() && cursor > 0;
        value.canGoForward = !history.empty() && cursor + 1 < history.size();
        value.isLoading = !!active;
        value.uri = { reinterpret_cast<const uint8_t*>(target.data()), static_cast<uint32_t>(target.size()) };
        return value;
    }

    // Emit one terminal before provider buffer/active state can be reused by reentrant work.
    void finish(uint32_t outcome = WKUWPOutcomeCompleted, uint32_t domain = WKUWPErrorDomainNone, int code = 0, WKUWPCertificateToken token = { 0, 0 })
    {
        if (!active) return;
        auto value = event(WKUWPEventCompleted);
        auto related = target;
        value.uri = { reinterpret_cast<const uint8_t*>(related.data()), static_cast<uint32_t>(related.size()) };
        value.outcome = outcome;
        value.errorDomain = domain;
        value.errorCode = code;
        value.certificateToken = token;
        value.isLoading = 0;
        active = 0;
        callbacks.notify(callbacks.context, &value);
    }

    // Allocate a fresh proposal and honor the real public Starting verdict/HRESULT.
    bool begin(std::string uri, uint32_t content, uint64_t* id)
    {
        finish(WKUWPOutcomeCancelled);
        target = std::move(uri);
        kind = content;
        active = ++nextId;
        if (id) *id = active;
        auto value = event(WKUWPEventStarting);
        uint32_t cancel = 0;
        auto result = callbacks.starting(callbacks.context, &value, &cancel);
        if (cancel) { finish(WKUWPOutcomeCancelled); return false; }
        if (result != WKUWPResultOK) { finish(WKUWPOutcomeFailed, WKUWPErrorDomainPlatform, result); return false; }
        return true;
    }

    // Commit actual provider content without requiring control-owned shadow history.
    void commit()
    {
        ++generation;
        committed = target;
        auto value = event(WKUWPEventSourceChanged);
        callbacks.notify(callbacks.context, &value);
        finish();
    }

    // Controlled shared initialization records readiness separately from all page commands.
    static HRESULT __stdcall initialize(void*, SourceActionUWP action, const WKUWPString*)
    {
        return action == SourceActionUWP::Initialize ? S_OK : E_NOTIMPL;
    }

    // Provider attachment retains the exact copied callback context once.
    static WKUWPResult __stdcall setCallbacks(uint64_t page, const WKUWPPageCallbacks* table)
    {
        auto self = find(page);
        if (!self || !table || self->attached) return WKUWPResultInvalidArgument;
        self->callbacks = *table;
        table->retainContext(table->context);
        self->attached = true;
        return WKUWPResultOK;
    }

    // Load an actual Remote-shaped request; only accepted commits change provider history.
    static WKUWPResult __stdcall navigate(uint64_t page, const WKUWPNavigationRequest* request, uint64_t* id)
    {
        auto self = find(page);
        if (!self || !request || !id) return WKUWPResultInvalidArgument;
        ++self->remoteCalls;
        self->requestShape = self->requestShape && request->header.structureSize == sizeof(*request)
            && request->header.structureVersion == 1 && request->contentKind == WKUWPContentRemote
            && request->uri.length && !request->html.length && !request->warningToken.issuer && !request->warningToken.decision;
        self->decision = { 0, 0 };
        if (!self->begin(copiedSpan(request->uri), WKUWPContentRemote, id)) return WKUWPResultOK;
        if (self->failCertificate) {
            self->failCertificate = false;
            self->failedTarget = self->target;
            self->decision = { 9000 + page, ++self->revision };
            self->finish(WKUWPOutcomeFailed, WKUWPErrorDomainTransport, 60, self->decision);
            return WKUWPResultOK;
        }
        if (!self->history.empty()) self->history.resize(self->cursor + 1);
        self->history.push_back(self->target);
        self->cursor = self->history.size() - 1;
        self->hostText.clear();
        self->commit();
        return WKUWPResultOK;
    }

    // Submit isolated HostHtml bytes with empty URI; an associated pair must match exactly.
    static WKUWPResult __stdcall loadHtml(uint64_t page, const WKUWPNavigationRequest* request, uint64_t* id)
    {
        auto self = find(page);
        if (!self || !request || !id) return WKUWPResultInvalidArgument;
        ++self->htmlCalls;
        self->requestShape = self->requestShape && request->contentKind == WKUWPContentHostHtml && !request->uri.length && !request->uri.data;
        self->warningSeen = request->warningToken;
        if (request->warningToken.issuer || request->warningToken.decision) {
            if (request->warningToken.issuer != self->decision.issuer || request->warningToken.decision != self->decision.decision)
                return WKUWPResultInvalidCertificateToken;
        } else self->decision = { 0, 0 };
        auto markup = copiedSpan(request->html);
        if (!self->begin("about:blank", WKUWPContentHostHtml, id)) return WKUWPResultOK;
        self->hostText = std::move(markup);
        self->commit();
        return WKUWPResultOK;
    }

    // Traverse the actual provider cursor without inserting transient host content.
    static WKUWPResult __stdcall back(uint64_t page)
    {
        auto self = find(page);
        if (!self) return WKUWPResultInvalidArgument;
        ++self->backCalls;
        self->decision = { 0, 0 };
        if (!self->cursor) return WKUWPResultOK;
        auto previous = self->cursor - 1;
        if (!self->begin(self->history[previous], WKUWPContentRemote, nullptr)) return WKUWPResultOK;
        self->cursor = previous;
        self->hostText.clear();
        self->commit();
        return WKUWPResultOK;
    }

    // Traverse the actual provider's next remote neighbor after transient dismissal.
    static WKUWPResult __stdcall forward(uint64_t page)
    {
        auto self = find(page);
        if (!self) return WKUWPResultInvalidArgument;
        ++self->forwardCalls;
        self->decision = { 0, 0 };
        if (self->cursor + 1 >= self->history.size()) return WKUWPResultOK;
        auto next = self->cursor + 1;
        if (!self->begin(self->history[next], WKUWPContentRemote, nullptr)) return WKUWPResultOK;
        self->cursor = next;
        self->hostText.clear();
        self->commit();
        return WKUWPResultOK;
    }

    // Reload the provider's current remote content without another history entry.
    static WKUWPResult __stdcall reload(uint64_t page)
    {
        auto self = find(page);
        if (!self) return WKUWPResultInvalidArgument;
        ++self->reloadCalls;
        self->decision = { 0, 0 };
        if (self->committed.empty()) return WKUWPResultOK;
        if (self->begin(self->committed, WKUWPContentRemote, nullptr)) self->commit();
        return WKUWPResultOK;
    }

    // Cancel only outstanding work and invalidate pending consent, not the current document.
    static WKUWPResult __stdcall stop(uint64_t page)
    {
        auto self = find(page);
        if (!self) return WKUWPResultInvalidArgument;
        ++self->stopCalls;
        self->decision = { 0, 0 };
        self->finish(WKUWPOutcomeCancelled);
        return WKUWPResultOK;
    }

    // Consume consent atomically before a new-ID retry of the exact stored failed target.
    static WKUWPResult __stdcall proceed(uint64_t page, const WKUWPCertificateToken* token, uint64_t* id)
    {
        auto self = find(page);
        if (!self || !token || !id) return WKUWPResultInvalidArgument;
        ++self->retryCalls;
        if (!token->issuer || token->issuer != self->decision.issuer || token->decision != self->decision.decision)
            return WKUWPResultInvalidCertificateToken;
        self->decision = { 0, 0 };
        self->armed = true;
        if (!self->begin(self->failedTarget, WKUWPContentRemote, id)) return WKUWPResultOK;
        if (!self->history.empty()) self->history.resize(self->cursor + 1);
        self->history.push_back(self->target);
        self->cursor = self->history.size() - 1;
        self->commit();
        return WKUWPResultOK;
    }

    // Acknowledge native page/context close; no opaque capability survives the page.
    static WKUWPResult __stdcall close(uint64_t page)
    {
        auto self = find(page);
        if (!self) return WKUWPResultClosed;
        self->decision = { 0, 0 };
        self->finish(WKUWPOutcomeCancelled);
        if (self->attached) {
            self->callbacks.releaseContext(self->callbacks.context);
            self->attached = false;
        }
        return WKUWPResultOK;
    }
};

// Exercise frozen-table command dispatch with actual public lifecycle/token methods.
void runCommands(CommandReport& report)
{
    {
        CommandPage f(401);
        f.view->GoBack(); f.view->GoForward(); f.view->Reload();
        report.check(f.backCalls == 1 && f.forwardCalls == 1 && f.reloadCalls == 1 && !f.nextId && !f.completions,
            "unavailable history and no-document Reload are no-ops without new IDs");
        f.view->Source = commandUri(L"https://example.test/a");
        f.view->Source = commandUri(L"https://example.test/b");
        f.view->Source = commandUri(L"https://example.test/c");
        report.check(f.remoteCalls == 3 && f.history.size() == 3 && f.cursor == 2 && f.requestShape,
            "public Source uses exact frozen Remote requests and provider-owned history");
        f.view->GoBack();
        report.check(f.backCalls == 2 && f.cursor == 1 && f.view->Source->AbsoluteUri == L"https://example.test/b" && f.view->CanGoForward,
            "public Back traverses actual engine cursor and copied availability");
        auto count = f.history.size();
        auto cursor = f.cursor;
        auto id = f.nextId;
        f.view->Reload();
        report.check(f.reloadCalls == 2 && f.nextId == id + 1 && f.history.size() == count && f.cursor == cursor,
            "remote Reload allocates a fresh provider ID without history insertion");
        f.view->NavigateToString(L"<title>Host</title><p>opaque data</p>");
        report.check(f.htmlCalls == 1 && f.requestShape && f.view->Source->AbsoluteUri == L"about:blank"
                && f.completed->ContentKind == WebKitNavigationContentKind::HostHtml && !f.completed->HasHttpStatus,
            "public HTML uses empty-base HostHtml request and blank typed lifecycle");
        report.check(f.history.size() == count && f.cursor == cursor && f.view->CanGoBack && f.view->CanGoForward,
            "host HTML does not insert replace remove or move engine history entries");
        auto markup = f.hostText;
        id = f.nextId;
        f.view->Reload();
        report.check(f.htmlCalls == 2 && f.reloadCalls == 2 && f.hostText == markup && f.nextId == id + 1
                && f.history.size() == count && f.cursor == cursor, "transient Reload replays retained exact markup with fresh ID and hidden history");
        f.view->GoBack();
        report.check(f.cursor == 0 && f.view->Source->AbsoluteUri == L"https://example.test/a", "Back from transient content traverses existing engine neighbor without invented extra entry");
        auto calls = f.backCalls;
        auto ids = f.nextId;
        f.view->GoBack();
        report.check(f.backCalls == calls + 1 && f.nextId == ids, "Back at oldest entry remains a no-op");
        f.view->GoForward();
        report.check(f.cursor == 1 && f.view->Source->AbsoluteUri == L"https://example.test/b", "Forward from existing cursor uses provider history");
        f.view->Source = commandUri(L"https://example.test/new-branch");
        report.check(f.history.size() == 3 && f.cursor == 2 && f.history.back() == "https://example.test/new-branch" && !f.view->CanGoForward,
            "later remote navigation truncates provider forward branch normally");
        auto terminals = f.completions;
        f.view->Stop(); f.view->Stop();
        report.check(f.stopCalls == 2 && f.completions == terminals && f.view->Source->AbsoluteUri == L"https://example.test/new-branch",
            "terminal Stop cancels subresources without new main terminal or document clear");
    }
    {
        CommandPage f(402);
        f.view->Source = commandUri(L"https://example.test/retained");
        bool veto = true;
        f.view->NavigationStarting += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationStartingEventArgs^>(
            [&veto](WebKitWebView^, WebKitNavigationStartingEventArgs^ args) { if (veto && args->ContentKind == WebKitNavigationContentKind::HostHtml) args->Cancel = true; });
        auto old = f.view->Source->AbsoluteUri;
        auto history = f.history.size();
        f.view->NavigateToString(L"vetoed HTML");
        report.check(f.completed->Outcome == WebKitNavigationOutcome::Cancelled && f.view->Source->AbsoluteUri == old
                && f.history.size() == history && f.hostText.empty(), "vetoed HTML owes one cancellation and retains current document and history");
        veto = false;
        auto text = ref new String(L"first\0second", 12);
        f.view->NavigateToString(text);
        report.check(f.hostText.size() == 12 && f.hostText[5] == '\0' && f.hostText.substr(6) == "second",
            "host text crosses the frozen span by length including embedded NUL");
        report.check(commandThrows([&] { f.view->NavigateToString(nullptr); }, E_INVALIDARG), "null HTML is rejected before native side effects");
    }
    {
        CommandPage a(403), b(404);
        a.failCertificate = true; b.failCertificate = true;
        a.view->Source = commandUri(L"https://example.test/failure");
        b.view->Source = commandUri(L"https://example.test/failure");
        auto token = a.completed->CertificateFailureToken;
        auto other = b.completed->CertificateFailureToken;
        report.check(token && other && token->FailedNavigationId == 1 && other->FailedNavigationId == 1,
            "both actual provider failures offer local-ID-1 control-bound objects");
        report.check(commandThrows([&] { a.view->NavigateToString(L"cross", other); }, E_INVALIDARG)
                && commandThrows([&] { a.view->ProceedWithCertificateFailure(other); }, E_INVALIDARG)
                && !a.retryCalls && a.view->IsCurrentCertificateToken(token), "cross-control warning and retry submission fail before token or page mutation");
        a.view->NavigateToString(L"warning markup", token);
        report.check(a.warningSeen.issuer == token->EngineToken().issuer && a.warningSeen.decision == token->EngineToken().decision
                && a.view->IsCurrentCertificateToken(token) && a.completed->ContentKind == WebKitNavigationContentKind::HostHtml,
            "associated warning commit preserves exact pending decision across new HostHtml ID");
        auto warningId = a.nextId;
        a.view->NavigateToString(L"updated warning", token);
        report.check(a.nextId == warningId + 1 && a.view->IsCurrentCertificateToken(token), "repeated explicitly associated warnings preserve eligibility");
        a.view->ProceedWithCertificateFailure(token);
        report.check(a.retryCalls == 1 && a.armed && a.decision.decision == 0 && a.nextId > warningId
                && a.completed->ContentKind == WebKitNavigationContentKind::Remote && a.target == "https://example.test/failure",
            "retry atomically consumes provider decision and uses a fresh Remote ID for exact failed target");
        report.check(!a.view->IsCurrentCertificateToken(token) && commandThrows([&] { a.view->ProceedWithCertificateFailure(token); }, E_INVALIDARG)
                && a.retryCalls == 1, "consumed object cannot retry a second time");
        b.view->NavigateToString(L"generic markup");
        report.check(!b.view->IsCurrentCertificateToken(other) && !b.decision.decision,
            "generic HTML invalidates both UI and provider pending consent");
    }
    {
        CommandPage f(405);
        f.failCertificate = true;
        f.view->Source = commandUri(L"https://example.test/reload-warning");
        auto token = f.completed->CertificateFailureToken;
        f.view->NavigateToString(L"warning", token);
        f.view->Reload();
        report.check(!f.view->IsCurrentCertificateToken(token) && !f.warningSeen.issuer && !f.decision.decision,
            "warning Reload replays text but ends token eligibility");
        f.failCertificate = true;
        f.view->Source = commandUri(L"https://example.test/noop-token");
        token = f.completed->CertificateFailureToken;
        f.view->GoForward();
        report.check(!f.view->IsCurrentCertificateToken(token) && f.forwardCalls == 1 && !f.decision.decision,
            "unavailable history invalidates UI and native pending decision without a new ID");
    }
    {
        CommandPage f(406);
        f.failCertificate = true;
        f.view->Source = commandUri(L"https://example.test/vetoed-retry");
        auto token = f.completed->CertificateFailureToken;
        f.view->NavigationStarting += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationStartingEventArgs^>(
            [](WebKitWebView^, WebKitNavigationStartingEventArgs^ args) { args->Cancel = true; });
        f.view->ProceedWithCertificateFailure(token);
        report.check(f.retryCalls == 1 && !f.decision.decision && f.completed->Outcome == WebKitNavigationOutcome::Cancelled
                && !f.view->IsCurrentCertificateToken(token), "vetoed retry still consumes consent and owes one new-ID cancellation");
    }
    {
        CommandPage f(407);
        f.failCertificate = true;
        bool immediateConsumption = false, frozen = false;
        f.view->NavigationCompleted += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationCompletedEventArgs^>(
            [&f, &immediateConsumption, &frozen](WebKitWebView^, WebKitNavigationCompletedEventArgs^ args) {
                if (!args->CertificateFailureToken) return;
                auto id = f.nextId;
                f.view->ProceedWithCertificateFailure(args->CertificateFailureToken);
                immediateConsumption = !f.view->IsCurrentCertificateToken(args->CertificateFailureToken)
                    && commandThrows([&] { f.view->ProceedWithCertificateFailure(args->CertificateFailureToken); }, E_INVALIDARG);
                frozen = !f.view->IsLoading && f.nextId == id && !f.retryCalls;
            });
        f.view->Source = commandUri(L"https://example.test/reentrant-retry");
        report.check(immediateConsumption && frozen && f.retryCalls == 1 && f.nextId == 2,
            "reentrant retry consumes before enqueue but defers provider work until old completion unwinds");
    }
    report.check(PageControllerUWP::resultToHRESULT(WKUWPResultInvalidCertificateToken) == E_INVALIDARG
            && PageControllerUWP::resultToHRESULT(WKUWPResultWrongThread) == RPC_E_WRONG_THREAD
            && PageControllerUWP::resultToHRESULT(static_cast<WKUWPResult>(E_ACCESSDENIED)) == E_ACCESSDENIED,
        "native operation result mapping is explicit and preserves originating HRESULT");
}

} // namespace

// Diagnostic-only export: public commands must execute against the exact frozen table.
extern "C" __declspec(dllexport) HRESULT __stdcall WKUWPTestCommands(const wchar_t* path)
{
    CommandReport report;
    try { runCommands(report); }
    catch (Exception^ exception) {
        report.check(false, "uncaught command test exception");
        report.lines += "HRESULT=" + std::to_string(exception->HResult) + "\r\n";
    } catch (...) { report.check(false, "uncaught native command test exception"); }
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
