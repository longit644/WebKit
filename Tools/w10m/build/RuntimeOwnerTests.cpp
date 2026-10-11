// Verifies the host-side runtime owner: cleanup tickets with acknowledged
// cancellation, failed-enqueue accounting, quiescence, dispatcher loss, hook
// revocation, engine pinning and close/recreate counts.
//
// The harness links RuntimeOwnerW10M.cpp plus the clipboard stack directly and
// reaches the test-double engine provider only through LoadLibrary, exactly as
// the owner does. Scripted dispatcher/transport doubles stand in for WinRT, so
// every ticket, hook and shutdown rule below runs on this host. deletes via a
// pending transport call never happen: held completions are dropped without
// invoking once their owner is gone, which is the transport contract.

#include "RuntimeOwnerW10M.h"

#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace WebKit;

// Completed clipboard record shared by every test below.
struct OwnerLog {
    // Guards the counters against worker-thread transport completions.
    std::mutex lock;
    // Number of clipboard completion invocations observed so far.
    unsigned count { 0 };
    // Operation ID carried by the most recent completion.
    uint64_t lastId { 0 };
    // Frozen kind carried by the most recent completion.
    uint32_t lastKind { 0 };
    // Cut marker carried by the most recent completion.
    bool lastCut { false };
    // Result carried by the most recent completion.
    WKUWPResult lastResult { WKUWPResultOK };
    // Text bytes copied out of the borrowed span during the call.
    std::string lastText;
    // Context carried by the most recent completion.
    void* lastContext { nullptr };
    // Whether the borrowed span was readable for its full length during the call.
    bool spanReadable { false };
};

static OwnerLog g_log;

// Record one clipboard completion and prove the borrowed span stays readable.
static void __stdcall RecordOwner(void* context, uint64_t operationId, uint32_t kind, bool cut,
    WKUWPResult result, const WKUWPString* text)
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
    g_log.lastResult = WKUWPResultOK;
    g_log.lastText.clear();
    g_log.lastContext = nullptr;
    g_log.spanReadable = false;
}

// Hook revocation record; every revocation runs on the owner thread.
static unsigned g_hookCount = 0;
static uint64_t g_hookLastId = 0;
static WKUWPResult g_hookLastResult = WKUWPResultOK;
static void* g_hookLastContext = nullptr;

// Record one hook revocation.
static void __stdcall RecordHook(void* context, uint64_t hookId, WKUWPResult result)
{
    ++g_hookCount;
    g_hookLastId = hookId;
    g_hookLastResult = result;
    g_hookLastContext = context;
}

// Forget earlier revocations before the next case.
static void ResetHooks()
{
    g_hookCount = 0;
    g_hookLastId = 0;
    g_hookLastResult = WKUWPResultOK;
    g_hookLastContext = nullptr;
}

// Engine-side table completion count for owner-table interop cases.
static unsigned g_engineCompletions = 0;

// Record one engine-side table completion without retaining anything.
static void __stdcall RecordEngine(void*, uint64_t, WKUWPResult, const WKUWPString*)
{
    ++g_engineCompletions;
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

    // Report owner affinity; only the constructing thread owns the runtime.
    bool hasThreadAccess() override { return true; }

    // Deliver, capture or refuse one posted task, counting every attempt.
    bool post(std::function<void()>&& task) override
    {
        ++m_posts;
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
    // Total post attempts observed.
    unsigned m_posts { 0 };
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

// Borrowed span over a caller-owned buffer for copy/cut requests.
static WKUWPString MakeSpan(const std::string& bytes)
{
    WKUWPString span { reinterpret_cast<const uint8_t*>(bytes.data()), static_cast<uint32_t>(bytes.size()) };
    return span;
}

// Port pair behind one owner generation.
struct OwnerPorts {
    // Dispatcher observed through the service stack.
    FakeDispatcherUWP* dispatcher { nullptr };
    // Transport observed through the service stack.
    FakeTransportUWP* transport { nullptr };
};

// Build one owner with scripted doubles owned down the whole stack.
static std::unique_ptr<RuntimeOwnerUWP> MakeOwner(OwnerPorts& ports)
{
    auto ownedDispatcher = std::make_unique<FakeDispatcherUWP>();
    auto ownedTransport = std::make_unique<FakeTransportUWP>();
    ports.dispatcher = ownedDispatcher.get();
    ports.transport = ownedTransport.get();
    return std::make_unique<RuntimeOwnerUWP>(9, std::move(ownedDispatcher), std::move(ownedTransport));
}

// Fresh ports for one clipboard recreation.
static void FreshPorts(std::unique_ptr<IServiceDispatcherUWP>& dispatcher,
    std::unique_ptr<IClipboardTransportUWP>& transport, OwnerPorts& ports)
{
    auto ownedDispatcher = std::make_unique<FakeDispatcherUWP>();
    auto ownedTransport = std::make_unique<FakeTransportUWP>();
    ports.dispatcher = ownedDispatcher.get();
    ports.transport = ownedTransport.get();
    dispatcher = std::move(ownedDispatcher);
    transport = std::move(ownedTransport);
}

// Provider shutdown-result setter behind the legacy-surface gate.
typedef void(__stdcall* SetProviderShutdownResult)(WKUWPResult);

// Convert a narrow module path for the engine bind and pin observations.
static std::wstring WidePath(const char* narrow)
{
    int count = MultiByteToWideChar(CP_UTF8, 0, narrow, -1, nullptr, 0);
    std::wstring wide(count > 0 ? static_cast<size_t>(count) : 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, narrow, -1, &wide[0], count);
    return wide;
}

// Runs every runtime-owner case on this host; the provider image arrives on
// the command line and is reached only through the loader, exactly as the
// owner reaches it. WinRT stays scripted out, so no apartment is required.
int main(int argc, char** argv)
{
    if (argc < 2) {
        printf("usage: RuntimeOwnerTests <provider-module>\n");
        return 2;
    }
    std::wstring providerPath = WidePath(argv[1]);

    OwnerPorts ports;
    auto owner = MakeOwner(ports);
    EXPECT_TRUE(owner->checkThread() == S_OK, "owner thread accepted");
    EXPECT_TRUE(owner->pendingCount() == 0, "no pending operations initially");
    EXPECT_TRUE(owner->openTickets() == 0, "no open tickets initially");
    EXPECT_TRUE(owner->hookCount() == 0, "no hooks initially");
    uint64_t generation = owner->generation();
    EXPECT_TRUE(generation == 0, "first clipboard generation is zero");
    EXPECT_TRUE(!owner->isShuttingDown(), "shutdown unlached initially");
    EXPECT_TRUE(!owner->dispatcherLost(), "dispatcher healthy initially");
    EXPECT_TRUE(owner->quiescent(), "quiescent initially");
    EXPECT_TRUE(owner->completedTotal() == 0 && owner->canceledTotal() == 0, "no acknowledgments initially");
    EXPECT_TRUE(owner->droppedTotal() == 0, "no drops initially");
    EXPECT_TRUE(owner->hooksRevokedTotal() == 0, "no revocations initially");
    EXPECT_TRUE(owner->tableRefCount() == 0, "no table retains initially");
    EXPECT_TRUE(owner->engineShutdownResult() == WKUWPResultOK, "no engine shutdown forwarded initially");
    EXPECT_TRUE(!owner->isEngineBound(), "engine unbound initially");

    // Each accepted operation opens a ticket that its completion acknowledges.
    ResetLog();
    std::string copied = "owner copy";
    WKUWPString copySpan = MakeSpan(copied);
    int token = 5;
    EXPECT_TRUE(owner->copy(&copySpan, &RecordOwner, &token) == S_OK, "owner copy accepted");
    EXPECT_TRUE(g_log.count == 1, "owner copy completes exactly once");
    EXPECT_TRUE(g_log.lastResult == WKUWPResultOK && g_log.lastKind == WKUWPClipboardWrite, "owner copy completes OK as write");
    EXPECT_TRUE(!g_log.lastCut && g_log.lastContext == &token, "owner copy carries context without cut");
    EXPECT_TRUE(ports.transport->m_written == copied, "transport receives owner copy bytes");
    EXPECT_TRUE(owner->openTickets() == 0, "copy ticket acknowledged");
    EXPECT_TRUE(owner->completedTotal() == 1, "copy counted completed");
    EXPECT_TRUE(owner->quiescent(), "quiescent after copy");

    // Cut and paste ride the same ticket machinery with their own markers.
    ResetLog();
    std::string cutBytes = "owner cut";
    WKUWPString cutSpan = MakeSpan(cutBytes);
    EXPECT_TRUE(owner->cut(&cutSpan, &RecordOwner, &token) == S_OK, "owner cut accepted");
    EXPECT_TRUE(g_log.count == 1 && g_log.lastCut, "owner cut completes marked cut");
    ports.transport->m_readResult = S_OK;
    ports.transport->m_readBytes = "owner paste";
    EXPECT_TRUE(owner->paste(&RecordOwner, &token) == S_OK, "owner paste accepted");
    EXPECT_TRUE(g_log.lastText == ports.transport->m_readBytes, "owner paste delivers exact bytes");
    EXPECT_TRUE(owner->completedTotal() == 3, "three normal acknowledgments counted");
    EXPECT_TRUE(owner->openTickets() == 0, "no ticket left open");

    // Failed writes acknowledge through the same ticket with origin preserved.
    ResetLog();
    ports.transport->m_writeResult = E_ACCESSDENIED;
    EXPECT_TRUE(owner->copy(&copySpan, &RecordOwner, &token) == E_ACCESSDENIED, "failed owner copy returns origin");
    EXPECT_TRUE(g_log.count == 1, "failed owner copy still acknowledges once");
    EXPECT_TRUE(owner->completedTotal() == 4, "failed acknowledgment counted completed");
    ports.transport->m_writeResult = S_OK;

    // Malformed operations open no ticket at all.
    ResetLog();
    EXPECT_TRUE(owner->copy(nullptr, &RecordOwner, &token) == E_INVALIDARG, "null owner copy rejected");
    EXPECT_TRUE(owner->copy(&copySpan, nullptr, &token) == E_INVALIDARG, "null owner completion rejected");
    EXPECT_TRUE(owner->paste(nullptr, &token) == E_INVALIDARG, "null owner paste rejected");
    EXPECT_TRUE(g_log.count == 0 && owner->openTickets() == 0, "rejections open nothing");

    // The engine-facing table carries owner context with a working postWake.
    ResetLog();
    WKUWPPlatformServices table { };
    EXPECT_TRUE(owner->buildTable(&table) == S_OK, "owner table built");
    EXPECT_TRUE(table.header.structureSize == sizeof(table), "owner table header size");
    EXPECT_TRUE(table.context == owner.get(), "owner table context is the owner");
    EXPECT_TRUE(table.retainContext && table.releaseContext && table.requestClipboard
        && table.cancelClipboard && table.postWake, "owner table entries present");
    EXPECT_TRUE(owner->buildTable(nullptr) == E_INVALIDARG, "null owner table rejected");
    table.retainContext(table.context);
    EXPECT_TRUE(owner->tableRefCount() == 1, "table retain counted");
    table.releaseContext(table.context);
    EXPECT_TRUE(owner->tableRefCount() == 0, "table release counted");
    EXPECT_TRUE(owner->postWake() == WKUWPResultOK, "healthy postWake succeeds");
    EXPECT_TRUE(ports.dispatcher->m_posts == 1, "postWake enqueues one wake");
    g_engineCompletions = 0;
    WKUWPString empty { nullptr, 0 };
    EXPECT_TRUE(table.requestClipboard(table.context, 9, 900, WKUWPClipboardWrite, &empty,
        &RecordEngine, &token) == WKUWPResultOK, "engine write through owner table accepted");
    EXPECT_TRUE(g_engineCompletions == 1, "engine write completes without a ticket");
    EXPECT_TRUE(owner->openTickets() == 0, "engine operations open no ticket");
    EXPECT_TRUE(table.requestClipboard(nullptr, 9, 901, WKUWPClipboardWrite, &empty,
        &RecordEngine, &token) == WKUWPResultInvalidArgument, "null table context rejected");

    // Hooks open tickets that revocation acknowledges exactly once.
    ResetHooks();
    uint64_t firstHook = 0, secondHook = 0;
    int hookToken = 6;
    EXPECT_TRUE(owner->registerHook(&RecordHook, &hookToken, &firstHook) == S_OK, "first hook registered");
    EXPECT_TRUE(owner->registerHook(&RecordHook, &hookToken, &secondHook) == S_OK, "second hook registered");
    EXPECT_TRUE(secondHook != firstHook, "hook IDs are never reused");
    EXPECT_TRUE(owner->hookCount() == 2, "two hooks open");
    EXPECT_TRUE(!owner->quiescent(), "open hooks break quiescence");
    EXPECT_TRUE(owner->revokeHook(firstHook) == S_OK, "first hook revoked");
    EXPECT_TRUE(g_hookCount == 1 && g_hookLastId == firstHook, "revocation acknowledges once with ID");
    EXPECT_TRUE(g_hookLastResult == WKUWPResultClosed && g_hookLastContext == &hookToken, "revocation carries Closed and context");
    EXPECT_TRUE(owner->hooksRevokedTotal() == 1, "revocation counted");
    EXPECT_TRUE(owner->revokeHook(777) == E_INVALIDARG, "unknown hook rejected");
    EXPECT_TRUE(owner->registerHook(nullptr, &hookToken, &secondHook) == E_INVALIDARG, "null hook rejected");
    uint64_t ignored = 0;
    EXPECT_TRUE(owner->registerHook(&RecordHook, &hookToken, nullptr) == E_INVALIDARG, "null hook ID rejected");
    static_cast<void>(ignored);
    EXPECT_TRUE(owner->revokeHook(secondHook) == S_OK, "second hook revoked");
    EXPECT_TRUE(g_hookCount == 2 && owner->hooksRevokedTotal() == 2, "both explicit revocations counted");
    EXPECT_TRUE(owner->hookCount() == 0, "no hook left open");
    EXPECT_TRUE(owner->quiescent(), "quiescent with nothing open");

    // Close cancels every pending ticket with exactly-once Closed delivery.
    ResetLog();
    ports.transport->m_manual = true;
    EXPECT_TRUE(owner->paste(&RecordOwner, &token) == S_OK, "close-pending paste accepted");
    EXPECT_TRUE(owner->paste(&RecordOwner, &token) == S_OK, "second close-pending paste accepted");
    EXPECT_TRUE(owner->pendingCount() == 2 && owner->openTickets() == 2, "two tickets open before close");
    EXPECT_TRUE(!owner->quiescent(), "pending tickets break quiescence");
    EXPECT_TRUE(owner->closeClipboard() == S_OK, "clipboard closed");
    EXPECT_TRUE(g_log.count == 2 && g_log.lastResult == WKUWPResultClosed, "close acknowledges every ticket Closed");
    EXPECT_TRUE(owner->canceledTotal() == 2, "cancellations counted");
    EXPECT_TRUE(owner->openTickets() == 0 && owner->pendingCount() == 0, "close drains everything");
    EXPECT_TRUE(owner->closeClipboard() == S_OK, "clipboard close is idempotent");
    ports.transport->m_pending.clear();
    ports.transport->m_manual = false;

    // Recreation advances the generation with totals carried and pending reset.
    OwnerPorts fresh;
    std::unique_ptr<IServiceDispatcherUWP> freshDispatcher;
    std::unique_ptr<IClipboardTransportUWP> freshTransport;
    FreshPorts(freshDispatcher, freshTransport, fresh);
    EXPECT_TRUE(owner->recreateClipboard(std::move(freshDispatcher), std::move(freshTransport)) == S_OK, "clipboard recreated");
    EXPECT_TRUE(owner->generation() == generation + 1, "generation advances on recreate");
    EXPECT_TRUE(owner->pendingCount() == 0 && owner->openTickets() == 0, "recreate resets pending");
    EXPECT_TRUE(owner->completedTotal() == 4 && owner->canceledTotal() == 2, "recreate carries totals");
    ResetLog();
    EXPECT_TRUE(owner->copy(&copySpan, &RecordOwner, &token) == S_OK, "post-recreate copy accepted");
    EXPECT_TRUE(g_log.count == 1 && owner->completedTotal() == 5, "post-recreate copy acknowledged");
    fresh.transport->m_manual = true;
    EXPECT_TRUE(owner->paste(&RecordOwner, &token) == S_OK, "recreate-pending paste accepted");
    OwnerPorts newer;
    std::unique_ptr<IServiceDispatcherUWP> newerDispatcher;
    std::unique_ptr<IClipboardTransportUWP> newerTransport;
    FreshPorts(newerDispatcher, newerTransport, newer);
    EXPECT_TRUE(owner->recreateClipboard(std::move(newerDispatcher), std::move(newerTransport)) == S_OK, "recreate with pending closes first");
    EXPECT_TRUE(g_log.count == 2 && g_log.lastResult == WKUWPResultClosed, "recreate acknowledges the pending ticket");
    EXPECT_TRUE(owner->generation() == generation + 2, "second recreation advances again");

    // Shutdown acknowledges the last ticket, revokes the last hook, loses the
    // dispatcher and stays quiescent; later work fails fail-closed.
    ResetLog();
    ResetHooks();
    newer.transport->m_manual = true;
    EXPECT_TRUE(owner->paste(&RecordOwner, &token) == S_OK, "shutdown-pending paste accepted");
    uint64_t shutdownHook = 0;
    EXPECT_TRUE(owner->registerHook(&RecordHook, &hookToken, &shutdownHook) == S_OK, "shutdown hook registered");
    EXPECT_TRUE(owner->beginShutdown() == S_OK, "shutdown accepted while unbound");
    EXPECT_TRUE(g_log.count == 1 && g_log.lastResult == WKUWPResultClosed, "shutdown acknowledges the ticket Closed");
    EXPECT_TRUE(g_hookCount == 1 && g_hookLastResult == WKUWPResultClosed, "shutdown revokes the hook Closed");
    EXPECT_TRUE(owner->isShuttingDown() && owner->dispatcherLost(), "shutdown latches both flags");
    EXPECT_TRUE(owner->quiescent(), "quiescent after shutdown");
    EXPECT_TRUE(owner->hooksRevokedTotal() == 3, "all three revocations counted");
    EXPECT_TRUE(owner->copy(&copySpan, &RecordOwner, &token) == RO_E_CLOSED, "post-shutdown copy rejected");
    EXPECT_TRUE(owner->registerHook(&RecordHook, &hookToken, &shutdownHook) == RO_E_CLOSED, "post-shutdown hook rejected");
    std::unique_ptr<IServiceDispatcherUWP> lateDispatcher;
    std::unique_ptr<IClipboardTransportUWP> lateTransport;
    OwnerPorts late;
    FreshPorts(lateDispatcher, lateTransport, late);
    EXPECT_TRUE(owner->recreateClipboard(std::move(lateDispatcher), std::move(lateTransport)) == RO_E_CLOSED, "post-shutdown recreate rejected");
    EXPECT_TRUE(owner->postWake() == WKUWPResultDispatcherUnavailable, "post-loss wake unavailable");
    EXPECT_TRUE(owner->buildTable(&table) == S_OK, "table still builds for engine cancels");
    EXPECT_TRUE(table.requestClipboard(table.context, 9, 902, WKUWPClipboardWrite, &empty,
        &RecordEngine, &token) == WKUWPResultShuttingDown, "post-shutdown engine request refused");
    EXPECT_TRUE(table.cancelClipboard(table.context, 902) == static_cast<WKUWPResult>(E_INVALIDARG), "engine cancel still passes through");
    EXPECT_TRUE(owner->beginShutdown() == S_OK, "shutdown is idempotent");
    EXPECT_TRUE(owner->pollShutdown() == WKUWPResultOK, "unbound shutdown poll succeeds");
    newer.transport->m_pending.clear();

    // Dispatcher loss drops marshaled completions loudly: counted, pending,
    // acknowledged only by synchronous close. Destruction with a lost
    // dispatcher then releases last without crashing or silent loss.
    {
        OwnerPorts lost;
        auto parted = MakeOwner(lost);
        lost.dispatcher->setMode(FakeDispatcherUWP::Mode::Refuse);
        lost.transport->m_manual = true;
        ResetLog();
        EXPECT_TRUE(parted->paste(&RecordOwner, &token) == S_OK, "loss-pending paste accepted");
        EXPECT_TRUE(parted->loseDispatcher() == S_OK, "dispatcher lost");
        EXPECT_TRUE(parted->loseDispatcher() == S_OK, "loss is idempotent");
        EXPECT_TRUE(parted->dispatcherLost(), "loss latched");
        EXPECT_TRUE(parted->postWake() == WKUWPResultDispatcherUnavailable, "lost wake unavailable");
        auto dropped = std::move(lost.transport->m_pending[0]);
        lost.transport->m_pending.clear();
        std::thread worker([taken = std::move(dropped)]() mutable { taken(S_OK, "unheard"); });
        worker.join();
        EXPECT_TRUE(g_log.count == 0, "refused completion never delivers");
        EXPECT_TRUE(parted->droppedTotal() == 1, "refused completion counted");
        EXPECT_TRUE(parted->pendingCount() == 1 && parted->openTickets() == 1, "dropped operation stays open");
        EXPECT_TRUE(parted->closeClipboard() == S_OK, "loss-pending close accepted");
        EXPECT_TRUE(g_log.count == 1 && g_log.lastResult == WKUWPResultClosed, "close acknowledges without dispatcher");
        EXPECT_TRUE(parted->quiescent(), "quiescent after loss close");
    }

    // Calls from any other thread are rejected before any ticket exists.
    {
        OwnerPorts foreign;
        auto outlier = MakeOwner(foreign);
        ResetLog();
        HRESULT threadCopy = S_OK, threadPaste = S_OK, threadClose = S_OK, threadHook = S_OK;
        HRESULT threadShutdown = S_OK, threadTable = S_OK;
        WKUWPResult threadBind = WKUWPResultOK;
        WKUWPPlatformServices foreignTable { };
        std::thread stranger([&] {
            threadCopy = outlier->copy(&copySpan, &RecordOwner, &token);
            threadPaste = outlier->paste(&RecordOwner, &token);
            threadClose = outlier->closeClipboard();
            threadHook = outlier->registerHook(&RecordHook, &token, &shutdownHook);
            threadShutdown = outlier->beginShutdown();
            threadTable = outlier->buildTable(&foreignTable);
            threadBind = outlier->bindEngine(L"nowhere.dll", 0, 0);
        });
        stranger.join();
        EXPECT_TRUE(threadCopy == RPC_E_WRONG_THREAD, "foreign copy rejected");
        EXPECT_TRUE(threadPaste == RPC_E_WRONG_THREAD, "foreign paste rejected");
        EXPECT_TRUE(threadClose == RPC_E_WRONG_THREAD, "foreign close rejected");
        EXPECT_TRUE(threadHook == RPC_E_WRONG_THREAD, "foreign hook rejected");
        EXPECT_TRUE(threadShutdown == RPC_E_WRONG_THREAD, "foreign shutdown rejected");
        EXPECT_TRUE(threadTable == RPC_E_WRONG_THREAD, "foreign table rejected");
        EXPECT_TRUE(threadBind == static_cast<WKUWPResult>(RPC_E_WRONG_THREAD), "foreign bind rejected");
        EXPECT_TRUE(g_log.count == 0 && outlier->openTickets() == 0, "foreign calls open nothing");
        EXPECT_TRUE(outlier->quiescent() && !outlier->isShuttingDown(), "foreign calls latch nothing");
    }

    // Engine pinning through the loader: a missing image fails unbound, the
    // test-double image binds and pins, shutdown acknowledgment never depends
    // on the engine answer, and the pin survives this owner.
    {
        OwnerPorts wired;
        auto engine = MakeOwner(wired);
        EXPECT_TRUE(engine->bindEngine(L"missing-engine-75.dll", 0, 0) != WKUWPResultOK, "missing engine rejected");
        EXPECT_TRUE(!engine->isEngineBound(), "failed bind leaves engine unbound");
        EXPECT_TRUE(engine->bindEngine(providerPath.c_str(), 0, 0) == WKUWPResultOK, "provider engine bound");
        EXPECT_TRUE(engine->isEngineBound(), "bound flag set");
        HMODULE firstHandle = nullptr;
        EXPECT_TRUE(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, providerPath.c_str(), &firstHandle) && firstHandle, "bound image observable");
        EXPECT_TRUE(engine->bindEngine(providerPath.c_str(), 0, 0) == WKUWPResultOK, "second bind idempotent");
        HMODULE secondHandle = nullptr;
        EXPECT_TRUE(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, providerPath.c_str(), &secondHandle) && secondHandle == firstHandle, "rebind pins the same image");
        HMODULE setterLibrary = LoadLibraryW(providerPath.c_str());
        EXPECT_TRUE(!!setterLibrary, "provider image reloadable for setters");
        auto setShutdownResult = reinterpret_cast<SetProviderShutdownResult>(
            GetProcAddress(setterLibrary, "WKUWPTestProviderSetShutdownResult"));
        EXPECT_TRUE(!!setShutdownResult, "shutdown setter resolved");
        ResetLog();
        wired.transport->m_manual = true;
        EXPECT_TRUE(engine->paste(&RecordOwner, &token) == S_OK, "engine-shutdown paste accepted");
        setShutdownResult(static_cast<WKUWPResult>(E_FAIL));
        HRESULT shutdownResult = engine->beginShutdown();
        setShutdownResult(WKUWPResultOK);
        EXPECT_TRUE(shutdownResult == E_FAIL, "engine shutdown failure preserved");
        EXPECT_TRUE(g_log.count == 1 && g_log.lastResult == WKUWPResultClosed, "ticket acknowledged despite engine failure");
        EXPECT_TRUE(engine->quiescent(), "quiescent despite engine failure");
        EXPECT_TRUE(engine->engineShutdownResult() == static_cast<WKUWPResult>(E_FAIL), "engine result recorded");
        EXPECT_TRUE(engine->pollShutdown() == WKUWPResultOK, "shutdown poll passes through");
        EXPECT_TRUE(engine->bindEngine(providerPath.c_str(), 0, 0) == WKUWPResultShuttingDown, "bind after shutdown refused");
        wired.transport->m_pending.clear();
        // Drop the setter reference: the only remaining load is the owner's
        // pin, so survival past this scope proves process pinning.
        FreeLibrary(setterLibrary);
    }
    HMODULE pinned = nullptr;
    EXPECT_TRUE(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, providerPath.c_str(), &pinned) && pinned, "engine pin survives its owner");

    if (g_failures) {
        printf("FAIL: %d of %d runtime owner checks failed\n", g_failures, g_checks);
        return 1;
    }
    printf("PASS: %d runtime owner checks\n", g_checks);
    return 0;
}
