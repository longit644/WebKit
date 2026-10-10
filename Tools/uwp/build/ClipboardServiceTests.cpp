// Verifies the host-side V1 clipboard service behind neutral callbacks.
//
// The harness links ClipboardServiceUWP.cpp directly and drives it through
// scripted dispatcher/transport doubles, so every completion, cancellation and
// validation rule below runs on this host. WinRT behavior belongs to the CX
// transport and is proven by the ARM compile plus later device tasks.

#include "ClipboardServiceUWP.h"

#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace WebKit;

// Completed-completion record shared by every test below.
struct CompletionLog {
    // Guards the counters against worker-thread transport completions.
    std::mutex lock;
    // Number of completion invocations observed so far.
    unsigned count { 0 };
    // Operation ID carried by the most recent completion.
    uint64_t lastId { 0 };
    // Result carried by the most recent completion.
    WKUWPResult lastResult { WKUWPResultOK };
    // Text bytes copied out of the borrowed span during the call.
    std::string lastText;
    // Context carried by the most recent completion.
    void* lastContext { nullptr };
    // Whether the borrowed span was readable for its full length during the call.
    bool spanReadable { false };
};

static CompletionLog g_log;

// Record one completion and prove the borrowed span stays readable inside it.
static void __stdcall RecordCompletion(void* context, uint64_t operationId, WKUWPResult result, const WKUWPString* text)
{
    std::lock_guard<std::mutex> guard(g_log.lock);
    ++g_log.count;
    g_log.lastId = operationId;
    g_log.lastResult = result;
    g_log.lastContext = context;
    g_log.lastText.clear();
    g_log.spanReadable = false;
    if (!text)
        return;
    if (text->length && !text->data)
        return;
    if (text->length)
        g_log.lastText.assign(reinterpret_cast<const char*>(text->data), text->length);
    g_log.spanReadable = true;
}

// Forget earlier completions before the next case.
static void ResetLog()
{
    std::lock_guard<std::mutex> guard(g_log.lock);
    g_log.count = 0;
    g_log.lastId = 0;
    g_log.lastResult = WKUWPResultOK;
    g_log.lastText.clear();
    g_log.lastContext = nullptr;
    g_log.spanReadable = false;
}

static int g_checks = 0;
static int g_failures = 0;

// Count one named check and report it immediately.
#define EXPECT_TRUE(condition, name) \
    do { \
        ++g_checks; \
        if (condition) { \
            printf("ok %d - %s\n", g_checks, name); \
        } else { \
            ++g_failures; \
            printf("NOT OK %d - %s\n", g_checks, name); \
        } \
    } while (0)

// Dispatcher double with inline, capture and refuse delivery modes.
class FakeDispatcherUWP final : public IServiceDispatcherUWP {
public:
    // Delivery behavior selected per test case.
    enum class Mode { Inline, Capture, Refuse };

    // Report owner affinity; only the constructing thread owns the service.
    bool hasThreadAccess() override { return true; }

    // Deliver, capture or refuse one posted task.
    bool post(std::function<void()>&& task) override
    {
        if (m_mode == Mode::Refuse)
            return false;
        if (m_mode == Mode::Capture) {
            m_captured.push_back(std::move(task));
            return true;
        }
        task();
        return true;
    }

    // Select the delivery behavior for the next posted task.
    void setMode(Mode mode) { m_mode = mode; }
    // Tasks held back by capture mode.
    std::vector<std::function<void()>> m_captured;
    // Current delivery behavior.
    Mode m_mode { Mode::Inline };
};

// Transport double with scripted writes and controllable read completions.
class FakeTransportUWP final : public IClipboardTransportUWP {
public:
    // Answer one write with the scripted result.
    HRESULT writeText(const std::string& utf8) override
    {
        m_written = utf8;
        return m_writeResult;
    }

    // Complete inline or hold the completion for the test to invoke.
    void readText(std::function<void(HRESULT, std::string)> done) override
    {
        if (m_manual) {
            m_pending.push_back(std::move(done));
            return;
        }
        done(m_readResult, m_readBytes);
    }

    // Result reported for the next write.
    HRESULT m_writeResult { S_OK };
    // Bytes retained from the most recent write.
    std::string m_written;
    // Whether reads wait for an explicit test invocation.
    bool m_manual { false };
    // Result reported for the next inline read.
    HRESULT m_readResult { S_OK };
    // Bytes reported for the next inline read.
    std::string m_readBytes;
    // Held read completions awaiting test invocation.
    std::vector<std::function<void(HRESULT, std::string)>> m_pending;
};

// Borrowed empty span reused for write requests, which carry no read text.
static WKUWPString g_empty { nullptr, 0 };

// Borrowed span over a caller-owned buffer for write requests.
static WKUWPString MakeSpan(const std::string& bytes)
{
    WKUWPString span { reinterpret_cast<const uint8_t*>(bytes.data()), static_cast<uint32_t>(bytes.size()) };
    return span;
}

// Build one service with scripted doubles owned by the caller.
static std::unique_ptr<ClipboardServiceUWP> MakeService(FakeDispatcherUWP*& dispatcher, FakeTransportUWP*& transport)
{
    auto ownedDispatcher = std::make_unique<FakeDispatcherUWP>();
    auto ownedTransport = std::make_unique<FakeTransportUWP>();
    dispatcher = ownedDispatcher.get();
    transport = ownedTransport.get();
    return std::make_unique<ClipboardServiceUWP>(std::move(ownedDispatcher), std::move(ownedTransport));
}

// Runs every clipboard service case on this host; the WinRT transport is
// scripted out, so no WinRT apartment or device is required.
int main()
{
    // Frozen clipboard kinds keep their contract values through the new enum.
    uint32_t readKind = static_cast<uint32_t>(ClipboardKindUWP::Read);
    uint32_t writeKind = static_cast<uint32_t>(ClipboardKindUWP::Write);
    EXPECT_TRUE(readKind == WKUWPClipboardRead, "kind read is 0");
    EXPECT_TRUE(writeKind == WKUWPClipboardWrite, "kind write is 1");

    FakeDispatcherUWP* dispatcher = nullptr;
    FakeTransportUWP* transport = nullptr;
    auto service = MakeService(dispatcher, transport);
    EXPECT_TRUE(service->checkThread() == S_OK, "owner thread accepted");
    EXPECT_TRUE(service->pendingCount() == 0, "no pending operations initially");
    EXPECT_TRUE(service->droppedEnqueues() == 0, "no dropped enqueues initially");
    EXPECT_TRUE(service->contextRefCount() == 1, "single context retain initially");

    // A successful write completes OK inline with an empty readable span.
    ResetLog();
    std::string copied = "copy me";
    WKUWPString writeSpan = MakeSpan(copied);
    int token = 7;
    EXPECT_TRUE(service->requestClipboard(1, 11, WKUWPClipboardWrite, &writeSpan, &RecordCompletion, &token) == S_OK, "write accepted");
    EXPECT_TRUE(g_log.count == 1, "write completes exactly once");
    EXPECT_TRUE(g_log.lastResult == WKUWPResultOK, "write completes OK");
    EXPECT_TRUE(g_log.lastId == 11, "write completion carries operation ID");
    EXPECT_TRUE(g_log.lastContext == &token, "write completion carries context");
    EXPECT_TRUE(g_log.spanReadable && g_log.lastText.empty(), "write span readable and empty");
    EXPECT_TRUE(transport->m_written == copied, "transport receives exact write bytes");
    EXPECT_TRUE(service->pendingCount() == 0, "write leaves nothing pending");

    // A failed write completes once with the originating failure preserved.
    ResetLog();
    transport->m_writeResult = E_ACCESSDENIED;
    EXPECT_TRUE(service->requestClipboard(1, 12, WKUWPClipboardWrite, &writeSpan, &RecordCompletion, &token) == E_ACCESSDENIED, "failed write returns origin");
    EXPECT_TRUE(g_log.count == 1, "failed write completes exactly once");
    EXPECT_TRUE(g_log.lastResult == static_cast<WKUWPResult>(E_ACCESSDENIED), "failed write preserves origin");
    EXPECT_TRUE(service->pendingCount() == 0, "failed write leaves nothing pending");
    transport->m_writeResult = S_OK;

    // Malformed requests are rejected before any completion can exist.
    ResetLog();
    EXPECT_TRUE(service->requestClipboard(1, 13, 2, &g_empty, &RecordCompletion, &token) == E_INVALIDARG, "unknown kind rejected");
    EXPECT_TRUE(service->requestClipboard(1, 13, WKUWPClipboardWrite, &g_empty, nullptr, &token) == E_INVALIDARG, "null completion rejected");
    EXPECT_TRUE(service->requestClipboard(1, 13, WKUWPClipboardWrite, nullptr, &RecordCompletion, &token) == E_INVALIDARG, "null text rejected");
    WKUWPString dangling { nullptr, 4 };
    EXPECT_TRUE(service->requestClipboard(1, 13, WKUWPClipboardWrite, &dangling, &RecordCompletion, &token) == E_INVALIDARG, "dangling span rejected");
    EXPECT_TRUE(g_log.count == 0, "rejected requests never complete");
    EXPECT_TRUE(service->pendingCount() == 0, "rejected requests leave nothing pending");

    // A missing port fails the request instead of faulting.
    {
        FakeDispatcherUWP* unusedDispatcher = nullptr;
        FakeTransportUWP* unusedTransport = nullptr;
        auto noDispatcher = std::make_unique<ClipboardServiceUWP>(nullptr, std::make_unique<FakeTransportUWP>());
        auto noTransport = std::make_unique<ClipboardServiceUWP>(std::make_unique<FakeDispatcherUWP>(), nullptr);
        static_cast<void>(unusedDispatcher);
        static_cast<void>(unusedTransport);
        EXPECT_TRUE(noDispatcher->requestClipboard(1, 14, WKUWPClipboardWrite, &g_empty, &RecordCompletion, &token) == E_INVALIDARG, "null dispatcher rejected");
        EXPECT_TRUE(noTransport->requestClipboard(1, 14, WKUWPClipboardWrite, &g_empty, &RecordCompletion, &token) == E_INVALIDARG, "null transport rejected");
    }

    // A same-thread read completes inline with the exact transport bytes.
    ResetLog();
    transport->m_readResult = S_OK;
    transport->m_readBytes = "pasted \xe2\x9c\x93";
    EXPECT_TRUE(service->requestClipboard(2, 20, WKUWPClipboardRead, &g_empty, &RecordCompletion, &token) == S_OK, "read accepted");
    EXPECT_TRUE(g_log.count == 1, "read completes exactly once");
    EXPECT_TRUE(g_log.lastResult == WKUWPResultOK, "read completes OK");
    EXPECT_TRUE(g_log.lastText == transport->m_readBytes, "read delivers exact bytes");
    EXPECT_TRUE(g_log.spanReadable, "read span readable during call");
    EXPECT_TRUE(service->pendingCount() == 0, "read leaves nothing pending");

    // A duplicate ID is rejected while the first operation stays pending.
    ResetLog();
    transport->m_manual = true;
    EXPECT_TRUE(service->requestClipboard(2, 21, WKUWPClipboardRead, &g_empty, &RecordCompletion, &token) == S_OK, "first read accepted");
    EXPECT_TRUE(service->requestClipboard(2, 21, WKUWPClipboardRead, &g_empty, &RecordCompletion, &token) == E_INVALIDARG, "duplicate ID rejected");
    EXPECT_TRUE(service->pendingCount() == 1, "original operation stays pending");
    EXPECT_TRUE(transport->m_pending.size() == 1, "one transport read outstanding");
    transport->m_pending[0](S_OK, "first");
    transport->m_pending.clear();
    EXPECT_TRUE(g_log.count == 1 && g_log.lastText == "first", "original read still completes");
    transport->m_manual = false;

    // An ID is reusable once its operation has completed.
    ResetLog();
    EXPECT_TRUE(service->requestClipboard(2, 21, WKUWPClipboardWrite, &g_empty, &RecordCompletion, &token) == S_OK, "ID reused after completion");
    EXPECT_TRUE(g_log.count == 1 && g_log.lastId == 21, "reused ID completes");

    // A repeated transport completion is ignored after the first delivery.
    ResetLog();
    transport->m_manual = true;
    EXPECT_TRUE(service->requestClipboard(2, 22, WKUWPClipboardRead, &g_empty, &RecordCompletion, &token) == S_OK, "guarded read accepted");
    transport->m_pending[0](S_OK, "once");
    transport->m_pending[0](S_OK, "twice");
    transport->m_pending.clear();
    EXPECT_TRUE(g_log.count == 1 && g_log.lastText == "once", "second completion ignored");
    EXPECT_TRUE(service->pendingCount() == 0, "guarded read leaves nothing pending");
    transport->m_manual = false;

    // Canceling an accepted read completes Closed synchronously and exactly once.
    ResetLog();
    transport->m_manual = true;
    EXPECT_TRUE(service->requestClipboard(2, 23, WKUWPClipboardRead, &g_empty, &RecordCompletion, &token) == S_OK, "cancelable read accepted");
    EXPECT_TRUE(service->cancelClipboard(23) == S_OK, "cancel accepted");
    EXPECT_TRUE(g_log.count == 1, "cancel completes exactly once");
    EXPECT_TRUE(g_log.lastResult == WKUWPResultClosed, "cancel completes Closed");
    EXPECT_TRUE(g_log.spanReadable && g_log.lastText.empty(), "cancel span readable and empty");
    EXPECT_TRUE(service->pendingCount() == 0, "cancel leaves nothing pending");
    transport->m_pending[0](S_OK, "late");
    transport->m_pending.clear();
    EXPECT_TRUE(g_log.count == 1, "late transport completion ignored after cancel");
    EXPECT_TRUE(service->cancelClipboard(99) == E_INVALIDARG, "unknown cancel rejected");
    EXPECT_TRUE(g_log.count == 1, "rejected cancel never completes");
    transport->m_manual = false;

    // A worker-thread transport completion rides the dispatcher back to the owner.
    ResetLog();
    dispatcher->setMode(FakeDispatcherUWP::Mode::Capture);
    transport->m_manual = true;
    EXPECT_TRUE(service->requestClipboard(2, 24, WKUWPClipboardRead, &g_empty, &RecordCompletion, &token) == S_OK, "marshaled read accepted");
    std::thread worker([captured = std::move(transport->m_pending[0])]() mutable {
        captured(S_OK, "from worker");
    });
    transport->m_pending.clear();
    worker.join();
    EXPECT_TRUE(g_log.count == 0, "worker completion does not run inline");
    EXPECT_TRUE(dispatcher->m_captured.size() == 1, "one task posted to the owner");
    EXPECT_TRUE(service->pendingCount() == 1, "operation stays pending until delivery");
    dispatcher->m_captured[0]();
    dispatcher->m_captured.clear();
    EXPECT_TRUE(g_log.count == 1, "posted task completes exactly once");
    EXPECT_TRUE(g_log.lastResult == WKUWPResultOK && g_log.lastText == "from worker", "posted task delivers worker bytes");
    EXPECT_TRUE(service->pendingCount() == 0, "delivery leaves nothing pending");
    dispatcher->setMode(FakeDispatcherUWP::Mode::Inline);
    transport->m_manual = false;

    // A refused dispatcher enqueue is counted and stays pending until cancel.
    ResetLog();
    dispatcher->setMode(FakeDispatcherUWP::Mode::Refuse);
    transport->m_manual = true;
    EXPECT_TRUE(service->requestClipboard(2, 25, WKUWPClipboardRead, &g_empty, &RecordCompletion, &token) == S_OK, "refused read accepted");
    std::thread refuser([captured = std::move(transport->m_pending[0])]() mutable {
        captured(S_OK, "lost");
    });
    transport->m_pending.clear();
    refuser.join();
    EXPECT_TRUE(g_log.count == 0, "refused completion never runs");
    EXPECT_TRUE(service->droppedEnqueues() == 1, "refused enqueue counted");
    EXPECT_TRUE(service->pendingCount() == 1, "refused operation stays pending");
    EXPECT_TRUE(service->cancelClipboard(25) == S_OK, "refused operation cancelable");
    EXPECT_TRUE(g_log.count == 1 && g_log.lastResult == WKUWPResultClosed, "refused operation closes on cancel");
    dispatcher->setMode(FakeDispatcherUWP::Mode::Inline);
    transport->m_manual = false;

    // Calls from any other thread are rejected before any argument is consumed.
    ResetLog();
    transport->m_manual = true;
    HRESULT threadRequest = S_OK;
    HRESULT threadCancel = S_OK;
    std::thread stranger([&] {
        threadRequest = service->requestClipboard(2, 26, WKUWPClipboardRead, &g_empty, &RecordCompletion, &token);
        threadCancel = service->cancelClipboard(26);
    });
    stranger.join();
    EXPECT_TRUE(threadRequest == RPC_E_WRONG_THREAD, "foreign request rejected");
    EXPECT_TRUE(threadCancel == RPC_E_WRONG_THREAD, "foreign cancel rejected");
    EXPECT_TRUE(g_log.count == 0, "foreign calls never complete");
    EXPECT_TRUE(service->pendingCount() == 0, "foreign calls register nothing");
    transport->m_manual = false;

    // The borrowed table carries this instance with void retain/release routing.
    ResetLog();
    WKUWPPlatformServices table { };
    EXPECT_TRUE(service->buildTable(&table) == S_OK, "table built");
    EXPECT_TRUE(table.header.structureSize == sizeof(table), "table header size");
    EXPECT_TRUE(table.header.structureVersion == WKUWP_STRUCTURE_VERSION, "table header version");
    EXPECT_TRUE(table.context == service.get(), "table context is the service");
    EXPECT_TRUE(table.retainContext && table.releaseContext && table.requestClipboard && table.cancelClipboard, "table thunks present");
    EXPECT_TRUE(table.postWake == nullptr, "postWake stays null until 7.5");
    EXPECT_TRUE(service->buildTable(nullptr) == E_INVALIDARG, "null table rejected");
    table.retainContext(table.context);
    EXPECT_TRUE(service->contextRefCount() == 2, "retain thunk counted");
    table.releaseContext(table.context);
    EXPECT_TRUE(service->contextRefCount() == 1, "release thunk counted");
    table.releaseContext(nullptr);
    table.retainContext(nullptr);
    EXPECT_TRUE(service->contextRefCount() == 1, "null retain/release ignored");
    EXPECT_TRUE(table.requestClipboard(table.context, 3, 30, WKUWPClipboardWrite, &g_empty, &RecordCompletion, &token) == WKUWPResultOK, "table write accepted");
    EXPECT_TRUE(g_log.count == 1 && g_log.lastId == 30, "table write completes");
    EXPECT_TRUE(table.cancelClipboard(table.context, 404) == static_cast<WKUWPResult>(E_INVALIDARG), "table unknown cancel rejected");
    EXPECT_TRUE(table.requestClipboard(nullptr, 3, 31, WKUWPClipboardWrite, &g_empty, &RecordCompletion, &token) == WKUWPResultInvalidArgument, "null table context request rejected");
    EXPECT_TRUE(table.cancelClipboard(nullptr, 31) == WKUWPResultInvalidArgument, "null table context cancel rejected");

    if (g_failures) {
        printf("FAIL: %d of %d clipboard service checks failed\n", g_failures, g_checks);
        return 1;
    }
    printf("PASS: %d clipboard service checks\n", g_checks);
    return 0;
}
