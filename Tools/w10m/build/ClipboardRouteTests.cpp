// Verifies host-side clipboard routing through retained owner-thread tokens.
//
// The harness links ClipboardRouteW10M.cpp and ClipboardServiceW10M.cpp directly
// and drives them through scripted dispatcher/transport doubles, so copy, cut,
// paste, Close and cross-thread behavior all run on this host. deletes via a
// pending transport call never happen: held transport completions are dropped
// without invoking once their owner is gone, which is the transport contract.
// WinRT behavior belongs to the CX transport and later device tasks.

#include "ClipboardRouteW10M.h"

#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace WebKit;

// Completed route-completion record shared by every test below.
struct RouteLog {
    // Guards the counters against worker-thread transport completions.
    std::mutex lock;
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
    // Context carried by the most recent completion.
    void* lastContext { nullptr };
    // Whether the borrowed span was readable for its full length during the call.
    bool spanReadable { false };
};

static RouteLog g_log;

// Record one route completion and prove the borrowed span stays readable.
static void __stdcall RecordRoute(void* context, uint64_t operationId, uint32_t kind, bool cut,
    WKW10MResult result, const WKW10MString* text)
{
    std::lock_guard<std::mutex> guard(g_log.lock);
    ++g_log.count;
    g_log.lastId = operationId;
    g_log.lastKind = kind;
    g_log.lastCut = cut;
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
    g_log.lastKind = 0;
    g_log.lastCut = false;
    g_log.lastResult = WKW10MResultOK;
    g_log.lastText.clear();
    g_log.lastContext = nullptr;
    g_log.spanReadable = false;
}

// Engine-style service completion counter for table-interop cases.
static unsigned g_serviceCompletions = 0;

// Record one engine-side service completion without retaining anything.
static void __stdcall RecordService(void*, uint64_t, WKW10MResult, const WKW10MString*)
{
    ++g_serviceCompletions;
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
class FakeDispatcherW10M final : public IServiceDispatcherW10M {
public:
    // Delivery behavior selected per test case.
    enum class Mode { Inline, Capture, Refuse };

    // Report owner affinity; only the constructing thread owns the route.
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
class FakeTransportW10M final : public IClipboardTransportW10M {
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

// Borrowed span over a caller-owned buffer for copy/cut requests.
static WKW10MString MakeSpan(const std::string& bytes)
{
    WKW10MString span { reinterpret_cast<const uint8_t*>(bytes.data()), static_cast<uint32_t>(bytes.size()) };
    return span;
}

// Build one route with scripted doubles owned down the whole stack.
static std::unique_ptr<ClipboardRouteW10M> MakeRoute(FakeDispatcherW10M*& dispatcher, FakeTransportW10M*& transport)
{
    auto ownedDispatcher = std::make_unique<FakeDispatcherW10M>();
    auto ownedTransport = std::make_unique<FakeTransportW10M>();
    dispatcher = ownedDispatcher.get();
    transport = ownedTransport.get();
    auto service = std::make_unique<ClipboardServiceW10M>(std::move(ownedDispatcher), std::move(ownedTransport));
    return std::make_unique<ClipboardRouteW10M>(7, std::move(service));
}

// Runs every clipboard routing case on this host; the WinRT transport is
// scripted out, so no WinRT apartment or device is required.
int main()
{
    FakeDispatcherW10M* dispatcher = nullptr;
    FakeTransportW10M* transport = nullptr;
    auto route = MakeRoute(dispatcher, transport);
    EXPECT_TRUE(route->checkThread() == S_OK, "owner thread accepted");
    EXPECT_TRUE(route->pendingCount() == 0, "no pending operations initially");
    EXPECT_TRUE(!route->closed(), "route starts open");

    // Copy publishes exact bytes and completes OK once with an empty span.
    ResetLog();
    std::string copied = "copy bytes \xe2\x9c\x93";
    WKW10MString copySpan = MakeSpan(copied);
    int token = 3;
    EXPECT_TRUE(route->copy(&copySpan, &RecordRoute, &token) == S_OK, "copy accepted");
    EXPECT_TRUE(g_log.count == 1, "copy completes exactly once");
    EXPECT_TRUE(g_log.lastResult == WKW10MResultOK, "copy completes OK");
    EXPECT_TRUE(g_log.lastKind == WKW10MClipboardWrite, "copy reports write kind");
    EXPECT_TRUE(!g_log.lastCut, "copy reports cut false");
    EXPECT_TRUE(g_log.lastContext == &token, "copy completion carries context");
    EXPECT_TRUE(g_log.spanReadable && g_log.lastText.empty(), "copy span readable and empty");
    EXPECT_TRUE(transport->m_written == copied, "transport receives exact copy bytes");
    uint64_t copyId = g_log.lastId;
    EXPECT_TRUE(route->pendingCount() == 0, "copy leaves nothing pending");

    // Cut publishes exact bytes and marks the completion cut.
    ResetLog();
    std::string cutBytes = "cut bytes";
    WKW10MString cutSpan = MakeSpan(cutBytes);
    EXPECT_TRUE(route->cut(&cutSpan, &RecordRoute, &token) == S_OK, "cut accepted");
    EXPECT_TRUE(g_log.count == 1, "cut completes exactly once");
    EXPECT_TRUE(g_log.lastResult == WKW10MResultOK, "cut completes OK");
    EXPECT_TRUE(g_log.lastKind == WKW10MClipboardWrite, "cut reports write kind");
    EXPECT_TRUE(g_log.lastCut, "cut reports cut true");
    EXPECT_TRUE(transport->m_written == cutBytes, "transport receives exact cut bytes");
    EXPECT_TRUE(g_log.lastId != copyId, "operation IDs are never reused");
    EXPECT_TRUE(route->pendingCount() == 0, "cut leaves nothing pending");

    // Paste delivers the transport bytes once with read kind.
    ResetLog();
    transport->m_readResult = S_OK;
    transport->m_readBytes = "pasted \xe2\x9c\x93";
    EXPECT_TRUE(route->paste(&RecordRoute, &token) == S_OK, "paste accepted");
    EXPECT_TRUE(g_log.count == 1, "paste completes exactly once");
    EXPECT_TRUE(g_log.lastResult == WKW10MResultOK, "paste completes OK");
    EXPECT_TRUE(g_log.lastKind == WKW10MClipboardRead, "paste reports read kind");
    EXPECT_TRUE(!g_log.lastCut, "paste reports cut false");
    EXPECT_TRUE(g_log.lastText == transport->m_readBytes, "paste delivers exact bytes");
    EXPECT_TRUE(g_log.spanReadable, "paste span readable during call");
    EXPECT_TRUE(route->pendingCount() == 0, "paste leaves nothing pending");

    // A failed write completes once with the originating failure preserved.
    ResetLog();
    transport->m_writeResult = E_ACCESSDENIED;
    EXPECT_TRUE(route->copy(&copySpan, &RecordRoute, &token) == E_ACCESSDENIED, "failed copy returns origin");
    EXPECT_TRUE(g_log.count == 1, "failed copy completes exactly once");
    EXPECT_TRUE(g_log.lastResult == static_cast<WKW10MResult>(E_ACCESSDENIED), "failed copy preserves origin");
    EXPECT_TRUE(route->pendingCount() == 0, "failed copy leaves nothing pending");
    transport->m_writeResult = S_OK;

    // Malformed requests are rejected before any token exists.
    ResetLog();
    EXPECT_TRUE(route->copy(nullptr, &RecordRoute, &token) == E_INVALIDARG, "null copy text rejected");
    WKW10MString dangling { nullptr, 5 };
    EXPECT_TRUE(route->copy(&dangling, &RecordRoute, &token) == E_INVALIDARG, "dangling copy span rejected");
    EXPECT_TRUE(route->copy(&copySpan, nullptr, &token) == E_INVALIDARG, "null copy completion rejected");
    EXPECT_TRUE(route->cut(&copySpan, nullptr, &token) == E_INVALIDARG, "null cut completion rejected");
    EXPECT_TRUE(route->paste(nullptr, &token) == E_INVALIDARG, "null paste completion rejected");
    EXPECT_TRUE(g_log.count == 0, "rejected requests never complete");
    EXPECT_TRUE(route->pendingCount() == 0, "rejected requests leave nothing pending");

    // A missing service fails every entry point instead of faulting.
    {
        auto orphan = std::make_unique<ClipboardRouteW10M>(7, nullptr);
        EXPECT_TRUE(orphan->copy(&copySpan, &RecordRoute, &token) == E_INVALIDARG, "orphan copy rejected");
        EXPECT_TRUE(orphan->paste(&RecordRoute, &token) == E_INVALIDARG, "orphan paste rejected");
        EXPECT_TRUE(orphan->close() == E_INVALIDARG, "orphan close rejected");
        EXPECT_TRUE(orphan->buildTable(nullptr) == E_INVALIDARG, "orphan table rejected");
    }

    // Close cancels every pending operation with exactly-once Closed delivery.
    ResetLog();
    transport->m_manual = true;
    EXPECT_TRUE(route->paste(&RecordRoute, &token) == S_OK, "first close-pending paste accepted");
    EXPECT_TRUE(route->paste(&RecordRoute, &token) == S_OK, "second close-pending paste accepted");
    EXPECT_TRUE(route->pendingCount() == 2, "two operations pending before close");
    EXPECT_TRUE(route->close() == S_OK, "close accepted");
    EXPECT_TRUE(g_log.count == 2, "close completes every pending operation once");
    EXPECT_TRUE(g_log.lastResult == WKW10MResultClosed, "close completes Closed");
    EXPECT_TRUE(route->pendingCount() == 0, "close leaves nothing pending");
    EXPECT_TRUE(route->closed(), "close latches");
    EXPECT_TRUE(route->close() == S_OK, "close is idempotent");
    EXPECT_TRUE(g_log.count == 2, "idempotent close completes nothing new");
    // Held transport completions are dropped without invoking: their operations
    // are already complete, and the transport contract forbids late calls.
    transport->m_pending.clear();
    EXPECT_TRUE(g_log.count == 2, "transport completions after close stay uninvoked");
    transport->m_manual = false;

    // Operations after close fail Closed without completing.
    ResetLog();
    EXPECT_TRUE(route->copy(&copySpan, &RecordRoute, &token) == RO_E_CLOSED, "copy after close rejected");
    EXPECT_TRUE(route->cut(&cutSpan, &RecordRoute, &token) == RO_E_CLOSED, "cut after close rejected");
    EXPECT_TRUE(route->paste(&RecordRoute, &token) == RO_E_CLOSED, "paste after close rejected");
    EXPECT_TRUE(g_log.count == 0, "post-close requests never complete");

    // A late transport completion after close is ignored once invoked.
    {
        FakeDispatcherW10M* lateDispatcher = nullptr;
        FakeTransportW10M* lateTransport = nullptr;
        auto late = MakeRoute(lateDispatcher, lateTransport);
        lateTransport->m_manual = true;
        ResetLog();
        EXPECT_TRUE(late->paste(&RecordRoute, &token) == S_OK, "late-ignored paste accepted");
        EXPECT_TRUE(late->close() == S_OK, "late-ignored route closed");
        EXPECT_TRUE(g_log.count == 1 && g_log.lastResult == WKW10MResultClosed, "close wins the race");
        // Move the held completion out before invoking: once it runs, the owner
        // may be gone, and the transport contract forbids touching it after.
        auto lateDone = std::move(lateTransport->m_pending[0]);
        lateTransport->m_pending.clear();
        lateDone(S_OK, "too late");
        EXPECT_TRUE(g_log.count == 1, "late transport completion ignored");
        EXPECT_TRUE(late->pendingCount() == 0, "ignored completion leaves nothing pending");
    }

    // Destruction closes first: pending work completes Closed during the destructor.
    {
        FakeDispatcherW10M* dyingDispatcher = nullptr;
        FakeTransportW10M* dyingTransport = nullptr;
        ResetLog();
        {
            auto dying = MakeRoute(dyingDispatcher, dyingTransport);
            dyingTransport->m_manual = true;
            EXPECT_TRUE(dying->paste(&RecordRoute, &token) == S_OK, "destruction-pending paste accepted");
        }
        EXPECT_TRUE(g_log.count == 1 && g_log.lastResult == WKW10MResultClosed, "destruction completes Closed");
    }

    // A completion that reenters close() meets the latch and stays exact.
    {
        FakeDispatcherW10M* reentrantDispatcher = nullptr;
        FakeTransportW10M* reentrantTransport = nullptr;
        auto reentrant = MakeRoute(reentrantDispatcher, reentrantTransport);
        reentrantTransport->m_manual = true;
        ResetLog();
        // Reenters close() from inside a close-driven completion.
        struct Reenter {
            // Meet the close latch without delivering anything new.
            static void __stdcall run(void* context, uint64_t, uint32_t, bool, WKW10MResult result, const WKW10MString*) {
                auto* self = static_cast<ClipboardRouteW10M*>(context);
                if (result == WKW10MResultClosed)
                    EXPECT_TRUE(self->close() == S_OK, "reentrant close accepted");
            }
        };
        EXPECT_TRUE(reentrant->paste(&Reenter::run, reentrant.get()) == S_OK, "reentrant paste accepted");
        EXPECT_TRUE(reentrant->close() == S_OK, "outer close accepted");
        EXPECT_TRUE(g_log.count == 0, "reentrant delivery bypasses the shared log");
        EXPECT_TRUE(reentrant->pendingCount() == 0, "reentrant close drains");
    }

    // Destroying the route inside an ordinary completion is safe: the token is
    // already erased, so destruction closes nothing pending and frees cleanly.
    {
        FakeDispatcherW10M* selfDispatcher = nullptr;
        FakeTransportW10M* selfTransport = nullptr;
        auto self = MakeRoute(selfDispatcher, selfTransport);
        selfTransport->m_manual = true;
        ResetLog();
        // Destroys the route from inside an ordinary completion.
        struct SelfDestruct {
            // Release the holder; the erased token keeps this delivery safe.
            static void __stdcall run(void* context, uint64_t, uint32_t, bool, WKW10MResult, const WKW10MString*) {
                auto* holder = static_cast<std::unique_ptr<ClipboardRouteW10M>*>(context);
                holder->reset();
            }
        };
        EXPECT_TRUE(self->paste(&SelfDestruct::run, &self) == S_OK, "self-destroying paste accepted");
        // Move the held completion out first: invoking it destroys the route,
        // which destroys the transport holding the vector it came from.
        auto finalDone = std::move(selfTransport->m_pending[0]);
        selfTransport = nullptr;
        finalDone(S_OK, "final");
        EXPECT_TRUE(!self, "route destroyed inside its own completion");
    }

    // Calls from any other thread are rejected before any token exists.
    {
        FakeDispatcherW10M* foreignDispatcher = nullptr;
        FakeTransportW10M* foreignTransport = nullptr;
        auto foreign = MakeRoute(foreignDispatcher, foreignTransport);
        ResetLog();
        HRESULT threadCopy = S_OK, threadPaste = S_OK, threadClose = S_OK, threadTable = S_OK;
        WKW10MPlatformServices foreignTable { };
        std::thread stranger([&] {
            threadCopy = foreign->copy(&copySpan, &RecordRoute, &token);
            threadPaste = foreign->paste(&RecordRoute, &token);
            threadClose = foreign->close();
            threadTable = foreign->buildTable(&foreignTable);
        });
        stranger.join();
        EXPECT_TRUE(threadCopy == RPC_E_WRONG_THREAD, "foreign copy rejected");
        EXPECT_TRUE(threadPaste == RPC_E_WRONG_THREAD, "foreign paste rejected");
        EXPECT_TRUE(threadClose == RPC_E_WRONG_THREAD, "foreign close rejected");
        EXPECT_TRUE(threadTable == RPC_E_WRONG_THREAD, "foreign table rejected");
        EXPECT_TRUE(g_log.count == 0, "foreign calls never complete");
        EXPECT_TRUE(foreign->pendingCount() == 0, "foreign calls register nothing");
        EXPECT_TRUE(!foreign->closed(), "foreign close latches nothing");
    }

    // The borrowed table serves engine operations alongside route operations.
    {
        FakeDispatcherW10M* sharedDispatcher = nullptr;
        FakeTransportW10M* sharedTransport = nullptr;
        auto shared = MakeRoute(sharedDispatcher, sharedTransport);
        sharedTransport->m_manual = true;
        ResetLog();
        g_serviceCompletions = 0;
        WKW10MPlatformServices table { };
        EXPECT_TRUE(shared->buildTable(&table) == S_OK, "route table built");
        EXPECT_TRUE(table.header.structureSize == sizeof(table), "route table header size");
        EXPECT_TRUE(table.context && table.requestClipboard && table.cancelClipboard, "route table entries present");
        EXPECT_TRUE(table.postWake == nullptr, "route table postWake stays null");
        EXPECT_TRUE(shared->paste(&RecordRoute, &token) == S_OK, "shared route paste accepted");
        WKW10MString empty { nullptr, 0 };
        EXPECT_TRUE(table.requestClipboard(table.context, 7, 500, WKW10MClipboardWrite, &empty,
            &RecordService, &token) == WKW10MResultOK, "engine write through route table accepted");
        EXPECT_TRUE(g_serviceCompletions == 1, "engine write completes");
        EXPECT_TRUE(shared->pendingCount() == 1, "route operation unaffected by engine write");
        EXPECT_TRUE(shared->close() == S_OK, "shared route closed");
        EXPECT_TRUE(g_log.count == 1 && g_log.lastResult == WKW10MResultClosed, "route operation closes independently");
        sharedTransport->m_pending.clear();
    }

    if (g_failures) {
        printf("FAIL: %d of %d clipboard route checks failed\n", g_failures, g_checks);
        return 1;
    }
    printf("PASS: %d clipboard route checks\n", g_checks);
    return 0;
}
