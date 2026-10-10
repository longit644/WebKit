/*
 * EngineModuleUWPTests.cpp - host-side verification of the task 3.3 consumer.
 *
 * This is the acceptance harness for M10 task 3.3. It links the consumer
 * (Source/WebKit/UIProcess/API/uwp/native/EngineModuleUWP.cpp) directly into
 * the executable, because that file is the unit under test, and drives it
 * against the configurable test provider built from
 * Tools/uwp/build/WKUWPModuleTestProvider.cpp. That provider is test tooling
 * only; nothing here proves anything about the real engine provider.
 *
 * What the harness proves, in acceptance order:
 *
 *   - A compatible provider resolves: bind reports Ok, the table names major 1,
 *     the module stays bound, and binding again performs no second load.
 *   - An incompatible engine identity is rejected: a foreign major, a truncated
 *     reported extent, a null slot, a missing export and a missing capability
 *     each fail with a frozen code and leave the module unbound.
 *   - The engine module is never freed once pinned: it is observably loaded
 *     after binding, after failed operations and after shutdown begins, while a
 *     rejected binding on a module nobody else holds leaves that module unloaded.
 *   - Per-page cleanup returns to baseline: create/close/recreate ends at zero
 *     open pages, unknown handles never reach the engine, and a failed creation
 *     registers nothing and writes no handle.
 *   - Shutdown latches: beginShutdown forwards once, page creation then fails
 *     locally without calling the engine, and pollShutdown passes the engine's
 *     answer through.
 *
 * The shipping consumer and engine are ARM32 and an ARM32 image cannot execute
 * on this x64 development host, so the behaviour runs from x64 copies of the
 * same sources while the ARM32 compile and link checks are done by
 * Tools/uwp/build/test-wkuwp-engine-module.ps1. Nothing here claims device
 * behaviour.
 */

#include "EngineModuleUWP.h"

#include <windows.h>

#include <stdio.h>
#include <string.h>

namespace {

// Sentinel written into caller outputs to prove a rejected call never wrote them.
constexpr uint64_t kUntouchedHandle = 0xFEEDFACECAFEBEEFull;

// closePage slot index in the test provider's declaration order.
constexpr uint32_t kTestClosePageSlot = 5;

// Truncated reported extent that still covers the header but not the table.
constexpr uint32_t kTestTruncatedBytes = 64;

// Setter and counter signatures resolved from the test provider by name.
typedef void(__stdcall* SetU64)(uint64_t);
typedef void(__stdcall* SetU32Pair)(uint32_t, uint32_t);
typedef void(__stdcall* SetU32)(uint32_t);
typedef void(__stdcall* SetResult)(WKUWPResult);
typedef void(__stdcall* ResetFunction)();
typedef uint32_t(__stdcall* CounterFunction)();

// Live configuration handles for the test provider module.
struct TestProvider {
    // Handle the harness itself holds; kept loaded for the whole run.
    HMODULE library;
    SetU64 setCapabilities;
    SetU32Pair setAbiVersion;
    SetU32 setTruncateBytes;
    SetU32 setNullSlot;
    SetResult setCreateResult;
    SetResult setCloseResult;
    SetResult setShutdownResult;
    SetResult setPollResult;
    ResetFunction reset;
    CounterFunction discoveryCalls;
    CounterFunction createCalls;
    CounterFunction closeCalls;
    CounterFunction beginShutdownCalls;
    CounterFunction pollShutdownCalls;
};

int g_failures = 0;

// Prints one assertion result with the detail that identifies the row.
void reportCheck(const char* name, bool passed, const char* detail)
{
    printf("%s %s: %s\n", passed ? "PASS" : "FAIL", name, detail);
    if (!passed)
        g_failures++;
}

// True while the named module image is loaded, without changing its refcount.
bool isModuleLoaded(const wchar_t* path)
{
    HMODULE found = nullptr;
    BOOL loaded = GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, path, &found);
    return loaded && found;
}

// Resolves one required test-provider export; a missing setter fails the run.
bool resolveSetter(HMODULE library, const char* name, void** slot)
{
    *slot = reinterpret_cast<void*>(GetProcAddress(library, name));
    return *slot != nullptr;
}

// Loads the provider and resolves every setter and counter the tests drive.
bool loadTestProvider(const wchar_t* path, TestProvider* provider)
{
    memset(provider, 0, sizeof(*provider));
    provider->library = LoadLibraryW(path);
    if (!provider->library)
        return false;
    return resolveSetter(provider->library, "WKUWPTestProviderSetCapabilities",
               reinterpret_cast<void**>(&provider->setCapabilities))
        && resolveSetter(provider->library, "WKUWPTestProviderSetAbiVersion",
            reinterpret_cast<void**>(&provider->setAbiVersion))
        && resolveSetter(provider->library, "WKUWPTestProviderSetTruncateBytes",
            reinterpret_cast<void**>(&provider->setTruncateBytes))
        && resolveSetter(provider->library, "WKUWPTestProviderSetNullSlot",
            reinterpret_cast<void**>(&provider->setNullSlot))
        && resolveSetter(provider->library, "WKUWPTestProviderSetCreateResult",
            reinterpret_cast<void**>(&provider->setCreateResult))
        && resolveSetter(provider->library, "WKUWPTestProviderSetCloseResult",
            reinterpret_cast<void**>(&provider->setCloseResult))
        && resolveSetter(provider->library, "WKUWPTestProviderSetShutdownResult",
            reinterpret_cast<void**>(&provider->setShutdownResult))
        && resolveSetter(provider->library, "WKUWPTestProviderSetPollResult",
            reinterpret_cast<void**>(&provider->setPollResult))
        && resolveSetter(provider->library, "WKUWPTestProviderReset",
            reinterpret_cast<void**>(&provider->reset))
        && resolveSetter(provider->library, "WKUWPTestProviderDiscoveryCalls",
            reinterpret_cast<void**>(&provider->discoveryCalls))
        && resolveSetter(provider->library, "WKUWPTestProviderCreateCalls",
            reinterpret_cast<void**>(&provider->createCalls))
        && resolveSetter(provider->library, "WKUWPTestProviderCloseCalls",
            reinterpret_cast<void**>(&provider->closeCalls))
        && resolveSetter(provider->library, "WKUWPTestProviderBeginShutdownCalls",
            reinterpret_cast<void**>(&provider->beginShutdownCalls))
        && resolveSetter(provider->library, "WKUWPTestProviderPollShutdownCalls",
            reinterpret_cast<void**>(&provider->pollShutdownCalls));
}

// Valid creation options accepted by the working test slot.
void writeTestPageOptions(WKUWPPageOptions* options)
{
    memset(options, 0, sizeof(*options));
    options->header.structureSize = sizeof(*options);
    options->header.structureVersion = WKUWP_STRUCTURE_VERSION;
}

// Compatible resolution binds once and stays bound without a second load.
void checkCompatibleResolution(TestProvider* provider, const wchar_t* path)
{
    provider->reset();
    WebKit::EngineModuleUWP module;

    WKUWPResult result = module.bind(path, 0, 0);
    char message[256];
    snprintf(message, sizeof(message), "bind -> result=%d expected=0, bound=%d, api=%p", static_cast<int>(result),
        module.isBound() ? 1 : 0, module.api());
    reportCheck("bind/compatible", result == WKUWPResultOK && module.isBound() && module.api(), message);
    if (!module.api()) {
        reportCheck("bind/identity", false, "compatible negotiation failed, cannot continue");
        return;
    }

    const bool identity = module.api()->abiMajor == WKUWP_ABI_MAJOR_VERSION && module.api()->initialize
        && module.api()->createPage && module.api()->closePage && module.api()->beginShutdown
        && module.api()->pollShutdown;
    snprintf(message, sizeof(message), "negotiated table names major 1 with callable slots=%d", identity ? 1 : 0);
    reportCheck("bind/identity", identity, message);

    const uint32_t callsBefore = provider->discoveryCalls();
    result = module.bind(path, 0, 0);
    snprintf(message, sizeof(message), "second bind -> result=%d, discovery calls %u -> %u", static_cast<int>(result),
        callsBefore, provider->discoveryCalls());
    reportCheck("bind/idempotent",
        result == WKUWPResultOK && provider->discoveryCalls() == callsBefore, message);

    snprintf(message, sizeof(message), "module handle=%p, image loaded=%d, open pages=%u", module.moduleHandle(),
        isModuleLoaded(path) ? 1 : 0, module.openPageCount());
    reportCheck("bind/pinned",
        module.moduleHandle() && isModuleLoaded(path) && module.openPageCount() == 0, message);
}

// A requested capability the provider delivers negotiates successfully.
void checkCapabilityGatedSuccess(TestProvider* provider, const wchar_t* path)
{
    provider->reset();
    provider->setCapabilities(WKUWPCapabilityStructuredNotifications | WKUWPCapabilityHostHtml);
    WebKit::EngineModuleUWP module;

    WKUWPResult result = module.bind(path, 0, WKUWPCapabilityStructuredNotifications);
    char message[256];
    snprintf(message, sizeof(message), "bind with a delivered capability -> result=%d expected=0",
        static_cast<int>(result));
    reportCheck("capability/delivered", result == WKUWPResultOK && module.isBound(), message);
}

// A requested capability the provider does not deliver fails before pinning.
void checkMissingCapability(TestProvider* provider, const wchar_t* path)
{
    provider->reset();
    WebKit::EngineModuleUWP module;

    WKUWPResult result = module.bind(path, 0, WKUWPCapabilityStructuredNotifications);
    char message[256];
    snprintf(message, sizeof(message), "bind with an undelivered capability -> result=%d expected=%d, bound=%d",
        static_cast<int>(result), static_cast<int>(WKUWPResultMissingCapability), module.isBound() ? 1 : 0);
    reportCheck("capability/missing",
        result == WKUWPResultMissingCapability && !module.isBound() && !module.api(), message);
}

// A provider that reports a foreign major is rejected as an incompatible identity.
void checkForeignMajor(TestProvider* provider, const wchar_t* path)
{
    provider->reset();
    provider->setAbiVersion(2, 0);
    WebKit::EngineModuleUWP module;

    WKUWPResult result = module.bind(path, 0, 0);
    char message[256];
    snprintf(message, sizeof(message), "bind reporting major 2 -> result=%d expected=%d, bound=%d",
        static_cast<int>(result), static_cast<int>(WKUWPResultAbiMismatch), module.isBound() ? 1 : 0);
    reportCheck("identity/foreign-major",
        result == WKUWPResultAbiMismatch && !module.isBound() && !module.api(), message);
}

// A provider whose reported extent does not cover the table is rejected on size.
void checkTruncatedTable(TestProvider* provider, const wchar_t* path)
{
    provider->reset();
    provider->setTruncateBytes(kTestTruncatedBytes);
    WebKit::EngineModuleUWP module;

    WKUWPResult result = module.bind(path, 0, 0);
    char message[256];
    snprintf(message, sizeof(message), "bind with a %u-byte reported extent -> result=%d expected=%d, bound=%d",
        kTestTruncatedBytes, static_cast<int>(result), static_cast<int>(WKUWPResultAbiMismatch),
        module.isBound() ? 1 : 0);
    reportCheck("identity/truncated",
        result == WKUWPResultAbiMismatch && !module.isBound() && !module.api(), message);
}

// A provider with a null slot is rejected instead of handing out a dead call.
void checkNullSlot(TestProvider* provider, const wchar_t* path)
{
    provider->reset();
    provider->setNullSlot(kTestClosePageSlot);
    WebKit::EngineModuleUWP module;

    WKUWPResult result = module.bind(path, 0, 0);
    char message[256];
    snprintf(message, sizeof(message), "bind with a null closePage slot -> result=%d expected=%d, bound=%d",
        static_cast<int>(result), static_cast<int>(WKUWPResultAbiMismatch), module.isBound() ? 1 : 0);
    reportCheck("identity/null-slot",
        result == WKUWPResultAbiMismatch && !module.isBound() && !module.api(), message);
}

// A module without the discovery export is not a V1 engine.
void checkMissingExport()
{
    wchar_t systemPath[MAX_PATH];
    if (!GetSystemDirectoryW(systemPath, MAX_PATH)) {
        reportCheck("identity/missing-export", false, "cannot locate the system directory");
        return;
    }
    wcscat_s(systemPath, L"\\kernel32.dll");
    WebKit::EngineModuleUWP module;

    WKUWPResult result = module.bind(systemPath, 0, 0);
    char message[256];
    snprintf(message, sizeof(message), "bind without the discovery export -> result=%d expected=%d, bound=%d",
        static_cast<int>(result), static_cast<int>(WKUWPResultAbiMismatch), module.isBound() ? 1 : 0);
    reportCheck("identity/missing-export",
        result == WKUWPResultAbiMismatch && !module.isBound() && !module.api(), message);
}

// A null module path is the caller's own bug, reported before any load.
void checkNullPath()
{
    WebKit::EngineModuleUWP module;

    WKUWPResult result = module.bind(nullptr, 0, 0);
    char message[128];
    snprintf(message, sizeof(message), "bind with a null path -> result=%d expected=%d, bound=%d",
        static_cast<int>(result), static_cast<int>(WKUWPResultInvalidArgument), module.isBound() ? 1 : 0);
    reportCheck("argument/null-path",
        result == WKUWPResultInvalidArgument && !module.isBound() && !module.api(), message);
}

// Every operation on an unbound module reports NotInitialized without an engine.
void checkUnboundOperations()
{
    WebKit::EngineModuleUWP module;
    WKUWPPageOptions options;
    writeTestPageOptions(&options);
    WKUWPPageHandle page = kUntouchedHandle;

    const bool createRejected = module.createPage(&options, &page) == WKUWPResultNotInitialized;
    const bool closeRejected = module.closePage(7) == WKUWPResultNotInitialized;
    const bool shutdownRejected = module.beginShutdown() == WKUWPResultNotInitialized;
    const bool pollRejected = module.pollShutdown() == WKUWPResultNotInitialized;

    char message[256];
    snprintf(message, sizeof(message), "create/close/shutdown/poll without binding -> %d%d%d%d, handle untouched=%d",
        createRejected ? 1 : 0, closeRejected ? 1 : 0, shutdownRejected ? 1 : 0, pollRejected ? 1 : 0,
        page == kUntouchedHandle ? 1 : 0);
    reportCheck("unbound/operations",
        createRejected && closeRejected && shutdownRejected && pollRejected && page == kUntouchedHandle
            && !module.api() && module.openPageCount() == 0 && !module.isShuttingDown(),
        message);
}

// Create/close/recreate returns the open-page count to its zero baseline twice.
void checkCloseRecreateBaseline(TestProvider* provider, const wchar_t* path)
{
    provider->reset();
    provider->setCreateResult(WKUWPResultOK);
    WebKit::EngineModuleUWP module;
    if (module.bind(path, 0, 0) != WKUWPResultOK) {
        reportCheck("page/baseline", false, "compatible negotiation failed, cannot continue");
        return;
    }

    WKUWPPageOptions options;
    writeTestPageOptions(&options);
    char message[256];

    WKUWPPageHandle first = kUntouchedHandle;
    WKUWPResult result = module.createPage(&options, &first);
    snprintf(message, sizeof(message), "create -> result=%d, handle=0x%llx, open=%u, engine creates=%u",
        static_cast<int>(result), first, module.openPageCount(), provider->createCalls());
    reportCheck("page/create", result == WKUWPResultOK && first != 0 && module.openPageCount() == 1 && provider->createCalls() == 1, message);

    result = module.closePage(first);
    snprintf(message, sizeof(message), "close -> result=%d, open=%u, engine closes=%u", static_cast<int>(result),
        module.openPageCount(), provider->closeCalls());
    reportCheck("page/close", result == WKUWPResultOK && module.openPageCount() == 0 && provider->closeCalls() == 1, message);

    WKUWPPageHandle second = kUntouchedHandle;
    result = module.createPage(&options, &second);
    const bool recreated = result == WKUWPResultOK && second != 0 && second != first
        && module.openPageCount() == 1;
    snprintf(message, sizeof(message), "recreate -> result=%d, distinct handle=%d, open=%u", static_cast<int>(result),
        second != first ? 1 : 0, module.openPageCount());
    reportCheck("page/recreate", recreated, message);

    result = module.closePage(second);
    snprintf(message, sizeof(message), "close again -> result=%d, open=%u, engine closes=%u", static_cast<int>(result),
        module.openPageCount(), provider->closeCalls());
    reportCheck("page/baseline", result == WKUWPResultOK && module.openPageCount() == 0 && provider->closeCalls() == 2, message);
}

// Handles this object never created never reach the engine.
void checkUnknownHandles(TestProvider* provider, const wchar_t* path)
{
    provider->reset();
    provider->setCreateResult(WKUWPResultOK);
    WebKit::EngineModuleUWP module;
    if (module.bind(path, 0, 0) != WKUWPResultOK) {
        reportCheck("page/unknown", false, "compatible negotiation failed, cannot continue");
        return;
    }

    const uint32_t closesBefore = provider->closeCalls();
    const bool foreignRejected = module.closePage(0x1234) == WKUWPResultInvalidArgument;
    const bool zeroRejected = module.closePage(0) == WKUWPResultInvalidArgument;

    char message[256];
    snprintf(message, sizeof(message), "foreign/zero close -> rejected=%d%d, engine closes %u -> %u",
        foreignRejected ? 1 : 0, zeroRejected ? 1 : 0, closesBefore, provider->closeCalls());
    reportCheck("page/unknown",
        foreignRejected && zeroRejected && provider->closeCalls() == closesBefore && module.openPageCount() == 0,
        message);
}

// A failed creation registers nothing and writes no handle.
void checkFailedCreation(TestProvider* provider, const wchar_t* path)
{
    provider->reset();
    provider->setCreateResult(WKUWPResultUnsupported);
    WebKit::EngineModuleUWP module;
    if (module.bind(path, 0, 0) != WKUWPResultOK) {
        reportCheck("page/failed-creation", false, "compatible negotiation failed, cannot continue");
        return;
    }

    WKUWPPageOptions options;
    writeTestPageOptions(&options);
    WKUWPPageHandle page = kUntouchedHandle;
    WKUWPResult result = module.createPage(&options, &page);

    char message[256];
    snprintf(message, sizeof(message), "refused creation -> result=%d expected=%d, handle untouched=%d, open=%u",
        static_cast<int>(result), static_cast<int>(WKUWPResultUnsupported), page == kUntouchedHandle ? 1 : 0,
        module.openPageCount());
    reportCheck("page/failed-creation",
        result == WKUWPResultUnsupported && page == kUntouchedHandle && module.openPageCount() == 0, message);
}

// Shutdown latches: creation fails locally afterwards and progress passes through.
void checkShutdownLatch(TestProvider* provider, const wchar_t* path)
{
    provider->reset();
    provider->setCreateResult(WKUWPResultOK);
    WebKit::EngineModuleUWP module;
    if (module.bind(path, 0, 0) != WKUWPResultOK) {
        reportCheck("shutdown/begin", false, "compatible negotiation failed, cannot continue");
        return;
    }

    char message[256];
    WKUWPResult result = module.beginShutdown();
    snprintf(message, sizeof(message), "beginShutdown -> result=%d expected=0, latched=%d, engine calls=%u",
        static_cast<int>(result), module.isShuttingDown() ? 1 : 0, provider->beginShutdownCalls());
    reportCheck("shutdown/begin",
        result == WKUWPResultOK && module.isShuttingDown() && provider->beginShutdownCalls() == 1, message);

    WKUWPPageOptions options;
    writeTestPageOptions(&options);
    WKUWPPageHandle page = kUntouchedHandle;
    const uint32_t createsBefore = provider->createCalls();
    result = module.createPage(&options, &page);
    snprintf(message, sizeof(message), "creation after shutdown -> result=%d expected=%d, engine creates %u -> %u",
        static_cast<int>(result), static_cast<int>(WKUWPResultShuttingDown), createsBefore,
        provider->createCalls());
    reportCheck("shutdown/creation-blocked",
        result == WKUWPResultShuttingDown && provider->createCalls() == createsBefore
            && page == kUntouchedHandle && module.openPageCount() == 0,
        message);

    provider->setPollResult(WKUWPResultPending);
    result = module.pollShutdown();
    snprintf(message, sizeof(message), "pollShutdown -> result=%d expected=%d, engine polls=%u",
        static_cast<int>(result), static_cast<int>(WKUWPResultPending), provider->pollShutdownCalls());
    reportCheck("shutdown/poll-pending",
        result == WKUWPResultPending && provider->pollShutdownCalls() == 1, message);

    provider->setPollResult(WKUWPResultOK);
    result = module.pollShutdown();
    snprintf(message, sizeof(message), "pollShutdown -> result=%d expected=0, engine polls=%u", static_cast<int>(result),
        provider->pollShutdownCalls());
    reportCheck("shutdown/poll-done", result == WKUWPResultOK && provider->pollShutdownCalls() == 2, message);

    result = module.beginShutdown();
    snprintf(message, sizeof(message), "second beginShutdown -> result=%d expected=0, engine calls=%u",
        static_cast<int>(result), provider->beginShutdownCalls());
    reportCheck("shutdown/idempotent",
        result == WKUWPResultOK && provider->beginShutdownCalls() == 2, message);
}

// The pinned image stays loaded across binding, failed calls and shutdown.
void checkPinningLifecycle(TestProvider* provider, const wchar_t* path)
{
    provider->reset();
    WebKit::EngineModuleUWP module;
    char message[256];

    if (module.bind(path, 0, 0) != WKUWPResultOK) {
        reportCheck("pinning/bound", false, "compatible negotiation failed, cannot continue");
        return;
    }
    snprintf(message, sizeof(message), "image loaded after bind=%d", isModuleLoaded(path) ? 1 : 0);
    reportCheck("pinning/bound", isModuleLoaded(path), message);

    module.closePage(0x1234);
    snprintf(message, sizeof(message), "image loaded after a rejected close=%d", isModuleLoaded(path) ? 1 : 0);
    reportCheck("pinning/after-failure", isModuleLoaded(path), message);

    module.beginShutdown();
    snprintf(message, sizeof(message), "image loaded after shutdown began=%d, still bound=%d",
        isModuleLoaded(path) ? 1 : 0, module.isBound() ? 1 : 0);
    reportCheck("pinning/after-shutdown", isModuleLoaded(path) && module.isBound(), message);
}

// A rejected binding on an image nobody else holds leaves that image unloaded.
//
// This image was built with a truncated reported extent baked in, so no setup
// call is needed: DLL globals do not survive an unload, which is why the
// truncation cannot be armed through setters on a freshly loaded copy. The
// harness itself never loads this path, so its load state afterwards says only
// what the module under test freed.
void checkRejectionUnloads(const wchar_t* path)
{
    WebKit::EngineModuleUWP module;

    WKUWPResult result = module.bind(path, 0, 0);
    char message[256];
    snprintf(message, sizeof(message), "rejected bind -> result=%d expected=%d, bound=%d, image loaded=%d",
        static_cast<int>(result), static_cast<int>(WKUWPResultAbiMismatch), module.isBound() ? 1 : 0,
        isModuleLoaded(path) ? 1 : 0);
    reportCheck("pinning/rejection-unloads",
        result == WKUWPResultAbiMismatch && !module.isBound() && !isModuleLoaded(path), message);
}

} // namespace

// Runs the ownership gate against the provider images named on the command line.
//
// The consumer under test is linked into this executable; every provider image
// is reached only through the loader, exactly as the consumer reaches them. The
// lifecycle and rejection images must be file copies this process never loads
// itself, or their load state would say nothing about what the consumer freed.
int main(int argc, char** argv)
{
    // Unbuffered output keeps every printed row even if a later check faults,
    // so a crash still identifies the row that caused it.
    setvbuf(stdout, nullptr, _IONBF, 0);

    if (argc < 4) {
        printf("usage: EngineModuleUWPTests <provider-module> <lifecycle-module> <rejection-module>\n");
        return 2;
    }

    // argv as UTF-16 for the loader; the-life-cycle and rejection images must be
    // file copies the harness itself never loads, or their load state would say
    // nothing about what the module under test freed.
    wchar_t providerPath[MAX_PATH];
    wchar_t lifecyclePath[MAX_PATH];
    wchar_t rejectionPath[MAX_PATH];
    if (!MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, providerPath, MAX_PATH)
        || !MultiByteToWideChar(CP_UTF8, 0, argv[2], -1, lifecyclePath, MAX_PATH)
        || !MultiByteToWideChar(CP_UTF8, 0, argv[3], -1, rejectionPath, MAX_PATH)) {
        printf("FAIL arguments: module paths do not fit in MAX_PATH\n");
        return 2;
    }

    TestProvider provider;
    if (!loadTestProvider(providerPath, &provider)) {
        printf("FAIL load: cannot load test provider %s (error %lu)\n", argv[1], GetLastError());
        return 1;
    }
    printf("PASS load: test provider resolved from %s\n\n", argv[1]);

    printf("compatible resolution\n");
    checkCompatibleResolution(&provider, providerPath);
    checkCapabilityGatedSuccess(&provider, providerPath);

    printf("\nincompatible identity\n");
    checkMissingCapability(&provider, providerPath);
    checkForeignMajor(&provider, providerPath);
    checkTruncatedTable(&provider, providerPath);
    checkNullSlot(&provider, providerPath);
    checkMissingExport();
    checkNullPath();

    printf("\nunbound operations\n");
    checkUnboundOperations();

    printf("\nper-page ownership\n");
    checkCloseRecreateBaseline(&provider, providerPath);
    checkUnknownHandles(&provider, providerPath);
    checkFailedCreation(&provider, providerPath);

    printf("\nshutdown\n");
    checkShutdownLatch(&provider, providerPath);

    printf("\nmodule pinning\n");
    checkPinningLifecycle(&provider, lifecyclePath);
    checkRejectionUnloads(rejectionPath);

    printf("\n");
    if (g_failures) {
        printf("FAILED: %d module ownership check(s) did not hold.\n", g_failures);
        return 1;
    }

    printf("All EngineModuleUWP ownership checks passed.\n");
    return 0;
}
