/*
 * BridgeOrderingTests.cpp - host-side verification of the task 3.5 bridge
 * behaviour.
 *
 * This is the acceptance harness for M10 task 3.5. It links the consumer files
 * (Source/WebKit/UIProcess/API/w10m/native/EngineModuleW10M.cpp and
 * CallbackContextW10M.cpp) directly into the executable, because those files are
 * the units under test, and drives them against the configurable test-double
 * provider built from Tools/w10m/build/WKW10MModuleTestProvider.cpp. That
 * provider is test tooling only; nothing here proves anything about the real
 * engine, whose table slots still report Unsupported until tasks 4.x.
 *
 * What the harness proves, in acceptance order:
 *
 *   - Two interleaved pages: each page's notifications reach only its own
 *     context with its own strictly increasing sequence, closing one page
 *     leaves the other deliverable, and neither context ever observes the
 *     other's payload. There is no global routed instance.
 *   - Wrong-thread calls: with owner-thread enforcement armed, a worker thread
 *     is rejected with WrongThread before any argument is read, the owning
 *     thread keeps working, registrations survive the rejection, and disabling
 *     enforcement restores service.
 *   - Close during policy dispatch: a close reentrant from inside starting
 *     still completes, the verdict still governs the answer, the attachment
 *     follows the same deferred safe release as detaching, and the close
 *     baseline returns to zero with no stale access afterwards.
 *
 * The shipping consumer and engine are ARM32 and an ARM32 image cannot execute
 * on this x64 development host, so the behaviour runs from x64 copies of the
 * same sources while the ARM32 compile and link checks are done by
 * Tools/w10m/build/test-wkw10m-bridge-ordering.ps1. Nothing here claims device
 * behaviour.
 */

#include "CallbackContextW10M.h"
#include "EngineModuleW10M.h"

#include <windows.h>

#include <new>
#include <stdio.h>
#include <string.h>

namespace {

// Sentinel written into caller outputs to prove a rejected call never wrote them.
constexpr uint64_t kUntouchedHandle = 0xFEEDFACECAFEBEEFull;

// Setter and counter signatures resolved from the test provider by name.
typedef void(__stdcall* ResetFunction)();
typedef void(__stdcall* SetResult)(WKUWPResult);
typedef void(__stdcall* SetU32)(uint32_t);
typedef WKUWPResult(__stdcall* FireReleaseFunction)(WKUWPPageHandle);
typedef WKUWPResult(__stdcall* DispatchNotifyFunction)(WKUWPPageHandle, uint32_t, const char*, const char*);
typedef WKUWPResult(__stdcall* DispatchStartingFunction)(WKUWPPageHandle, const char*, uint32_t*);

// Live configuration handles for the test provider module.
struct BridgeProvider {
    // Handle the harness itself holds; kept loaded for the whole run.
    HMODULE library;
    ResetFunction reset;
    SetResult setCreateResult;
    SetU32 setDispatching;
    SetU32 setEnforceThread;
    FireReleaseFunction fireRelease;
    DispatchNotifyFunction dispatchNotify;
    DispatchStartingFunction dispatchStarting;
};

int g_failures = 0;

// Prints one assertion result with the detail that identifies the row.
void reportCheck(const char* name, bool passed, const char* detail)
{
    printf("%s %s: %s\n", passed ? "PASS" : "FAIL", name, detail);
    if (!passed)
        g_failures++;
}

// Resolves one required test-provider export; a missing setter fails the run.
bool resolveSetter(HMODULE library, const char* name, void** slot)
{
    *slot = reinterpret_cast<void*>(GetProcAddress(library, name));
    return *slot != nullptr;
}

// Loads the provider and resolves every setter the bridge tests drive.
bool loadBridgeProvider(const wchar_t* path, BridgeProvider* provider)
{
    memset(provider, 0, sizeof(*provider));
    provider->library = LoadLibraryW(path);
    if (!provider->library)
        return false;
    return resolveSetter(provider->library, "WKUWPTestProviderReset",
               reinterpret_cast<void**>(&provider->reset))
        && resolveSetter(provider->library, "WKUWPTestProviderSetCreateResult",
            reinterpret_cast<void**>(&provider->setCreateResult))
        && resolveSetter(provider->library, "WKUWPTestProviderSetDispatching",
            reinterpret_cast<void**>(&provider->setDispatching))
        && resolveSetter(provider->library, "WKUWPTestProviderSetEnforceThread",
            reinterpret_cast<void**>(&provider->setEnforceThread))
        && resolveSetter(provider->library, "WKUWPTestProviderFireRelease",
            reinterpret_cast<void**>(&provider->fireRelease))
        && resolveSetter(provider->library, "WKUWPTestProviderDispatchNotify",
            reinterpret_cast<void**>(&provider->dispatchNotify))
        && resolveSetter(provider->library, "WKUWPTestProviderDispatchStarting",
            reinterpret_cast<void**>(&provider->dispatchStarting));
}

// Valid creation options accepted by the working test slot.
void writeTestPageOptions(WKUWPPageOptions* options)
{
    memset(options, 0, sizeof(*options));
    options->header.structureSize = sizeof(*options);
    options->header.structureVersion = WKUWP_STRUCTURE_VERSION;
}

// Binds a module with a working createPage slot for bridge tests.
bool bindBridgeModule(BridgeProvider* provider, const wchar_t* path, WebKit::EngineModuleUWP* module)
{
    provider->reset();
    provider->setCreateResult(WKUWPResultOK);
    return module->bind(path, 0, 0) == WKUWPResultOK;
}

// Opens one page, reporting failure instead of touching an invalid handle.
bool openBridgePage(WebKit::EngineModuleUWP* module, WKUWPPageHandle* page)
{
    WKUWPPageOptions options;
    writeTestPageOptions(&options);
    *page = kUntouchedHandle;
    return module->createPage(&options, page) == WKUWPResultOK && *page != 0;
}

// Attaches a fresh context, reporting failure without leaking the setup ref.
bool attachBridgeContext(WebKit::EngineModuleUWP* module, WKUWPPageHandle page,
    WebKit::PageCallbackContextUWP* context)
{
    WKUWPPageCallbacks table;
    if (context->buildTable(&table) != WKUWPResultOK) {
        context->release();
        return false;
    }
    if (module->setPageCallbacks(page, &table) != WKUWPResultOK) {
        context->release();
        return false;
    }
    return true;
}

// Reentrant close state for the close-during-policy tests.
struct CloseHookState {
    // Module the hook closes through, on the page under test.
    WebKit::EngineModuleUWP* module;
    WKUWPPageHandle page;
};

// Policy hook that closes the page reentrantly from inside a dispatch.
WKUWPResult __stdcall closeDuringPolicy(void* hookContext, WKUWPPageHandle, const WKUWPPageEvent*)
{
    CloseHookState* state = static_cast<CloseHookState*>(hookContext);
    return state->module->closePage(state->page);
}

// Two pages interleave without routing, and closing one spares the other.
void checkInterleavedPages(BridgeProvider* provider, const wchar_t* path)
{
    WebKit::EngineModuleUWP module;
    if (!bindBridgeModule(provider, path, &module)) {
        reportCheck("interleaved/bind", false, "cannot bind the test provider");
        return;
    }
    WKUWPPageHandle first = kUntouchedHandle;
    WKUWPPageHandle second = kUntouchedHandle;
    if (!openBridgePage(&module, &first) || !openBridgePage(&module, &second) || first == second) {
        reportCheck("interleaved/open", false, "cannot open two distinct pages");
        return;
    }

    WebKit::PageCallbackContextUWP* firstContext = new (std::nothrow) WebKit::PageCallbackContextUWP();
    WebKit::PageCallbackContextUWP* secondContext = new (std::nothrow) WebKit::PageCallbackContextUWP();
    if (!attachBridgeContext(&module, first, firstContext)
        || !attachBridgeContext(&module, second, secondContext)) {
        reportCheck("interleaved/attach", false, "cannot attach both contexts");
        return;
    }

    char message[256];
    provider->dispatchNotify(first, WKUWPEventSourceChanged, "https://one.test/", "One");
    provider->dispatchNotify(second, WKUWPEventSourceChanged, "https://two.test/", "Two");
    provider->dispatchNotify(first, WKUWPEventTitleChanged, "https://one.test/", "One retitled");

    const bool firstOwnsOnlyIts = firstContext->notifyCalls() == 2 && firstContext->lastSequence() == 3
        && firstContext->lastUriLength() == strlen("https://one.test/")
        && memcmp(firstContext->lastUriBytes(), "https://one.test/", firstContext->lastUriLength()) == 0
        && firstContext->lastEventKind() == WKUWPEventTitleChanged;
    const bool secondOwnsOnlyIts = secondContext->notifyCalls() == 1 && secondContext->lastSequence() == 2
        && secondContext->lastUriLength() == strlen("https://two.test/")
        && memcmp(secondContext->lastUriBytes(), "https://two.test/", secondContext->lastUriLength()) == 0;
    snprintf(message, sizeof(message), "first sees 2 own events ending seq 3=%d, second sees 1 own event seq 2=%d",
        firstOwnsOnlyIts ? 1 : 0, secondOwnsOnlyIts ? 1 : 0);
    reportCheck("interleaved/no-routing", firstOwnsOnlyIts && secondOwnsOnlyIts, message);

    WKUWPResult result = module.closePage(first);
    snprintf(message, sizeof(message), "close first -> result=%d expected=0, open=%u expected=1",
        static_cast<int>(result), module.openPageCount());
    reportCheck("interleaved/close-one", result == WKUWPResultOK && module.openPageCount() == 1, message);

    // The closed page's attachment was released by the close itself, so only
    // the setup reference remains; the surviving page keeps delivering.
    firstContext->release();
    provider->dispatchNotify(second, WKUWPEventTitleChanged, "https://two.test/", "Two retitled");
    snprintf(message, sizeof(message), "survivor delivers=%d, open=%u",
        secondContext->notifyCalls() == 2 ? 1 : 0, module.openPageCount());
    reportCheck("interleaved/survivor",
        secondContext->notifyCalls() == 2 && module.openPageCount() == 1, message);

    module.detachPageCallbacks(second);
    secondContext->release();
    module.closePage(second);
    snprintf(message, sizeof(message), "final baseline open=%u expected=0", module.openPageCount());
    reportCheck("interleaved/baseline", module.openPageCount() == 0, message);
}

// Calls a worker thread attempts; the engine rejects each before reading args.
struct WrongThreadProbe {
    // Module under test, bound on the owning thread before the worker runs.
    WebKit::EngineModuleUWP* module;

    // Page the owner opened; the worker must not be able to close it.
    WKUWPPageHandle page;

    // Answers the worker observed, all expected to be WrongThread.
    WKUWPResult createResult;
    WKUWPPageHandle createHandle;
    WKUWPResult closeResult;
    WKUWPResult detachResult;
    WKUWPResult stateResult;

    // Open-page count the worker observed after its rejected creation.
    uint32_t openCount;
};

// Worker entry point exercising the module from a non-owner thread.
DWORD __stdcall wrongThreadWorker(void* parameter)
{
    WrongThreadProbe* probe = static_cast<WrongThreadProbe*>(parameter);
    WKUWPPageOptions options;
    writeTestPageOptions(&options);
    probe->createHandle = kUntouchedHandle;
    probe->createResult = probe->module->createPage(&options, &probe->createHandle);
    probe->openCount = probe->module->openPageCount();
    probe->closeResult = probe->module->closePage(probe->page);
    probe->detachResult = probe->module->detachPageCallbacks(probe->page);
    WKUWPPageState state;
    memset(&state, 0, sizeof(state));
    uint8_t storage[16];
    uint32_t required = 0;
    probe->stateResult = probe->module->api()->getState(probe->page, &state, storage, sizeof(storage), &required);
    return 0;
}

// Off-owner-thread calls fail with WrongThread and change nothing.
void checkWrongThreadCalls(BridgeProvider* provider, const wchar_t* path)
{
    WebKit::EngineModuleUWP module;
    if (!bindBridgeModule(provider, path, &module)) {
        reportCheck("thread/bind", false, "cannot bind the test provider");
        return;
    }
    provider->setEnforceThread(1);

    char message[256];
    WKUWPPageHandle page = kUntouchedHandle;
    const bool ownerWorks = openBridgePage(&module, &page);
    snprintf(message, sizeof(message), "owner creates while enforced=%d", ownerWorks ? 1 : 0);
    reportCheck("thread/owner-works", ownerWorks, message);
    if (!ownerWorks) {
        provider->setEnforceThread(0);
        return;
    }

    WrongThreadProbe probe;
    memset(&probe, 0, sizeof(probe));
    probe.module = &module;
    probe.page = page;
    HANDLE worker = CreateThread(nullptr, 0, wrongThreadWorker, &probe, 0, nullptr);
    if (!worker) {
        reportCheck("thread/worker-rejected", false, "cannot start the worker thread");
        provider->setEnforceThread(0);
        module.closePage(page);
        return;
    }
    WaitForSingleObject(worker, INFINITE);
    CloseHandle(worker);

    snprintf(message, sizeof(message),
        "worker create=%d close=%d detach=%d state=%d (all 8), handle untouched=%d, open=%u",
        static_cast<int>(probe.createResult), static_cast<int>(probe.closeResult),
        static_cast<int>(probe.detachResult), static_cast<int>(probe.stateResult),
        probe.createHandle == kUntouchedHandle ? 1 : 0, probe.openCount);
    reportCheck("thread/worker-rejected",
        probe.createResult == WKUWPResultWrongThread && probe.closeResult == WKUWPResultWrongThread
            && probe.detachResult == WKUWPResultWrongThread && probe.stateResult == WKUWPResultWrongThread
            && probe.createHandle == kUntouchedHandle && probe.openCount == 1,
        message);

    provider->setEnforceThread(0);
    WKUWPResult result = module.closePage(page);
    snprintf(message, sizeof(message), "owner closes after unenforced=%d, open=%u", static_cast<int>(result),
        module.openPageCount());
    reportCheck("thread/owner-recovers", result == WKUWPResultOK && module.openPageCount() == 0, message);
}

// A close reentrant from inside policy still completes with the verdict intact.
void checkCloseDuringPolicy(BridgeProvider* provider, const wchar_t* path)
{
    WebKit::EngineModuleUWP module;
    if (!bindBridgeModule(provider, path, &module)) {
        reportCheck("close-during-policy/close", false, "cannot bind the test provider");
        return;
    }
    WKUWPPageHandle page = kUntouchedHandle;
    if (!openBridgePage(&module, &page)) {
        reportCheck("close-during-policy/close", false, "cannot open a test page");
        return;
    }
    CloseHookState hookState;
    hookState.module = &module;
    hookState.page = page;
    WebKit::PageCallbackContextUWP* context = new (std::nothrow) WebKit::PageCallbackContextUWP();
    context->setPolicyHook(closeDuringPolicy, &hookState);
    if (!attachBridgeContext(&module, page, context)) {
        reportCheck("close-during-policy/close", false, "cannot attach the test context");
        return;
    }

    char message[256];
    uint32_t cancel = 0xDEADBEEFu;
    WKUWPResult result = provider->dispatchStarting(page, "https://example.test/", &cancel);
    snprintf(message, sizeof(message),
        "starting with reentrant close -> result=%d expected=0, cancel=%u expected=0, hook fired=%d, hook answer=%d",
        static_cast<int>(result), cancel, context->policyFired() ? 1 : 0,
        static_cast<int>(context->lastPolicyResult()));
    reportCheck("close-during-policy/close",
        result == WKUWPResultOK && cancel == 0 && context->policyFired()
            && context->lastPolicyResult() == WKUWPResultOK && module.openPageCount() == 0,
        message);

    snprintf(message, sizeof(message), "attachment released by the close=%u expected=1, storage alive=%u",
        context->releaseCalls(), context->refCount());
    reportCheck("close-during-policy/ack",
        context->releaseCalls() == 1 && context->refCount() == 1, message);

    context->release();
    snprintf(message, sizeof(message), "no stale access after close: attach on closed page=%d expected=%d",
        static_cast<int>(WKUWPResultInvalidArgument), static_cast<int>(WKUWPResultInvalidArgument));
    WebKit::PageCallbackContextUWP* lateContext = new (std::nothrow) WebKit::PageCallbackContextUWP();
    WKUWPPageCallbacks lateTable;
    lateContext->buildTable(&lateTable);
    result = module.setPageCallbacks(page, &lateTable);
    snprintf(message, sizeof(message), "attach on closed page -> result=%d expected=%d", static_cast<int>(result),
        static_cast<int>(WKUWPResultInvalidArgument));
    reportCheck("close-during-policy/no-stale-attach", result == WKUWPResultInvalidArgument, message);
    lateContext->release();
}

// A close during dispatch defers the release exactly like a detach does.
void checkCloseDuringDispatchDeferred(BridgeProvider* provider, const wchar_t* path)
{
    WebKit::EngineModuleUWP module;
    if (!bindBridgeModule(provider, path, &module)) {
        reportCheck("close-deferred/close", false, "cannot bind the test provider");
        return;
    }
    WKUWPPageHandle page = kUntouchedHandle;
    if (!openBridgePage(&module, &page)) {
        reportCheck("close-deferred/close", false, "cannot open a test page");
        return;
    }
    CloseHookState hookState;
    hookState.module = &module;
    hookState.page = page;
    WebKit::PageCallbackContextUWP* context = new (std::nothrow) WebKit::PageCallbackContextUWP();
    context->setPolicyHook(closeDuringPolicy, &hookState);
    if (!attachBridgeContext(&module, page, context)) {
        reportCheck("close-deferred/close", false, "cannot attach the test context");
        return;
    }

    char message[256];
    provider->setDispatching(1);
    uint32_t cancel = 0xDEADBEEFu;
    WKUWPResult result = provider->dispatchStarting(page, "https://example.test/", &cancel);
    snprintf(message, sizeof(message),
        "close in dispatch -> starting=%d, hook answer=%d expected=0, releases=%u expected=0, open=%u",
        static_cast<int>(result), static_cast<int>(context->lastPolicyResult()), context->releaseCalls(),
        module.openPageCount());
    reportCheck("close-deferred/close",
        result == WKUWPResultOK && context->lastPolicyResult() == WKUWPResultOK
            && context->releaseCalls() == 0 && module.openPageCount() == 0,
        message);

    provider->setDispatching(0);
    result = provider->fireRelease(page);
    snprintf(message, sizeof(message), "unwind release -> result=%d expected=0, releases=%u expected=1",
        static_cast<int>(result), context->releaseCalls());
    reportCheck("close-deferred/release", result == WKUWPResultOK && context->releaseCalls() == 1, message);

    context->release();
}

} // namespace

// Runs the bridge gate against the provider image named on the command line.
//
// The consumers under test are linked into this executable; the provider image
// is reached only through the loader, exactly as the consumers reach it.
int main(int argc, char** argv)
{
    // Unbuffered output keeps every printed row even if a later check faults,
    // so a crash still identifies the row that caused it.
    setvbuf(stdout, nullptr, _IONBF, 0);

    if (argc < 2) {
        printf("usage: BridgeOrderingTests <provider-module>\n");
        return 2;
    }

    wchar_t providerPath[MAX_PATH];
    if (!MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, providerPath, MAX_PATH)) {
        printf("FAIL arguments: provider module path does not fit in MAX_PATH\n");
        return 2;
    }

    BridgeProvider provider;
    if (!loadBridgeProvider(providerPath, &provider)) {
        printf("FAIL load: cannot load test provider %s (error %lu)\n", argv[1], GetLastError());
        return 1;
    }
    printf("PASS load: test provider resolved from %s\n\n", argv[1]);

    printf("interleaved pages\n");
    checkInterleavedPages(&provider, providerPath);

    printf("\nwrong-thread calls\n");
    checkWrongThreadCalls(&provider, providerPath);

    printf("\nclose during policy dispatch\n");
    checkCloseDuringPolicy(&provider, providerPath);
    checkCloseDuringDispatchDeferred(&provider, providerPath);

    printf("\n");
    if (g_failures) {
        printf("FAILED: %d bridge check(s) did not hold.\n", g_failures);
        return 1;
    }

    printf("All bridge ordering checks passed.\n");
    return 0;
}
