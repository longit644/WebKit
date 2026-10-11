// Diagnostic-only exact notification traces against the real control and frozen page table.
#include "WebKitWebView.h"

#include <cstdio>
#include <inspectable.h>
#include <map>
#include <string>
#include <vector>

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::UI::Xaml::Data;
using namespace WebKit;

namespace {

// Fail-closed assertion and actual/reference trace aggregation.
struct TransactionReport {
    // Number of real-control matrix assertions evaluated.
    unsigned checks { 0 };
    // Failure count retained independently of later passing cases.
    unsigned failures { 0 };
    // Per-assertion results plus exact actual/expected traces.
    std::string lines;
    // Append one matrix assertion without hiding prior failure.
    void check(bool value, const char* name)
    {
        ++checks;
        if (!value) ++failures;
        lines += value ? "PASS: " : "FAIL: ";
        lines += name;
        lines += "\r\n";
    }

    // Compare the complete filtered transaction trace, not loose marker presence.
    void trace(const std::vector<std::string>& actual, const std::vector<std::string>& expected, const char* name)
    {
        check(actual == expected, name);
        lines += "actual=";
        for (auto& item : actual) lines += item + " ";
        lines += "\r\nexpected=";
        for (auto& item : expected) lines += item + " ";
        lines += "\r\n";
    }
};

// Known provider registry entry for context-free V1 operation signatures.
struct TransactionPage;
// Diagnostic-only routing by explicit page handle, with no framework global selected view.
std::map<uint64_t, TransactionPage*> transactionPages;

// Exact-trace provider with controlled async completion, admission errors and warning decisions.
struct TransactionPage {
    // Real authored XAML/CX object that implements every public transaction.
    WebKitWebView^ view { ref new WebKitWebView() };
    // Frozen engine table bound through the actual native page controller.
    WKW10MEngineAPI api { };
    // Retained copied provider callback table.
    WKW10MPageCallbacks callbacks { };
    // Page/main/document/sequence identities supplied by the provider.
    uint64_t page, nextId { 0 }, active { 0 }, generation { 0 }, sequence { 0 };
    // Active content policy and current proposed URI.
    uint32_t kind { WKW10MContentRemote };
    std::string target;
    // Actual filtered native/property/policy/terminal trace.
    std::vector<std::string> events;
    // Exact copied last HTML request used to prove latest unstarted intent selection.
    std::string markup;
    // Public outcomes recorded by ID, proving exactly-once obligations.
    std::map<uint64_t, unsigned> terminalCounts;
    // Last immutable completion retained after provider buffer reuse.
    WebKitNavigationCompletedEventArgs^ completed;
    // Provider-owned current certificate decision.
    WKW10MCertificateToken decision { 0, 0 };
    // Controlled native admission/decision failures without allocating any main ID.
    bool rejectNext { false }, certificateFailure { false };
    // Simulate an asynchronously acknowledged policy veto with an owed terminal.
    bool holdPolicyTerminal { false };
    // Counters proving rejected commands do not reach other operation slots.
    unsigned htmlCalls { 0 }, retryCalls { 0 }, closes { 0 };
    // Provider callback attachment ownership.
    bool attached { false };

    // Create an actual control with a negotiated frozen table and exact trace observers.
    explicit TransactionPage(uint64_t id, bool ready = true) : page(id)
    {
        transactionPages[page] = this;
        api.header = { sizeof(api), 1 };
        api.abiMajor = 1;
        api.capabilities = WKW10MCapabilityStructuredNotifications | WKW10MCapabilityHostHtml | WKW10MCapabilityCertificateDecisions;
        api.setCallbacks = &attach;
        api.navigate = &navigate;
        api.loadHtml = &html;
        api.goBack = &noop;
        api.goForward = &noop;
        api.reload = &noop;
        api.stop = &stop;
        api.proceedWithCertificateFailure = &retry;
        api.closePage = &close;
        view->SetCommandHandler(&initialize, this);
        auto initialized = view->EnsureInitializedAsync();
        if (FAILED(view->BindPageController(&api, page))) throw Exception::CreateException(E_FAIL);
        if (ready) {
            view->CompleteInitialization(view->InitializationAttemptId(), page, S_OK);
            initialized->GetResults();
        }
        view->PropertyChanged += ref new PropertyChangedEventHandler([this](Object^, PropertyChangedEventArgs^ args) {
            if (args->PropertyName == L"IsLoading") events.push_back(view->IsLoading ? "P1" : "P0");
        });
        view->NavigationStarting += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationStartingEventArgs^>(
            [this](WebKitWebView^, WebKitNavigationStartingEventArgs^ args) { events.push_back("S" + std::to_string(args->NavigationId)); });
        view->NavigationCompleted += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationCompletedEventArgs^>(
            [this](WebKitWebView^, WebKitNavigationCompletedEventArgs^ args) {
                completed = args;
                ++terminalCounts[args->NavigationId];
                auto outcome = args->Outcome == WebKitNavigationOutcome::Completed ? "C" : args->Outcome == WebKitNavigationOutcome::Cancelled ? "X" : "F";
                events.push_back(std::string(outcome) + std::to_string(args->NavigationId));
            });
    }

    // Suppress expired local subscriber captures before provider/native fixture release.
    ~TransactionPage()
    {
        if (attached) static_cast<PageCallbacksW10M*>(callbacks.context)->deactivate();
        view->Close();
        if (attached) callbacks.releaseContext(callbacks.context);
        transactionPages.erase(page);
    }

    // Locate only the explicitly supplied provider page.
    static TransactionPage* find(uint64_t page)
    {
        auto item = transactionPages.find(page);
        return item == transactionPages.end() ? nullptr : item->second;
    }

    // Build a current strictly ordered V1 signal whose bytes are borrowed only for dispatch.
    WKW10MPageEvent packet(uint32_t type)
    {
        WKW10MPageEvent result { };
        result.header = { sizeof(result), 1 };
        result.pageId = page;
        result.navigationId = active;
        result.documentGeneration = generation;
        result.callbackSequence = ++sequence;
        result.eventKind = type;
        result.contentKind = kind;
        result.isLoading = !!active;
        result.uri = { reinterpret_cast<const uint8_t*>(target.data()), static_cast<uint32_t>(target.size()) };
        return result;
    }

    // Finish one provider navigation before callbacks may reenter and allocate another ID.
    void finish(uint32_t outcome = WKW10MOutcomeCompleted, uint32_t domain = WKW10MErrorDomainNone, int code = 0, WKW10MCertificateToken token = { 0, 0 })
    {
        if (!active) return;
        auto value = packet(WKW10MEventCompleted);
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

    // Commit one current document generation without automatically finishing its load.
    void commit()
    {
        ++generation;
        auto value = packet(WKW10MEventSourceChanged);
        callbacks.notify(callbacks.context, &value);
    }

    // Admit and trace one fresh Remote/HostHtml proposal, honoring real policy precedence.
    void begin(std::string uri, uint32_t content, uint64_t* id)
    {
        finish(WKW10MOutcomeCancelled);
        target = std::move(uri);
        kind = content;
        active = ++nextId;
        if (id) *id = active;
        events.push_back("N" + std::to_string(active));
        auto value = packet(WKW10MEventStarting);
        uint32_t cancel = 0;
        auto result = callbacks.starting(callbacks.context, &value, &cancel);
        if (cancel) {
            if (!holdPolicyTerminal) finish(WKW10MOutcomeCancelled);
        }
        else if (result != WKW10MResultOK) finish(WKW10MOutcomeFailed, WKW10MErrorDomainPlatform, result);
        else if (certificateFailure) {
            certificateFailure = false;
            decision = { 12000 + page, 1 };
            finish(WKW10MOutcomeFailed, WKW10MErrorDomainTransport, 60, decision);
        }
    }

    // Controlled readiness is separate from document navigation identity.
    static HRESULT __stdcall initialize(void*, SourceActionW10M kind, const WKW10MString*)
    {
        return kind == SourceActionW10M::Initialize ? S_OK : E_NOTIMPL;
    }

    // Retain the provider's actual frozen attachment once.
    static WKW10MResult __stdcall attach(uint64_t page, const WKW10MPageCallbacks* table)
    {
        auto self = find(page);
        if (!self || self->attached) return WKW10MResultInvalidArgument;
        self->callbacks = *table;
        table->retainContext(table->context);
        self->attached = true;
        return WKW10MResultOK;
    }

    // Reject admission or begin exactly the borrowed Remote target through V1 policy.
    static WKW10MResult __stdcall navigate(uint64_t page, const WKW10MNavigationRequest* request, uint64_t* id)
    {
        auto self = find(page);
        if (!self) return WKW10MResultClosed;
        if (self->rejectNext) { self->rejectNext = false; return static_cast<WKW10MResult>(E_ACCESSDENIED); }
        self->decision = { 0, 0 };
        self->begin(std::string(reinterpret_cast<const char*>(request->uri.data), request->uri.length), WKW10MContentRemote, id);
        return WKW10MResultOK;
    }

    // Revalidate a copied warning pair at provider execution; generic HTML invalidates it.
    static WKW10MResult __stdcall html(uint64_t page, const WKW10MNavigationRequest* request, uint64_t* id)
    {
        auto self = find(page);
        if (!self) return WKW10MResultClosed;
        ++self->htmlCalls;
        if (request->warningToken.issuer && (request->warningToken.issuer != self->decision.issuer || request->warningToken.decision != self->decision.decision))
            return WKW10MResultInvalidCertificateToken;
        if (!request->warningToken.issuer) self->decision = { 0, 0 };
        self->markup.assign(reinterpret_cast<const char*>(request->html.data), request->html.length);
        self->begin("about:blank", WKW10MContentHostHtml, id);
        if (self->active) { self->commit(); self->finish(); }
        return WKW10MResultOK;
    }

    // No-op page commands still end provider pending consent but invent no main identity.
    static WKW10MResult __stdcall noop(uint64_t page)
    {
        auto self = find(page);
        if (!self) return WKW10MResultClosed;
        self->decision = { 0, 0 };
        return WKW10MResultOK;
    }

    // Cancel active work once; queued Stop must not run during the old notification.
    static WKW10MResult __stdcall stop(uint64_t page)
    {
        auto self = find(page);
        if (!self) return WKW10MResultClosed;
        self->decision = { 0, 0 };
        self->finish(WKW10MOutcomeCancelled);
        return WKW10MResultOK;
    }

    // Invalidated queued retries reject before provider work or fresh ID allocation.
    static WKW10MResult __stdcall retry(uint64_t page, const WKW10MCertificateToken* token, uint64_t* id)
    {
        auto self = find(page);
        if (!self) return WKW10MResultClosed;
        ++self->retryCalls;
        if (token->issuer != self->decision.issuer || token->decision != self->decision.decision) return WKW10MResultInvalidCertificateToken;
        self->decision = { 0, 0 };
        self->begin("https://example.test/retry", WKW10MContentRemote, id);
        return WKW10MResultOK;
    }

    // Trace one native cleanup request only after all owed public transactions finish.
    static WKW10MResult __stdcall close(uint64_t page)
    {
        auto self = find(page);
        if (!self) return WKW10MResultClosed;
        self->events.push_back("Z");
        ++self->closes;
        self->finish(WKW10MOutcomeCancelled);
        if (self->attached) { self->attached = false; self->callbacks.releaseContext(self->callbacks.context); }
        return WKW10MResultOK;
    }
};

// URI fixture constructor used only by public setter/command calls.
Uri^ traceUri(const wchar_t* suffix) { return ref new Uri(ref new String(suffix)); }

// Read the actual published URI, using the contract's empty native null convention.
std::wstring traceSource(WebKitWebView^ view)
{
    return view->Source ? std::wstring(view->Source->AbsoluteUri->Data(), view->Source->AbsoluteUri->Length()) : std::wstring();
}

// Run exact reference traces and state/token revalidation laws on the actual control.
void runTransactions(TransactionReport& report)
{
    {
        TransactionPage f(601);
        f.view->Source = traceUri(L"https://example.test/a"); f.commit(); f.events.clear();
        bool frozen = false, armed = true;
        f.view->PropertyChanged += ref new PropertyChangedEventHandler([&](Object^, PropertyChangedEventArgs^ args) {
            if (args->PropertyName != L"IsLoading" || f.view->IsLoading || !armed) return;
            armed = false;
            f.view->Source = traceUri(L"https://example.test/b");
            frozen = traceSource(f.view) == L"https://example.test/a" && !f.view->IsLoading && f.nextId == 1;
        });
        f.finish();
        report.check(frozen && f.terminalCounts[1] == 1, "PropertyChanged navigation keeps A snapshot and owes its terminal once");
        report.trace(f.events, { "P0", "C1", "N2", "P1", "S2" }, "reference trace PropertyChanged(A) navigating B after Completed(A)");
    }
    {
        TransactionPage f(602);
        f.view->Source = traceUri(L"https://example.test/a"); f.commit(); f.events.clear();
        f.view->PropertyChanged += ref new PropertyChangedEventHandler([&](Object^, PropertyChangedEventArgs^ args) {
            if (args->PropertyName == L"IsLoading" && !f.view->IsLoading) f.view->Close();
        });
        f.finish();
        report.check(f.terminalCounts[1] == 1 && f.completed->Outcome == WebKitNavigationOutcome::Completed, "PropertyChanged Close cannot erase or cancel already-terminal A");
        report.trace(f.events, { "P0", "C1", "Z" }, "reference trace owed Completed before reentrant Close revocation");
    }
    {
        TransactionPage f(603);
        f.view->NavigationStarting += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationStartingEventArgs^>(
            [&f](WebKitWebView^, WebKitNavigationStartingEventArgs^ args) {
                if (args->NavigationId != 1) return;
                f.view->Source = traceUri(L"https://example.test/replacement");
                throw Exception::CreateException(E_ACCESSDENIED);
            });
        f.view->Source = traceUri(L"https://example.test/a");
        report.check(f.terminalCounts[1] == 1 && f.terminalCounts[2] == 0 && f.view->IsLoading, "Starting replacement plus throw cancels A and admits only queued B");
        report.trace(f.events, { "N1", "P1", "S1", "P0", "X1", "N2", "P1", "S2" }, "reference trace Starting replacement cancellation precedes B proposal");
    }
    {
        TransactionPage f(604);
        f.view->NavigationStarting += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationStartingEventArgs^>(
            [](WebKitWebView^, WebKitNavigationStartingEventArgs^ args) { args->Cancel = true; throw Exception::CreateException(E_FAIL); });
        f.view->Source = traceUri(L"https://example.test/a");
        report.trace(f.events, { "N1", "P1", "S1", "P0", "X1" }, "reference trace explicit Cancel beats Starting throw");
        report.check(f.completed->ErrorDomain == WebKitNavigationErrorDomain::None && f.terminalCounts[1] == 1, "canceled proposal does not acquire a second Platform failure outcome");
    }
    {
        TransactionPage f(605);
        f.view->NavigationStarting += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationStartingEventArgs^>(
            [](WebKitWebView^, WebKitNavigationStartingEventArgs^) { throw Exception::CreateException(E_ACCESSDENIED); });
        f.view->Source = traceUri(L"https://example.test/a");
        report.trace(f.events, { "N1", "P1", "S1", "P0", "F1" }, "reference trace unopposed Starting throw maps one failure");
        report.check(f.completed->ErrorDomain == WebKitNavigationErrorDomain::Platform && f.completed->ErrorCode == E_ACCESSDENIED && f.terminalCounts[1] == 1,
            "Starting failure preserves exact Platform HRESULT with one owed terminal");
    }
    {
        TransactionPage f(606);
        f.view->Source = traceUri(L"https://example.test/a"); f.commit(); f.events.clear();
        bool frozen = false;
        f.view->NavigationCompleted += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationCompletedEventArgs^>(
            [&f, &frozen](WebKitWebView^, WebKitNavigationCompletedEventArgs^ args) {
                if (args->NavigationId != 1) return;
                f.view->Source = traceUri(L"https://example.test/b");
                frozen = !f.view->IsLoading && traceSource(f.view) == L"https://example.test/a" && f.nextId == 1;
            });
        f.finish();
        report.check(frozen && f.terminalCounts[1] == 1, "Completed reentrancy keeps A getters frozen until invocation ends");
        report.trace(f.events, { "P0", "C1", "N2", "P1", "S2" }, "reference trace Completed(A) navigation starts B afterward");
    }
    {
        TransactionPage f(607);
        f.view->Source = traceUri(L"https://example.test/a"); f.commit(); f.events.clear();
        f.view->NavigationCompleted += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationCompletedEventArgs^>(
            [&f](WebKitWebView^, WebKitNavigationCompletedEventArgs^) { f.view->Close(); });
        f.finish();
        report.trace(f.events, { "P0", "C1", "Z" }, "reference trace Close in Completed never invents cancellation A");
        report.check(f.terminalCounts[1] == 1 && f.view->ControlPhase() == ControlPhaseW10M::Closed, "Completed Close releases the instance only after its single terminal");
    }
    {
        TransactionPage f(608);
        f.view->Source = traceUri(L"https://example.test/a"); f.commit(); f.events.clear();
        bool armed = true;
        f.view->PropertyChanged += ref new PropertyChangedEventHandler([&](Object^, PropertyChangedEventArgs^ args) {
            if (args->PropertyName != L"IsLoading" || f.view->IsLoading || !armed) return;
            armed = false;
            auto next = f.packet(WKW10MEventTitleChanged);
            next.documentGeneration = 0;
            // Stable invalid-generation payload must never overwrite the terminal snapshot.
            constexpr char staleTitle[] = "stale title";
            next.title = { reinterpret_cast<const uint8_t*>(staleTitle), sizeof(staleTitle) - 1 };
            f.callbacks.notify(f.callbacks.context, &next);
            f.view->Source = traceUri(L"https://example.test/b");
        });
        f.finish();
        report.check(f.terminalCounts[1] == 1 && f.view->DocumentTitle->IsEmpty() && f.nextId == 2, "stale generation queued during terminal properties cannot erase A delivery");
        report.trace(f.events, { "P0", "C1", "N2", "P1", "S2" }, "reference trace generation rejection leaves owed Completed order intact");
    }
    {
        TransactionPage f(609);
        f.view->Source = traceUri(L"https://example.test/a"); f.commit(); f.events.clear();
        bool armed = true;
        f.view->PropertyChanged += ref new PropertyChangedEventHandler([&](Object^, PropertyChangedEventArgs^ args) {
            if (args->PropertyName != L"IsLoading" || f.view->IsLoading || !armed) return;
            armed = false;
            f.rejectNext = true;
            f.view->Source = traceUri(L"https://example.test/rejected");
            f.view->Source = traceUri(L"https://example.test/valid-next");
        });
        f.finish();
        report.check(f.view->DeferredCommandFailures() == 1 && f.view->LastDeferredCommandError() == E_ACCESSDENIED
                && f.nextId == 2 && traceSource(f.view) == L"https://example.test/valid-next",
            "failed deferred native admission is diagnostic and valid following FIFO command still executes");
        report.trace(f.events, { "P0", "C1", "N2", "P1", "S2" }, "reference trace rejected admission invents no ID or missing following proposal");
    }
    {
        TransactionPage f(610);
        f.certificateFailure = true;
        bool queued = false;
        f.view->NavigationCompleted += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationCompletedEventArgs^>(
            [&f, &queued](WebKitWebView^, WebKitNavigationCompletedEventArgs^ args) {
                if (!args->CertificateFailureToken || queued) return;
                queued = true;
                f.view->Stop();
                f.view->NavigateToString(L"obsolete warning", args->CertificateFailureToken);
                f.view->Source = traceUri(L"https://example.test/following-valid");
            });
        f.view->Source = traceUri(L"https://example.test/failure");
        report.check(f.view->DeferredCommandFailures() == 1 && f.view->LastDeferredCommandError() == E_INVALIDARG
                && !f.htmlCalls && f.nextId == 2 && f.terminalCounts[1] == 1,
            "warning token is revalidated after queued Stop and rejected without stranding later Source");
        report.trace(f.events, { "N1", "P1", "S1", "P0", "F1", "N2", "P1", "S2" }, "reference trace stale warning loses no old terminal or subsequent valid command");
    }
    {
        TransactionPage f(611);
        f.certificateFailure = true;
        f.view->NavigationCompleted += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationCompletedEventArgs^>(
            [&f](WebKitWebView^, WebKitNavigationCompletedEventArgs^ args) {
                if (!args->CertificateFailureToken) return;
                f.view->Stop();
                f.view->ProceedWithCertificateFailure(args->CertificateFailureToken);
                f.view->Source = traceUri(L"https://example.test/retry-following-valid");
            });
        f.view->Source = traceUri(L"https://example.test/failure");
        report.check(f.view->DeferredCommandFailures() == 1 && !f.retryCalls && f.nextId == 2 && f.terminalCounts[1] == 1,
            "consumed retry reservation revalidates mutation epoch and valid following command survives");
    }
    {
        TransactionPage f(612);
        f.view->Source = traceUri(L"https://example.test/a"); f.commit(); f.finish();
        auto source = traceSource(f.view);
        f.rejectNext = true;
        HRESULT result = S_OK;
        try { f.view->Source = traceUri(L"https://example.test/rejected-direct"); }
        catch (Exception^ exception) { result = exception->HResult; }
        report.check(result == E_ACCESSDENIED && traceSource(f.view) == source && f.nextId == 1 && f.view->DeferredCommandFailures() == 0,
            "synchronous admission failure restores exact previous Source and keeps original caller HRESULT");
    }
    {
        TransactionPage f(613, false);
        auto observer = f.view->EnsureInitializedAsync();
        f.view->NavigateToString(L"unstarted obsolete markup");
        f.view->Stop();
        auto result = f.view->CompleteInitialization(f.view->InitializationAttemptId(), f.page, S_OK);
        report.check(result == S_OK && observer->Status == AsyncStatus::Completed && !f.htmlCalls && !f.nextId && !f.view->IsLoading,
            "Stop drops obsolete HTML intent while shared initialization and its joiner succeed");
    }
    {
        TransactionPage f(614, false);
        f.view->NavigateToString(L"older markup");
        f.view->NavigateToString(L"latest markup");
        f.view->CompleteInitialization(f.view->InitializationAttemptId(), f.page, S_OK);
        report.check(f.htmlCalls == 1 && f.markup == "latest markup" && f.nextId == 1 && f.terminalCounts[1] == 1,
            "only latest HTML initialization continuation admits one copied document");
    }
    {
        TransactionPage f(615, false);
        f.view->NavigateToString(L"obsolete markup");
        f.view->Source = traceUri(L"https://example.test/latest-initial-source");
        f.view->CompleteInitialization(f.view->InitializationAttemptId(), f.page, S_OK);
        report.check(!f.htmlCalls && f.nextId == 1 && f.target == "https://example.test/latest-initial-source" && f.view->IsLoading,
            "new Source while Initializing invalidates older unstarted HTML intent");
    }
    {
        TransactionPage f(616, false);
        f.view->Source = traceUri(L"https://example.test/obsolete-initial-source");
        f.view->NavigateToString(L"replacement markup");
        f.view->CompleteInitialization(f.view->InitializationAttemptId(), f.page, S_OK);
        report.check(f.htmlCalls == 1 && f.nextId == 1 && f.terminalCounts[1] == 1 && traceSource(f.view) == L"about:blank",
            "latest unstarted HTML replaces initial Source without launching stale Remote work afterward");
    }
    {
        TransactionPage f(617, false);
        f.view->Source = traceUri(L"https://example.test/rejected-initial-source");
        auto observer = f.view->EnsureInitializedAsync();
        f.rejectNext = true;
        auto result = f.view->CompleteInitialization(f.view->InitializationAttemptId(), f.page, S_OK);
        report.check(result == S_OK && observer->Status == AsyncStatus::Completed && f.view->DeferredCommandFailures() == 1
                && f.view->LastDeferredCommandError() == E_ACCESSDENIED && !f.nextId && !f.view->Source && !f.view->IsLoading,
            "ready page survives deferred initial admission rejection with completed joiner and restored Source");
        f.view->Source = traceUri(L"https://example.test/recovered");
        report.check(f.nextId == 1 && f.view->IsLoading && f.view->DeferredCommandFailures() == 1,
            "valid Source after failed readiness admission still executes on the ready page");
    }
    {
        TransactionPage f(618);
        f.holdPolicyTerminal = true;
        f.view->NavigationStarting += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationStartingEventArgs^>(
            [](WebKitWebView^, WebKitNavigationStartingEventArgs^ args) { if (args->NavigationId == 1) args->Cancel = true; });
        f.view->Source = traceUri(L"https://example.test/pending-veto");
        f.rejectNext = true;
        f.view->Source = traceUri(L"https://example.test/rejected-after-veto");
        f.view->Source = traceUri(L"https://example.test/valid-after-veto");
        report.check(f.nextId == 1 && f.view->IsLoading && traceSource(f.view) == L"https://example.test/pending-veto",
            "commands waiting for asynchronous policy terminal leave A snapshot and main identity frozen");
        f.holdPolicyTerminal = false;
        f.finish(WKW10MOutcomeCancelled);
        report.check(f.view->DeferredCommandFailures() == 1 && f.view->LastDeferredCommandError() == E_ACCESSDENIED
                && f.terminalCounts[1] == 1 && f.nextId == 2 && f.view->IsLoading,
            "policy-wait commands are deferred even outside callbacks and failure cannot strand later FIFO work");
        report.trace(f.events, { "N1", "P1", "S1", "P0", "X1", "N2", "P1", "S2" },
            "reference trace asynchronous veto owes terminal before rejected and valid deferred proposals");
    }
    {
        TransactionPage f(619);
        f.view->NavigationStarting += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationStartingEventArgs^>(
            [&f](WebKitWebView^, WebKitNavigationStartingEventArgs^) { f.view->Close(); throw Exception::CreateException(E_ACCESSDENIED); });
        f.view->Source = traceUri(L"https://example.test/close-in-policy");
        report.check(f.closes == 1 && f.terminalCounts[1] == 1 && f.completed->ErrorDomain == WebKitNavigationErrorDomain::None
                && f.view->ControlPhase() == ControlPhaseW10M::Closed,
            "Starting Close overrides throw with one Cancelled and one native cleanup");
        report.trace(f.events, { "N1", "P1", "S1", "P0", "X1", "Z" },
            "reference trace Starting Close delivers cancellation before revocation");
    }
    {
        TransactionPage f(620);
        f.view->NavigationStarting += ref new TypedEventHandler<WebKitWebView^, WebKitNavigationStartingEventArgs^>(
            [&f](WebKitWebView^, WebKitNavigationStartingEventArgs^) { f.view->Stop(); });
        f.view->Source = traceUri(L"https://example.test/stop-in-policy");
        report.check(f.terminalCounts[1] == 1 && f.nextId == 1 && !f.view->IsLoading && !f.view->DeferredCommandFailures(),
            "Starting Stop cancels one proposal without fabricating a second main identity");
        report.trace(f.events, { "N1", "P1", "S1", "P0", "X1" }, "reference trace Starting Stop executes after owed cancellation");
    }
}

} // namespace

// Diagnostic-only XAML-owner entry recording exact actual/reference notification traces.
extern "C" __declspec(dllexport) HRESULT __stdcall WKW10MTestTransactions(const wchar_t* path)
{
    TransactionReport report;
    try { runTransactions(report); }
    catch (Exception^ exception) {
        report.check(false, "uncaught transaction matrix exception");
        report.lines += "HRESULT=" + std::to_string(exception->HResult) + "\r\n";
    } catch (...) { report.check(false, "uncaught native transaction matrix exception"); }
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
