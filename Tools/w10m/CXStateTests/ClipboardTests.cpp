// Diagnostic-only clipboard routing over the real dispatcher and WinRT transport.
// Assertions exercise actual copy/cut/paste round trips, worker-thread
// rejection and delayed completion after Close on the real control owner.
#include "platform/ClipboardRouteW10M.h"
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
struct ClipboardReport {
    // Number of assertions evaluated on the real clipboard stack.
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

// Route completion record; every delivery runs on the UI owner thread.
struct ClipboardDeviceLog {
    // Number of route completion invocations observed so far.
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
        auto* self = static_cast<ClipboardDeviceLog*>(context);
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

// Engine-side service completion count for table-interop cases.
unsigned g_engineWriteCompletions = 0;
// Last engine-side service completion result.
WKW10MResult g_engineWriteResult = WKW10MResultOK;
// Record one engine-side service completion without retaining anything.
void __stdcall recordEngineWrite(void*, uint64_t, WKW10MResult result, const WKW10MString*)
{
    ++g_engineWriteCompletions;
    g_engineWriteResult = result;
}

// Borrowed span over a caller-owned buffer for copy/cut requests.
WKW10MString deviceSpan(const std::string& bytes)
{
    WKW10MString span { reinterpret_cast<const uint8_t*>(bytes.data()), static_cast<uint32_t>(bytes.size()) };
    return span;
}

// Build one route on the real dispatcher and WinRT clipboard transport.
std::unique_ptr<ClipboardRouteW10M> makeDeviceRoute(DispatcherW10M^ dispatcher)
{
    auto service = std::make_unique<ClipboardServiceW10M>(
        CreatePlatformServiceDispatcher(dispatcher),
        CreateClipboardPlatformTransport());
    return std::make_unique<ClipboardRouteW10M>(11, std::move(service));
}

// Pump the owner dispatcher until the route drains or the budget expires.
// Async WinRT reads complete through RunAsync tasks, so the UI thread must
// pump: blocking on an event here would deadlock the delivery it waits for.
bool drainDeviceRoute(ClipboardRouteW10M& route, CoreDispatcher^ core, unsigned budgetMs = 8000)
{
    for (unsigned waited = 0; route.pendingCount() && waited < budgetMs; waited += 10) {
        core->ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
        Sleep(10);
    }
    return route.pendingCount() == 0;
}

// Exercise the real clipboard stack and record every assertion.
void runClipboard(ClipboardReport& report)
{
    auto window = CoreWindow::GetForCurrentThread();
    report.check(!!window, "clipboard suite runs with a current CoreWindow");
    if (!window)
        return;
    auto core = window->Dispatcher;
    auto dispatcher = ref new DispatcherW10M(core);
    report.check(!!dispatcher->HasThreadAccess(), "diagnostic runs on the owning dispatcher thread");

    auto route = makeDeviceRoute(dispatcher);
    ClipboardDeviceLog log;

    // Best-effort save of whatever text the device clipboard holds, so the run
    // restores user state at the end. Only our own tagged strings are asserted.
    std::string savedText;
    bool savedOk = false;
    {
        ClipboardDeviceLog saved;
        if (route->paste(&ClipboardDeviceLog::record, &saved) == S_OK && drainDeviceRoute(*route, core)) {
            savedText = saved.lastText;
            savedOk = saved.count == 1 && saved.lastResult == WKW10MResultOK;
        }
    }
    report.check(savedOk, "existing clipboard text saved before the run");

    // Copy publishes through the real transport and completes inline.
    std::string copyText = std::string("clipboard-") + TEST_PACKAGE_VERSION + "-copy";
    WKW10MString copySpan = deviceSpan(copyText);
    report.check(route->copy(&copySpan, &ClipboardDeviceLog::record, &log) == S_OK, "device copy accepted");
    report.check(log.count == 1 && log.lastResult == WKW10MResultOK, "device copy completes OK");
    report.check(route->pendingCount() == 0, "device copy leaves nothing pending");

    // Paste round-trips the exact published bytes through the real clipboard.
    ClipboardDeviceLog pasted;
    report.check(route->paste(&ClipboardDeviceLog::record, &pasted) == S_OK, "device paste accepted");
    report.check(drainDeviceRoute(*route, core), "device paste drains through the dispatcher");
    report.check(pasted.count == 1 && pasted.lastResult == WKW10MResultOK, "device paste completes OK");
    report.check(pasted.lastText == copyText, "device paste round-trips exact copy bytes");

    // Cut publishes like copy and marks the completion cut.
    std::string cutText = std::string("clipboard-") + TEST_PACKAGE_VERSION + "-cut";
    WKW10MString cutSpan = deviceSpan(cutText);
    ClipboardDeviceLog cut;
    report.check(route->cut(&cutSpan, &ClipboardDeviceLog::record, &cut) == S_OK, "device cut accepted");
    report.check(cut.count == 1 && cut.lastResult == WKW10MResultOK && cut.lastCut
        && cut.lastKind == WKW10MClipboardWrite, "device cut completes OK marked cut");
    ClipboardDeviceLog cutPaste;
    report.check(route->paste(&ClipboardDeviceLog::record, &cutPaste) == S_OK, "paste after cut accepted");
    report.check(drainDeviceRoute(*route, core) && cutPaste.count == 1
        && cutPaste.lastText == cutText, "paste after cut round-trips exact cut bytes");

    // Worker-thread clipboard calls are rejected before any token exists.
    HANDLE done { CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS) };
    std::vector<HRESULT> workerResults;
    auto routePtr = route.get();
    WKW10MString workerSpan = deviceSpan(copyText);
    ThreadPool::RunAsync(ref new WorkItemHandler([&](IAsyncAction^) {
        workerResults.push_back(routePtr->copy(&workerSpan, &ClipboardDeviceLog::record, &log));
        workerResults.push_back(routePtr->paste(&ClipboardDeviceLog::record, &log));
        workerResults.push_back(routePtr->close());
        SetEvent(done);
    }));
    bool finished = WaitForSingleObjectEx(done, 5000, FALSE) == WAIT_OBJECT_0;
    CloseHandle(done);
    bool wrongThread = finished && workerResults.size() == 3;
    if (finished) for (auto result : workerResults) wrongThread = wrongThread && result == RPC_E_WRONG_THREAD;
    report.check(wrongThread, "worker copy/paste/close report RPC_E_WRONG_THREAD");
    report.check(route->pendingCount() == 0, "worker attempts register nothing");

    // A read closed while in flight still completes exactly once, whether the
    // transport or the close wins the race; the route stays alive until drained.
    auto closing = makeDeviceRoute(dispatcher);
    ClipboardDeviceLog raced;
    report.check(closing->paste(&ClipboardDeviceLog::record, &raced) == S_OK, "close-race paste accepted");
    report.check(closing->close() == S_OK, "close-race route closed");
    report.check(drainDeviceRoute(*closing, core), "close-race route drains");
    bool singleTerminal = raced.count == 1
        && (raced.lastResult == WKW10MResultOK || raced.lastResult == WKW10MResultClosed);
    report.check(singleTerminal, "close-race paste completes exactly once as OK or Closed");
    report.check(closing->pendingCount() == 0, "close-race route leaves nothing pending");

    // Operations after close fail Closed without completing.
    auto ended = makeDeviceRoute(dispatcher);
    report.check(ended->close() == S_OK, "empty route closes");
    ClipboardDeviceLog closed;
    report.check(ended->paste(&ClipboardDeviceLog::record, &closed) == RO_E_CLOSED, "paste after close rejected Closed");
    report.check(closed.count == 0, "post-close paste never completes");

    // The borrowed table serves engine writes alongside route operations.
    WKW10MPlatformServices table { };
    report.check(route->buildTable(&table) == S_OK, "device route table built");
    WKW10MString empty { nullptr, 0 };
    g_engineWriteCompletions = 0;
    g_engineWriteResult = WKW10MResultOK;
    auto engineResult = table.requestClipboard(table.context, 11, 600, WKW10MClipboardWrite, &empty,
        &recordEngineWrite, nullptr);
    report.check(engineResult == WKW10MResultOK && g_engineWriteCompletions == 1
        && g_engineWriteResult == WKW10MResultOK, "engine write through device table completes OK");

    // Restore the saved clipboard text without asserting user state beyond no-fault delivery.
    bool restored = false;
    if (savedOk) {
        ClipboardDeviceLog restore;
        WKW10MString savedSpan = deviceSpan(savedText);
        restored = route->copy(&savedSpan, &ClipboardDeviceLog::record, &restore) == S_OK
            && restore.count == 1 && restore.lastResult == WKW10MResultOK;
    }
    report.check(restored, "saved clipboard text restored without fault");
}

} // namespace

// Diagnostic-only owner-thread entry: verify real clipboard routing behavior.
extern "C" __declspec(dllexport) HRESULT __stdcall WKW10MTestClipboard(const wchar_t* path)
{
    ClipboardReport report;
    try { runClipboard(report); }
    catch (Exception^ exception) {
        report.check(false, "uncaught clipboard matrix exception");
        report.lines += "HRESULT=" + std::to_string(exception->HResult) + "\r\n";
    } catch (...) { report.check(false, "uncaught native clipboard matrix exception"); }
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
