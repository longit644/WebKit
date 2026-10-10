// Native scheduling laws against controlled frozen process-service slots.
#include "rendering/FrameSchedulerUWP.h"

#include <cstdio>
#include <thread>

using namespace WebKit;

namespace {

// Fail-closed totals for the independent scheduling cases.
unsigned checks = 0, failures = 0;
// Provider invocation and controlled delayed-work counters.
unsigned services = 0, suspensionCalls = 0, completedResources = 0;
// Provider-side suspension and pending delayed resource completion state.
bool suspended = false, resourceReady = false;
// Controlled originating result/reentrant request.
WKUWPResult serviceResult = WKUWPResultOK, suspendResult = WKUWPResultOK;
// Optional native pump reentry used only by this provider test.
OwnerThreadServicePumpUWP* reenter = nullptr;
// Optional per-view Close inside service before a frame could be admitted.
FrameSchedulerUWP* closeDuringService = nullptr;

// Preserve an earlier failure independently of subsequent passing checks.
void check(bool value, const char* name)
{
    ++checks;
    if (!value) ++failures;
    std::printf("%s: %s\n", value ? "PASS" : "FAIL", name);
}

// Complete ready resource work without requesting or counting any presentation.
WKUWPResult __stdcall service()
{
    ++services;
    if (resourceReady && !suspended) { resourceReady = false; ++completedResources; }
    if (reenter) reenter->serviceNow();
    if (closeDuringService) closeDuringService->close();
    return serviceResult;
}

// Distinct process-service identity must not replace a live binding.
WKUWPResult __stdcall foreignService() { return WKUWPResultOK; }

// Explicit provider suspension is independent of every visual flag.
WKUWPResult __stdcall setSuspended(uint32_t value)
{
    ++suspensionCalls;
    if (suspendResult == WKUWPResultOK) suspended = !!value;
    return suspendResult;
}

// Construct the exact V1 table prefix with only the relevant neutral process slots.
WKUWPEngineAPI table()
{
    WKUWPEngineAPI api { };
    api.header = { sizeof(api), WKUWP_STRUCTURE_VERSION };
    api.abiMajor = WKUWP_ABI_MAJOR_VERSION;
    api.service = &service;
    api.setSuspended = &setSuspended;
    return api;
}

// Check live leases, exact epoch deduplication, visibility and process suspension.
void run()
{
    auto api = table();
    auto pump = std::make_shared<OwnerThreadServicePumpUWP>();
    check(pump->serviceNow() == HRESULT_FROM_WIN32(ERROR_NOT_READY), "unbound pump cannot service or invent readiness");
    auto shortAPI = api; shortAPI.header.structureSize = 8;
    check(pump->bind(&shortAPI) == HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH), "short table rejects before any service slot use");
    check(pump->bind(&api) == S_OK && !services, "binding copies negotiated process slots without initialization side effects");
    check(pump->bind(&api) == S_OK, "matching process binding is idempotent");
    auto foreign = api; foreign.service = &foreignService;
    check(pump->bind(&foreign) == HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED), "another engine cannot replace a copied process service identity");
    FrameSchedulerUWP a, b;
    check(a.attach(pump) == S_OK && b.attach(pump) == S_OK && pump->clientCount() == 2, "two independent view leases share one native process pump");
    check(a.beginFrame(0) == S_FALSE && b.beginFrame(0) == S_FALSE && !services, "visually paused controls perform no rendering-driven service or frame work");
    resourceReady = true;
    check(pump->serviceNow() == S_OK && completedResources == 1 && !a.frameCount() && !b.frameCount(),
        "owner timer/wake completes delayed work while both views remain visually unloaded");
    check(a.setPresentationEnabled(true) == S_OK && b.setPresentationEnabled(true) == S_OK, "presentation eligibility is independently enabled per view");
    auto before = services;
    check(a.beginFrame(100) == S_OK && b.beginFrame(100) == S_OK && services == before + 1 && a.frameCount() == 1 && b.frameCount() == 1,
        "one SDK frame epoch services engine once and admits each of two views once");
    check(a.beginFrame(100) == S_FALSE && b.beginFrame(100) == S_FALSE && services == before + 1,
        "duplicate callbacks cannot double service or presentation counts");
    check(a.beginFrame(99) == S_FALSE && b.beginFrame(99) == S_FALSE && services == before + 1, "stale frame epochs cannot rewind presentation state");
    check(a.beginFrame(-1) == E_INVALIDARG && services == before + 1, "negative SDK epoch rejects before native work");
    check(b.beginFrame(101) == S_OK && a.beginFrame(101) == S_OK && services == before + 2, "reversed participant order still has one engine cycle per frame");
    a.setPresentationEnabled(false);
    auto frameA = a.frameCount();
    resourceReady = true;
    check(pump->serviceNow() == S_OK && completedResources == 2 && pump->clientCount() == 2 && !suspensionCalls,
        "visual unload preserves service lease resource progress and active deadline clock");
    check(a.beginFrame(102) == S_FALSE && b.beginFrame(102) == S_OK && a.frameCount() == frameA, "unloaded A cannot block presentation or service for visible B");
    suspendResult = static_cast<WKUWPResult>(E_ACCESSDENIED);
    check(pump->setSuspended(true) == E_ACCESSDENIED && !pump->suspended(), "rejected process suspension preserves original HRESULT and active state");
    suspendResult = WKUWPResultOK;
    check(pump->setSuspended(true) == S_OK && pump->suspended() && suspended, "explicit app suspension reaches provider once");
    auto suspensions = suspensionCalls;
    check(pump->setSuspended(true) == S_OK && suspensionCalls == suspensions, "repeated suspension does not multiply provider suspension nesting");
    before = services;
    resourceReady = true;
    check(pump->serviceNow() == S_FALSE && b.beginFrame(103) == S_FALSE && services == before && completedResources == 2,
        "app suspension pauses both service and presentation while visual unload alone does not");
    check(pump->setSuspended(false) == S_OK && pump->serviceNow() == S_OK && completedResources == 3,
        "resume ends explicit suspension and completes retained work before later frames");
    check(b.beginFrame(103) == S_OK, "resume admits a fresh frame without a stale suspended epoch");
    serviceResult = static_cast<WKUWPResult>(E_ACCESSDENIED);
    before = services;
    a.setPresentationEnabled(true);
    check(a.beginFrame(104) == E_ACCESSDENIED && b.beginFrame(104) == E_ACCESSDENIED && services == before + 1,
        "one failed process service result is shared exactly by both frame participants");
    serviceResult = WKUWPResultOK;
    check(a.beginFrame(105) == S_OK && b.beginFrame(105) == S_OK && services == before + 2, "later valid epoch recovers without repeating a failed epoch");
    reenter = pump.get();
    before = services;
    check(pump->serviceNow() == S_OK && services == before + 1 && pump->deferredServiceCount() == 1, "recursive provider service request cannot nest a generic RunLoop cycle");
    reenter = nullptr;
    unsigned wrongThread = 0;
    before = services;
    std::thread worker([&] {
        wrongThread += pump->serviceNow() == RPC_E_WRONG_THREAD;
        wrongThread += pump->serviceForFrame(106) == RPC_E_WRONG_THREAD;
        wrongThread += pump->setSuspended(true) == RPC_E_WRONG_THREAD;
        wrongThread += a.setPresentationEnabled(false) == RPC_E_WRONG_THREAD;
        wrongThread += a.beginFrame(106) == RPC_E_WRONG_THREAD;
        wrongThread += a.close() == RPC_E_WRONG_THREAD;
    });
    worker.join();
    check(wrongThread == 6 && services == before && pump->clientCount() == 2 && a.presentationEnabled(), "worker mutations reject before service leases visibility or provider state change");
    closeDuringService = &a;
    check(a.beginFrame(106) == S_FALSE && pump->clientCount() == 1 && a.frameCount() == 3, "Close during shared service suppresses the already-retained view frame callback");
    closeDuringService = nullptr;
    check(a.close() == S_OK && a.beginFrame(107) == RO_E_CLOSED && pump->clientCount() == 1, "closed A stays revoked while B keeps one service lease");
    check(b.beginFrame(107) == S_OK, "surviving peer continues normally after other view closes");
    check(b.close() == S_OK && !pump->clientCount() && pump->serviceNow() == S_FALSE, "last ordinary Close ends service demand without unloading engine code");
    FrameSchedulerUWP recreated;
    check(recreated.attach(pump) == S_OK && recreated.setPresentationEnabled(true) == S_OK && recreated.beginFrame(0) == S_OK,
        "recreate starts a fresh SDK epoch after last-client teardown");
    recreated.close();
    check(!pump->clientCount(), "all native leases return to zero baseline");
    std::shared_ptr<OwnerThreadServicePumpUWP> sharedA, sharedB;
    check(OwnerThreadServicePumpUWP::acquire(&api, sharedA) == S_OK && OwnerThreadServicePumpUWP::acquire(&api, sharedB) == S_OK && sharedA == sharedB,
        "separate caller acquisitions return the same process owner pump");
    check(OwnerThreadServicePumpUWP::acquire(&foreign, sharedB) == HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED) && sharedA == sharedB,
        "incompatible process acquisition leaves prior output ownership untouched");
}

} // namespace

// Execute native scheduling laws with explicit per-case assertions and a fail-closed exit.
int main()
{
    run();
    std::printf("M10 frame scheduler: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
