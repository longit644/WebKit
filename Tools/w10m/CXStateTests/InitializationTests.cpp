// Diagnostic-only tests of real SDK async actions returned by the authored control.
// The per-control port supplies deterministic readiness, never a public fake action.
#include "WebKitWebView.h"

#include <cstdio>
#include <inspectable.h>
#include <string>

using namespace Platform;
using namespace Windows::Foundation;
using namespace WebKit;

namespace {

// Fail-closed versioned assertion aggregation for independent initialization callers.
struct InitializationReport {
    // Number of real-control assertions evaluated.
    unsigned checks { 0 };
    // Number of failures, never overwritten by later passing assertions.
    unsigned failures { 0 };
    // Full ASCII result lines saved with the package version.
    std::string lines;

    // Record one assertion independently of the visible aggregate result.
    void check(bool passed, const char* name)
    {
        ++checks;
        if (!passed) ++failures;
        lines += passed ? "PASS: " : "FAIL: ";
        lines += name;
        lines += "\r\n";
    }
};

// Per-instance readiness driver using the real control's generation-bound port.
struct InitializationDriver {
    // Real XAML control, including copied options and actual IAsyncAction objects.
    WebKitWebView^ view;
    // Captured attempt identity, used to reject stale completion on retry.
    uint64_t attempt { 0 };
    // Distinct page identity supplied only at controlled readiness.
    uint64_t page;
    // Initialization controller calls observed, excluding new joiners.
    unsigned initializes { 0 };
    // Native remote proposals observed after initialization completion callbacks.
    unsigned navigates { 0 };
    // Native cleanup calls observed after control Close.
    unsigned closes { 0 };
    // Monotonic main ID allocated by this controlled page.
    uint64_t navigation { 0 };
    // Monotonic callback sequence for this controlled page.
    uint64_t sequence { 0 };
    // Last initialized immutable configuration holder, retained by the control.
    const ValidatedWebViewOptionsW10M* configuration { nullptr };
    // Last proposed URI, independent of copied Source during handler reentrancy.
    std::wstring target;

    // Bind a deterministic readiness port to a real default/configured control.
    explicit InitializationDriver(uint64_t id, WebKitWebViewOptions^ options = nullptr)
        : view(options ? ref new WebKitWebView(options) : ref new WebKitWebView())
        , page(id)
    {
        view->SetCommandHandler(&command, this);
    }

    // Remove borrowed fixture routing before local lifetime ends.
    ~InitializationDriver()
    {
        view->SetCommandHandler(nullptr, nullptr);
        view->Close();
    }

    // Resolve exactly the captured attempt with an explicit page/error result.
    HRESULT resolve(HRESULT result = S_OK)
    {
        return view->CompleteInitialization(attempt, SUCCEEDED(result) ? page : 0, result);
    }

    // Observe initialization/remote/cleanup work and emit only a Starting signal.
    static HRESULT __stdcall command(void* context, SourceActionW10M kind, const WKW10MString* uri)
    {
        auto& self = *static_cast<InitializationDriver*>(context);
        if (kind == SourceActionW10M::Initialize) {
            ++self.initializes;
            self.attempt = self.view->InitializationAttemptId();
            self.configuration = self.view->ValidatedInitializationOptions();
            return S_OK;
        }
        if (kind == SourceActionW10M::Close) { ++self.closes; return S_OK; }
        if (kind == SourceActionW10M::Stop) return S_OK;
        if (kind != SourceActionW10M::Navigate) return E_INVALIDARG;
        ++self.navigates;
        auto length = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(uri->data), static_cast<int>(uri->length), nullptr, 0);
        self.target.resize(length);
        MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(uri->data), static_cast<int>(uri->length), &self.target[0], length);
        WKW10MPageEvent event { };
        event.header = { sizeof(event), WKW10M_STRUCTURE_VERSION };
        event.pageId = self.page;
        event.callbackSequence = ++self.sequence;
        event.navigationId = ++self.navigation;
        event.eventKind = WKW10MEventStarting;
        event.uri = *uri;
        event.isLoading = 1;
        uint32_t cancel = 0;
        return self.view->HandleStarting(event, &cancel);
    }
};

// Check exact WinRT error semantics rather than accepting any thrown exception.
template<typename F> bool throwsResult(F operation, HRESULT expected)
{
    try { operation(); }
    catch (Exception^ exception) { return exception->HResult == expected; }
    return false;
}

// Build a real Foundation URI for queued Source tests.
Uri^ remote(const wchar_t* name) { return ref new Uri(ref new String(name)); }

// Retrieve the SDK lifecycle interface, proving the returned action's actual projection.
IAsyncInfo^ info(IAsyncAction^ action) { return safe_cast<IAsyncInfo^>(action); }

// Verify distinct identity, handlers, lifecycle, ordering, retries and immutable options.
void runInitialization(InitializationReport& report)
{
    {
        InitializationDriver f(201);
        auto a = f.view->EnsureInitializedAsync();
        auto b = f.view->EnsureInitializedAsync();
        auto ia = info(a), ib = info(b);
        report.check(a != b && ia->Id != ib->Id && ia->Id && ib->Id, "each caller gets a distinct action and nonzero identity");
        report.check(f.initializes == 1 && ia->Status == AsyncStatus::Started && ib->Status == AsyncStatus::Started, "two caller observations join one controller attempt");
        report.check(throwsResult([&] { a->GetResults(); }, E_ILLEGAL_METHOD_CALL), "pending GetResults reports explicit illegal-method-call");
        unsigned completedA = 0, completedB = 0;
        bool peerReady = false;
        a->Completed = ref new AsyncActionCompletedHandler([&](IAsyncAction^ caller, AsyncStatus status) {
            ++completedA;
            peerReady = caller == a && status == AsyncStatus::Completed && ib->Status == AsyncStatus::Completed && !f.navigates;
            caller->GetResults();
        });
        b->Completed = ref new AsyncActionCompletedHandler([&](IAsyncAction^ caller, AsyncStatus status) {
            if (caller == b && status == AsyncStatus::Completed) ++completedB;
        });
        report.check(f.resolve() == S_OK && completedA == 1 && completedB == 1 && peerReady, "all live actions resolve before either independent completion handler");
        report.check(ia->ErrorCode.Value == S_OK && ib->ErrorCode.Value == S_OK, "successful actions expose original S_OK result");
        report.check(throwsResult([&] { a->Completed = ref new AsyncActionCompletedHandler([](IAsyncAction^, AsyncStatus) { }); }, E_ILLEGAL_DELEGATE_ASSIGNMENT), "one handler assignment belongs to each action");
        auto readyA = f.view->EnsureInitializedAsync();
        auto readyB = f.view->EnsureInitializedAsync();
        unsigned late = 0;
        readyA->Completed = ref new AsyncActionCompletedHandler([&](IAsyncAction^, AsyncStatus status) { if (status == AsyncStatus::Completed) ++late; });
        report.check(readyA != readyB && info(readyA)->Status == AsyncStatus::Completed && info(readyB)->Status == AsyncStatus::Completed
                && late == 1 && f.initializes == 1, "post-ready calls return fresh completed actions with late handler delivery");
    }
    {
        InitializationDriver f(202);
        auto canceled = f.view->EnsureInitializedAsync();
        auto peer = f.view->EnsureInitializedAsync();
        unsigned canceledCalls = 0, peerCalls = 0;
        canceled->Completed = ref new AsyncActionCompletedHandler([&](IAsyncAction^, AsyncStatus status) { if (status == AsyncStatus::Canceled) ++canceledCalls; });
        peer->Completed = ref new AsyncActionCompletedHandler([&](IAsyncAction^, AsyncStatus status) { if (status == AsyncStatus::Completed) ++peerCalls; });
        info(canceled)->Cancel();
        report.check(info(canceled)->Status == AsyncStatus::Canceled && info(canceled)->ErrorCode.Value == E_ABORT && canceledCalls == 1, "Cancel ends only that caller as Canceled/E_ABORT");
        report.check(info(peer)->Status == AsyncStatus::Started && f.view->ControlPhase() == ControlPhaseW10M::Initializing && f.closes == 0, "independent Cancel keeps peer and shared attempt live");
        report.check(throwsResult([&] { canceled->GetResults(); }, E_ABORT), "canceled observation GetResults preserves E_ABORT");
        f.resolve();
        report.check(peerCalls == 1 && canceledCalls == 1 && info(canceled)->Status == AsyncStatus::Canceled, "readiness cannot retroactively succeed canceled actions");
        info(canceled)->Cancel();
        report.check(canceledCalls == 1, "repeated Cancel does not redeliver completion");
    }
    {
        InitializationDriver f(203);
        auto closed = f.view->EnsureInitializedAsync();
        auto peer = f.view->EnsureInitializedAsync();
        unsigned closedCalls = 0, peerCalls = 0;
        closed->Completed = ref new AsyncActionCompletedHandler([&](IAsyncAction^, AsyncStatus) { ++closedCalls; });
        peer->Completed = ref new AsyncActionCompletedHandler([&](IAsyncAction^, AsyncStatus) { ++peerCalls; });
        info(closed)->Close();
        info(closed)->Close();
        report.check(throwsResult([&] { (void)info(closed)->Status; }, RO_E_CLOSED)
                && throwsResult([&] { closed->GetResults(); }, RO_E_CLOSED), "IAsyncInfo.Close releases a pending observation with closed read/result semantics");
        report.check(f.view->ControlPhase() == ControlPhaseW10M::Initializing && info(peer)->Status == AsyncStatus::Started, "action Close never cancels shared initialization or peers");
        f.resolve();
        report.check(!closedCalls && peerCalls == 1, "closed action suppresses delivery while peer completes normally");
    }
    {
        InitializationDriver f(204);
        auto a = f.view->EnsureInitializedAsync(), b = f.view->EnsureInitializedAsync();
        info(a)->Cancel(); info(b)->Close();
        f.view->Source = remote(L"https://example.test/after-all-observers-ended");
        report.check(f.initializes == 1 && f.view->ControlPhase() == ControlPhaseW10M::Initializing, "attempt continues after every caller ends its observation");
        f.resolve();
        report.check(f.navigates == 1 && f.target == L"https://example.test/after-all-observers-ended", "Source-driven use still starts after all observations end");
    }
    {
        InitializationDriver f(205);
        auto a = f.view->EnsureInitializedAsync(), b = f.view->EnsureInitializedAsync();
        f.view->Source = remote(L"https://example.test/unused-a");
        f.view->Source = remote(L"https://example.test/latest-b");
        unsigned callbacks = 0;
        bool beforeNavigation = true;
        a->Completed = ref new AsyncActionCompletedHandler([&](IAsyncAction^, AsyncStatus) { ++callbacks; beforeNavigation = beforeNavigation && !f.navigates && !f.view->IsLoading; });
        b->Completed = ref new AsyncActionCompletedHandler([&](IAsyncAction^, AsyncStatus) { ++callbacks; beforeNavigation = beforeNavigation && !f.navigates && !f.view->IsLoading; });
        f.resolve();
        report.check(callbacks == 2 && beforeNavigation && f.navigates == 1 && f.target == L"https://example.test/latest-b", "live joiners complete before only the latest Source is scheduled");
        report.check(info(a)->Status == AsyncStatus::Completed && info(b)->Status == AsyncStatus::Completed && f.view->IsLoading, "initialization success is readiness, not first navigation completion");
    }
    {
        InitializationDriver f(206);
        f.view->Source = remote(L"https://example.test/cleared");
        auto a = f.view->EnsureInitializedAsync(), b = f.view->EnsureInitializedAsync();
        f.view->Source = nullptr;
        f.resolve();
        report.check(info(a)->Status == AsyncStatus::Completed && info(b)->Status == AsyncStatus::Completed && !f.navigates, "initializing Source=null preserves joiners and removes queued navigation");
    }
    {
        InitializationDriver f(207);
        auto a = f.view->EnsureInitializedAsync(), b = f.view->EnsureInitializedAsync();
        f.view->Source = remote(L"https://example.test/before-close");
        auto oldAttempt = f.attempt;
        unsigned canceled = 0;
        bool allCanceled = false;
        a->Completed = ref new AsyncActionCompletedHandler([&](IAsyncAction^, AsyncStatus status) {
            if (status == AsyncStatus::Canceled) ++canceled;
            allCanceled = info(b)->Status == AsyncStatus::Canceled;
        });
        b->Completed = ref new AsyncActionCompletedHandler([&](IAsyncAction^, AsyncStatus status) { if (status == AsyncStatus::Canceled) ++canceled; });
        f.view->Close();
        report.check(canceled == 2 && allCanceled && info(a)->ErrorCode.Value == E_ABORT && info(b)->ErrorCode.Value == E_ABORT, "control Close marks every pending joiner canceled before first handler");
        report.check(f.closes == 1 && !f.navigates && f.view->CompleteInitialization(oldAttempt, f.page, S_OK) == S_FALSE, "Close cancels shared work and ignores delayed readiness");
        report.check(throwsResult([&] { f.view->EnsureInitializedAsync(); }, RO_E_CLOSED), "closed control rejects new initialization callers");
    }
    {
        auto options = ref new WebKitWebViewOptions();
        options->MaximumConcurrentRequests = 3;
        InitializationDriver f(208, options);
        options->MaximumConcurrentRequests = 0;
        options->CertificateAuthorityPath = L"missing-after-construction.pem";
        auto oldA = f.view->EnsureInitializedAsync(), oldB = f.view->EnsureInitializedAsync();
        auto oldAttempt = f.attempt;
        auto holder = f.configuration;
        auto fingerprint = holder ? holder->trustFingerprint() : std::string();
        unsigned errors = 0;
        oldA->Completed = ref new AsyncActionCompletedHandler([&](IAsyncAction^, AsyncStatus status) { if (status == AsyncStatus::Error) ++errors; });
        oldB->Completed = ref new AsyncActionCompletedHandler([&](IAsyncAction^, AsyncStatus status) { if (status == AsyncStatus::Error) ++errors; });
        f.resolve(E_ACCESSDENIED);
        report.check(errors == 2 && info(oldA)->ErrorCode.Value == E_ACCESSDENIED && info(oldB)->ErrorCode.Value == E_ACCESSDENIED, "initialization failure errors all live observations with identical original HRESULT");
        report.check(throwsResult([&] { oldA->GetResults(); }, E_ACCESSDENIED), "failed GetResults preserves originating HRESULT");
        auto retry = f.view->EnsureInitializedAsync();
        report.check(f.initializes == 2 && f.attempt != oldAttempt && f.configuration == holder
                && holder && holder->options().maximumConcurrentRequests == 3 && holder->trustFingerprint() == fingerprint,
            "explicit retry reuses validated frozen options/CA holder despite host bag mutation");
        report.check(f.view->CompleteInitialization(oldAttempt, f.page, S_OK) == S_FALSE && info(retry)->Status == AsyncStatus::Started,
            "stale prior-attempt readiness cannot resolve retry observations");
        f.resolve();
        report.check(info(retry)->Status == AsyncStatus::Completed && info(oldA)->Status == AsyncStatus::Error && info(oldB)->Status == AsyncStatus::Error,
            "retry success cannot change previously failed actions");
    }
    {
        auto options = ref new WebKitWebViewOptions();
        options->MaximumConcurrentRequests = 0;
        InitializationDriver f(209, options);
        auto failed = f.view->EnsureInitializedAsync();
        unsigned lateErrors = 0;
        failed->Completed = ref new AsyncActionCompletedHandler([&](IAsyncAction^, AsyncStatus status) { if (status == AsyncStatus::Error) ++lateErrors; });
        report.check(info(failed)->Status == AsyncStatus::Error && info(failed)->ErrorCode.Value == E_INVALIDARG && lateErrors == 1 && !f.initializes,
            "invalid copied options return a failed action before controller/page use");
        options->MaximumConcurrentRequests = 8;
        auto retry = f.view->EnsureInitializedAsync();
        report.check(info(retry)->Status == AsyncStatus::Error && info(retry)->ErrorCode.Value == E_INVALIDARG && !f.initializes,
            "failed validation retry cannot adopt later mutable host options");
    }
    {
        auto options = ref new WebKitWebViewOptions();
        options->CertificateAuthorityPath = L"missing-initialization-test.pem";
        InitializationDriver f(210, options);
        auto failed = f.view->EnsureInitializedAsync();
        report.check(info(failed)->Status == AsyncStatus::Error && info(failed)->ErrorCode.Value == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) && !f.initializes,
            "missing package CA is an async originating failure before page work");
    }
    {
        InitializationDriver f(211);
        auto a = f.view->EnsureInitializedAsync(), b = f.view->EnsureInitializedAsync();
        f.view->Source = remote(L"https://example.test/original");
        unsigned bCompleted = 0;
        a->Completed = ref new AsyncActionCompletedHandler([&](IAsyncAction^, AsyncStatus) { f.view->Source = remote(L"https://example.test/from-completion"); });
        b->Completed = ref new AsyncActionCompletedHandler([&](IAsyncAction^, AsyncStatus status) { if (status == AsyncStatus::Completed && !f.navigates) ++bCompleted; });
        f.resolve();
        report.check(bCompleted == 1 && f.navigates == 1 && f.target == L"https://example.test/from-completion", "completion-handler Source reentrancy waits for peers and revalidates latest intent");
    }
    {
        InitializationDriver f(212);
        auto a = f.view->EnsureInitializedAsync(), b = f.view->EnsureInitializedAsync();
        f.view->Source = remote(L"https://example.test/never-after-ready-close");
        unsigned completed = 0;
        a->Completed = ref new AsyncActionCompletedHandler([&](IAsyncAction^, AsyncStatus status) { if (status == AsyncStatus::Completed) ++completed; f.view->Close(); });
        b->Completed = ref new AsyncActionCompletedHandler([&](IAsyncAction^, AsyncStatus status) { if (status == AsyncStatus::Completed) ++completed; });
        f.resolve();
        report.check(completed == 2 && info(a)->Status == AsyncStatus::Completed && info(b)->Status == AsyncStatus::Completed && !f.navigates && f.closes == 1,
            "Close in a readiness handler preserves settled peer completion and suppresses queued navigation");
    }
}

} // namespace

// Diagnostic-only export: exercise actual IAsyncAction/IAsyncInfo on the XAML owner.
extern "C" __declspec(dllexport) HRESULT __stdcall WKW10MTestInitialization(const wchar_t* path)
{
    InitializationReport report;
    try { runInitialization(report); }
    catch (Exception^ exception) {
        report.check(false, "uncaught initialization test exception");
        report.lines += "HRESULT=" + std::to_string(exception->HResult) + "\r\n";
    } catch (...) { report.check(false, "uncaught native initialization test exception"); }
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
