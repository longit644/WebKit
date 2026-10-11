// Diagnostic-only runtime owner over the real dispatcher and WinRT transport.
// Assertions exercise ticketed copy/cut/paste, hook revocation, shutdown with
// acknowledged cancellation, close/recreate generations and quiescence on the
// real control owner. Engine pinning stays a host proof: binding the real
// engine inside this probe would start real engine work the run cannot grade.
#include "platform/RuntimeOwnerW10M.h"
#include "platform/ClipboardPlatformW10M.h"
#include "platform/DispatcherW10M.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::System::Threading;
using namespace Windows::UI::Core;
using namespace WebKit;

namespace {

// Fail-closed per-case assertions saved with the diagnostic package version.
struct OwnerReport {
    // Number of assertions evaluated on the real runtime stack.
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

// Ticket completion record; every delivery runs on the UI owner thread.
struct OwnerDeviceLog {
    // Number of ticket completion invocations observed so far.
    unsigned count { 0 };
    // Operation ID carried by the most recent completion.
    uint64_t lastId { 0 };
    // Frozen kind carried by the most recent completion.
    uint32_t lastKind { 0 };
    // Cut marker carried by the most recent completion.
    bool lastCut { false };
    // Result carried by the most recent completion.
    WKW10MResult lastResult { WKW10MResultOK };
    // Text bytes copied out of the borrowed span during the call.
    std::string lastText;
    // Record one delivery into the owning log.
    static void __stdcall record(void* context, uint64_t operationId, uint32_t kind, bool cut,
        WKW10MResult result, const WKW10MString* text)
    {
        auto* self = static_cast<OwnerDeviceLog*>(context);
        ++self->count;
        self->lastId = operationId;
        self->lastKind = kind;
        self->lastCut = cut;
        self->lastResult = result;
        self->lastText.clear();
        if (text && text->length && text->data)
            self->lastText.assign(reinterpret_cast<const char*>(text->data), text->length);
    }
};

// Hook revocation record; every revocation runs on the UI owner thread.
static unsigned g_ownerHookCount = 0;
static uint64_t g_ownerHookLastId = 0;
static WKW10MResult g_ownerHookLastResult = WKW10MResultOK;

// Record one hook revocation.
static void __stdcall recordOwnerHook(void*, uint64_t hookId, WKW10MResult result)
{
    ++g_ownerHookCount;
    g_ownerHookLastId = hookId;
    g_ownerHookLastResult = result;
}

// Borrowed span over a caller-owned buffer for copy/cut requests.
WKW10MString ownerSpan(const std::string& bytes)
{
    WKW10MString span { reinterpret_cast<const uint8_t*>(bytes.data()), static_cast<uint32_t>(bytes.size()) };
    return span;
}

// Build one runtime owner on the real dispatcher and WinRT clipboard transport.
std::unique_ptr<RuntimeOwnerW10M> makeDeviceOwner(DispatcherW10M^ dispatcher)
{
    auto ports = std::make_unique<RuntimeOwnerW10M>(13,
        CreatePlatformServiceDispatcher(dispatcher),
        CreateClipboardPlatformTransport());
    return ports;
}

// Pump the owner dispatcher until the owner drains or the budget expires.
// Async WinRT reads complete through RunAsync tasks, so the UI thread must
// pump: blocking on an event here would deadlock the delivery it waits for.
bool drainDeviceOwner(RuntimeOwnerW10M& owner, CoreDispatcher^ core, unsigned budgetMs = 8000)
{
    for (unsigned waited = 0; owner.pendingCount() && waited < budgetMs; waited += 10) {
        core->ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
        Sleep(10);
    }
    return owner.pendingCount() == 0;
}

// Exercise the real runtime stack and record every assertion.
void runOwner(OwnerReport& report)
{
    auto window = CoreWindow::GetForCurrentThread();
    report.check(!!window, "owner suite runs with a current CoreWindow");
    if (!window)
        return;
    auto core = window->Dispatcher;
    auto dispatcher = ref new DispatcherW10M(core);
    report.check(!!dispatcher->HasThreadAccess(), "diagnostic runs on the owning dispatcher thread");

    auto owner = makeDeviceOwner(dispatcher);
    unsigned accepted = 0;
    OwnerDeviceLog log;

    // Best-effort save of whatever text the device clipboard holds, so the run
    // restores user state at the end. Only our own tagged strings are asserted.
    std::string savedText;
    bool savedOk = false;
    {
        OwnerDeviceLog saved;
        if (owner->paste(&OwnerDeviceLog::record, &saved) == S_OK) {
            ++accepted;
            if (drainDeviceOwner(*owner, core)) {
                savedText = saved.lastText;
                savedOk = saved.count == 1 && saved.lastResult == WKW10MResultOK;
            }
        }
    }
    report.check(savedOk, "existing clipboard text saved before the run");

    // Ticketed copy publishes through the real transport and completes inline.
    std::string copyText = std::string("owner-") + TEST_PACKAGE_VERSION + "-copy";
    WKW10MString copySpan = ownerSpan(copyText);
    report.check(owner->copy(&copySpan, &OwnerDeviceLog::record, &log) == S_OK, "device owner copy accepted");
    ++accepted;
    report.check(log.count == 1 && log.lastResult == WKW10MResultOK, "device owner copy completes OK");
    report.check(owner->openTickets() == 0, "copy ticket acknowledged");
    report.check(owner->quiescent(), "quiescent after copy");

    // Paste round-trips the exact published bytes through the real clipboard.
    OwnerDeviceLog pasted;
    report.check(owner->paste(&OwnerDeviceLog::record, &pasted) == S_OK, "device owner paste accepted");
    ++accepted;
    report.check(drainDeviceOwner(*owner, core), "device owner paste drains through the dispatcher");
    report.check(pasted.count == 1 && pasted.lastResult == WKW10MResultOK, "device owner paste completes OK");
    report.check(pasted.lastText == copyText, "device owner paste round-trips exact copy bytes");

    // Cut publishes like copy and marks the completion cut.
    std::string cutText = std::string("owner-") + TEST_PACKAGE_VERSION + "-cut";
    WKW10MString cutSpan = ownerSpan(cutText);
    OwnerDeviceLog cut;
    report.check(owner->cut(&cutSpan, &OwnerDeviceLog::record, &cut) == S_OK, "device owner cut accepted");
    ++accepted;
    report.check(cut.count == 1 && cut.lastResult == WKW10MResultOK && cut.lastCut, "device owner cut completes OK marked cut");

    // Worker-thread owner calls are rejected before any ticket exists.
    HANDLE done { CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS) };
    std::vector<HRESULT> workerResults;
    auto ownerPtr = owner.get();
    WKW10MString workerSpan = ownerSpan(copyText);
    ThreadPool::RunAsync(ref new WorkItemHandler([&](IAsyncAction^) {
        workerResults.push_back(ownerPtr->copy(&workerSpan, &OwnerDeviceLog::record, &log));
        workerResults.push_back(ownerPtr->paste(&OwnerDeviceLog::record, &log));
        workerResults.push_back(ownerPtr->closeClipboard());
        SetEvent(done);
    }));
    bool finished = WaitForSingleObjectEx(done, 5000, FALSE) == WAIT_OBJECT_0;
    CloseHandle(done);
    bool wrongThread = finished && workerResults.size() == 3;
    if (finished) for (auto result : workerResults) wrongThread = wrongThread && result == RPC_E_WRONG_THREAD;
    report.check(wrongThread, "worker copy/paste/close report RPC_E_WRONG_THREAD");
    report.check(owner->openTickets() == 0, "worker attempts open no ticket");

    // Close and recreate generations keep totals with pending reset.
    unsigned completedBefore = owner->completedTotal();
    report.check(owner->closeClipboard() == S_OK, "device clipboard closed");
    std::unique_ptr<IServiceDispatcherW10M> freshDispatcher = CreatePlatformServiceDispatcher(dispatcher);
    std::unique_ptr<IClipboardTransportW10M> freshTransport = CreateClipboardPlatformTransport();
    report.check(owner->recreateClipboard(std::move(freshDispatcher), std::move(freshTransport)) == S_OK, "device clipboard recreated");
    report.check(owner->generation() == 1, "device generation advances on recreate");
    report.check(owner->completedTotal() == completedBefore, "recreate carries acknowledgment totals");
    OwnerDeviceLog recreated;
    report.check(owner->copy(&copySpan, &OwnerDeviceLog::record, &recreated) == S_OK, "post-recreate copy accepted");
    ++accepted;
    report.check(recreated.count == 1 && recreated.lastResult == WKW10MResultOK, "post-recreate copy acknowledged");

    // Hooks revoke explicitly and at shutdown with exactly-once delivery.
    uint64_t firstHook = 0, secondHook = 0;
    int hookToken = 13;
    report.check(owner->registerHook(&recordOwnerHook, &hookToken, &firstHook) == S_OK, "first device hook registered");
    report.check(owner->registerHook(&recordOwnerHook, &hookToken, &secondHook) == S_OK
        && secondHook != firstHook, "second device hook registered with fresh ID");
    report.check(owner->revokeHook(firstHook) == S_OK, "first device hook revoked");
    report.check(g_ownerHookCount == 1 && g_ownerHookLastId == firstHook
        && g_ownerHookLastResult == WKW10MResultClosed, "explicit revocation acknowledges Closed once with ID");

    // Shutdown acknowledges every accepted ticket, revokes the open hook, loses
    // the dispatcher and stays quiescent; later work fails fail-closed.
    report.check(owner->beginShutdown() == S_OK, "device shutdown accepted while unbound");
    report.check(owner->isShuttingDown() && owner->dispatcherLost(), "device shutdown latches both flags");
    report.check(owner->quiescent(), "quiescent after device shutdown");
    report.check(g_ownerHookCount == 2 && g_ownerHookLastId == secondHook
        && g_ownerHookLastResult == WKW10MResultClosed, "shutdown revokes the open hook Closed");
    report.check(owner->postWake() == WKW10MResultDispatcherUnavailable, "post-shutdown wake unavailable");
    OwnerDeviceLog closed;
    report.check(owner->copy(&copySpan, &OwnerDeviceLog::record, &closed) == RO_E_CLOSED, "post-shutdown copy rejected");
    report.check(closed.count == 0, "post-shutdown copy never completes");
    report.check(owner->completedTotal() + owner->canceledTotal() == accepted, "every accepted ticket acknowledged exactly once");

    // The engine-facing table serves engine writes with a working postWake.
    WKW10MPlatformServices table { };
    report.check(owner->buildTable(&table) == S_OK, "device owner table built");
    report.check(table.context == owner.get() && table.postWake, "device table carries owner context and wake");
    WKW10MString empty { nullptr, 0 };
    OwnerDeviceLog engine;
    auto engineResult = table.requestClipboard(table.context, 13, 1300, WKW10MClipboardWrite, &empty,
        [](void* context, uint64_t, WKW10MResult result, const WKW10MString*) {
            auto* log = static_cast<OwnerDeviceLog*>(context);
            ++log->count;
            log->lastResult = result;
        }, &engine);
    report.check(engineResult == WKW10MResultShuttingDown, "post-shutdown engine request refused ShuttingDown");

    // Restore the saved clipboard text without asserting user state beyond no-fault delivery.
    bool restored = false;
    if (savedOk) {
        auto rescue = makeDeviceOwner(dispatcher);
        OwnerDeviceLog restore;
        WKW10MString savedSpan = ownerSpan(savedText);
        restored = rescue->copy(&savedSpan, &OwnerDeviceLog::record, &restore) == S_OK
            && restore.count == 1 && restore.lastResult == WKW10MResultOK;
    }
    report.check(restored, "saved clipboard text restored without fault");
}

} // namespace

// Diagnostic-only owner-thread entry: verify real runtime owner behavior.
extern "C" __declspec(dllexport) HRESULT __stdcall WKW10MTestOwner(const wchar_t* path)
{
    OwnerReport report;
    try { runOwner(report); }
    catch (Exception^ exception) {
        report.check(false, "uncaught owner matrix exception");
        report.lines += "HRESULT=" + std::to_string(exception->HResult) + "\r\n";
    } catch (...) { report.check(false, "uncaught native owner matrix exception"); }
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
