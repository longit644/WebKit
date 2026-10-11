/*
 * CallbackOwnershipTests.cpp - host-side verification of the task 3.4 consumer
 * ownership layer.
 *
 * This is the acceptance harness for M10 task 3.4. It links the consumer files
 * (Source/WebKit/UIProcess/API/w10m/native/CallbackContextW10M.cpp,
 * PlatformServiceW10M.cpp and EngineModuleW10M.cpp) directly into the executable,
 * because those files are the units under test, and drives them against the
 * configurable test-double provider built from
 * Tools/w10m/build/WKW10MModuleTestProvider.cpp. That provider is test tooling
 * only; nothing here proves anything about the real engine, whose table slots
 * still report Unsupported until tasks 4.x.
 *
 * What the harness proves, in acceptance order:
 *
 *   - Retained per-page callback context: the engine retains once on install,
 *     borrowed URI/title spans are copied before the provider scrambles its
 *     buffers, policy verdicts and handler failures travel without throwing,
 *     and null or malformed deliveries are refused or suppressed.
 *   - Detach acknowledgment with release after unwind: detach during dispatch
 *     answers Pending without releasing, the release runs exactly once after
 *     the unwind, storage is freed only then, and a second release or a later
 *     delivery is refused.
 *   - Snapshot-buffer retry: a short first read reports only its need, the
 *     retry grows exactly to that need and succeeds, an ever-growing provider
 *     is abandoned after a bound, and non-size failures never retry.
 *   - Exactly-once canceled platform completion: an accepted Write completes
 *     once with the cancellation result on cancel, a Read delivers its borrowed
 *     text once, rejections and repeats complete nothing, and a full table
 *     fails fast.
 *   - Non-throwing ABI behaviour: every consumer entry point answers hostile
 *     inputs with frozen result codes instead of faulting.
 *
 * The shipping consumer and engine are ARM32 and an ARM32 image cannot execute
 * on this x64 development host, so the behaviour runs from x64 copies of the
 * same sources while the ARM32 compile and link checks are done by
 * Tools/w10m/build/test-wkw10m-callback-ownership.ps1. Nothing here claims device
 * behaviour.
 */

#include "CallbackContextW10M.h"
#include "EngineModuleW10M.h"
#include "PlatformServiceW10M.h"

#include <windows.h>

#include <new>
#include <stdio.h>
#include <string.h>

namespace {

// Sentinel written into caller outputs to prove a rejected call never wrote them.
constexpr uint64_t kUntouchedHandle = 0xFEEDFACECAFEBEEFull;

// Cancel verdict the provider writes before a policy call; the consumer must
// leave a refused call's output exactly like this.
constexpr uint32_t kUntouchedCancel = 0xDEADBEEFu;

// Handler failure travelled through the policy entry point for the mapping test.
constexpr WKW10MResult kTestHandlerFailure = static_cast<WKW10MResult>(-2147467259);

// Expected cancellation result for a canceled clipboard operation: the platform
// HRESULT for ERROR_CANCELLED, computed the same way the service computes it.
WKW10MResult expectedCanceledResult()
{
    HRESULT cancelled = HRESULT_FROM_WIN32(ERROR_CANCELLED);
    return static_cast<WKW10MResult>(static_cast<int32_t>(cancelled));
}

// Setter and counter signatures resolved from the test provider by name.
typedef void(__stdcall* ResetFunction)();
typedef void(__stdcall* SetResult)(WKW10MResult);
typedef void(__stdcall* SetU32)(uint32_t);
typedef WKW10MResult(__stdcall* FireReleaseFunction)(WKW10MPageHandle);
typedef WKW10MResult(__stdcall* DispatchNotifyFunction)(WKW10MPageHandle, uint32_t, const char*, const char*);
typedef WKW10MResult(__stdcall* DispatchStartingFunction)(WKW10MPageHandle, const char*, uint32_t*);
typedef WKW10MResult(__stdcall* DispatchNullNotifyFunction)(WKW10MPageHandle);
typedef WKW10MResult(__stdcall* DispatchNullStartingFunction)(WKW10MPageHandle, uint32_t*);
typedef uint32_t(__stdcall* CounterFunction)();

// Live configuration handles for the test provider module.
struct OwnershipProvider {
    // Handle the harness itself holds; kept loaded for the whole run.
    HMODULE library;
    ResetFunction reset;
    SetResult setCreateResult;
    SetU32 setDispatching;
    FireReleaseFunction fireRelease;
    DispatchNotifyFunction dispatchNotify;
    DispatchStartingFunction dispatchStarting;
    DispatchNullNotifyFunction dispatchNullNotify;
    DispatchNullStartingFunction dispatchNullStarting;
    SetU32 setStateMode;
    CounterFunction stateCalls;
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

// Loads the provider and resolves every setter the ownership tests drive.
bool loadOwnershipProvider(const wchar_t* path, OwnershipProvider* provider)
{
    memset(provider, 0, sizeof(*provider));
    provider->library = LoadLibraryW(path);
    if (!provider->library)
        return false;
    return resolveSetter(provider->library, "WKW10MTestProviderReset",
               reinterpret_cast<void**>(&provider->reset))
        && resolveSetter(provider->library, "WKW10MTestProviderSetCreateResult",
            reinterpret_cast<void**>(&provider->setCreateResult))
        && resolveSetter(provider->library, "WKW10MTestProviderSetDispatching",
            reinterpret_cast<void**>(&provider->setDispatching))
        && resolveSetter(provider->library, "WKW10MTestProviderFireRelease",
            reinterpret_cast<void**>(&provider->fireRelease))
        && resolveSetter(provider->library, "WKW10MTestProviderDispatchNotify",
            reinterpret_cast<void**>(&provider->dispatchNotify))
        && resolveSetter(provider->library, "WKW10MTestProviderDispatchStarting",
            reinterpret_cast<void**>(&provider->dispatchStarting))
        && resolveSetter(provider->library, "WKW10MTestProviderDispatchNullNotify",
            reinterpret_cast<void**>(&provider->dispatchNullNotify))
        && resolveSetter(provider->library, "WKW10MTestProviderDispatchNullStarting",
            reinterpret_cast<void**>(&provider->dispatchNullStarting))
        && resolveSetter(provider->library, "WKW10MTestProviderSetStateMode",
            reinterpret_cast<void**>(&provider->setStateMode))
        && resolveSetter(provider->library, "WKW10MTestProviderStateCalls",
            reinterpret_cast<void**>(&provider->stateCalls));
}

// Destruction observations for one context or service under test.
struct DestroyedTracker {
    // Destructor-hook invocations observed; exactly one proves single freeing.
    uint32_t destroyedCount;
};

// Destruction hook shared by the context and service lifetime tests.
void __stdcall onTestObjectDestroyed(void* hookContext)
{
    ++static_cast<DestroyedTracker*>(hookContext)->destroyedCount;
}

// Reentrant detach state for the release-after-unwind test.
struct DetachHookState {
    // Module the hook detaches through, on the page under test.
    WebKit::EngineModuleW10M* module;
    WKW10MPageHandle page;
};

// Detach hook that detaches reentrantly from inside a notification dispatch.
WKW10MResult __stdcall detachDuringNotify(void* hookContext, WKW10MPageHandle)
{
    DetachHookState* state = static_cast<DetachHookState*>(hookContext);
    return state->module->detachPageCallbacks(state->page);
}

// Completion observations for the platform-service tests.
struct CompletionTracker {
    // Completion invocations observed; exactly-once means one per operation.
    uint32_t calls;

    // Last operation identifier completed.
    uint64_t lastId;

    // Last result delivered.
    WKW10MResult lastResult;

    // Copy of the last borrowed text, taken during the completion call.
    uint8_t text[128];

    // Length of the copied text in bytes.
    uint32_t textLength;
};

// Completion entry point that copies borrowed text before returning.
void __stdcall recordTestCompletion(
    void* completionContext, uint64_t operationId, WKW10MResult result, const WKW10MString* text)
{
    CompletionTracker* tracker = static_cast<CompletionTracker*>(completionContext);
    ++tracker->calls;
    tracker->lastId = operationId;
    tracker->lastResult = result;
    uint32_t length = (text && text->data) ? text->length : 0;
    if (length > sizeof(tracker->text))
        length = sizeof(tracker->text);
    if (length > 0)
        memcpy(tracker->text, text->data, length);
    tracker->textLength = length;
}

// Valid creation options accepted by the working test slot.
void writeTestPageOptions(WKW10MPageOptions* options)
{
    memset(options, 0, sizeof(*options));
    options->header.structureSize = sizeof(*options);
    options->header.structureVersion = WKW10M_STRUCTURE_VERSION;
}

// Binds a module with a working createPage slot and opens one page for tests.
bool openTestPage(OwnershipProvider* provider, const wchar_t* path,
    WebKit::EngineModuleW10M* module, WKW10MPageHandle* page)
{
    provider->reset();
    provider->setCreateResult(WKW10MResultOK);
    if (module->bind(path, 0, 0) != WKW10MResultOK)
        return false;
    WKW10MPageOptions options;
    writeTestPageOptions(&options);
    return module->createPage(&options, page) == WKW10MResultOK && *page != 0;
}

// Attaching installs the table and retains the context exactly once.
void checkAttachFlow(OwnershipProvider* provider, const wchar_t* path)
{
    WebKit::EngineModuleW10M module;
    WKW10MPageHandle page = kUntouchedHandle;
    if (!openTestPage(provider, path, &module, &page)) {
        reportCheck("attach/install", false, "cannot open a test page");
        return;
    }

    WebKit::PageCallbackContextW10M* context = new (std::nothrow) WebKit::PageCallbackContextW10M();
    if (!context) {
        reportCheck("attach/install", false, "cannot allocate the test context");
        module.closePage(page);
        return;
    }
    WKW10MPageCallbacks table;
    char message[256];
    if (context->buildTable(&table) != WKW10MResultOK) {
        reportCheck("attach/install", false, "cannot build the callback table");
        context->release();
        module.closePage(page);
        return;
    }

    WKW10MResult result = module.setPageCallbacks(page, &table);
    snprintf(message, sizeof(message), "setPageCallbacks -> result=%d expected=0, refcount=%u, retains=%u",
        static_cast<int>(result), context->refCount(), context->retainCalls());
    reportCheck("attach/install",
        result == WKW10MResultOK && context->refCount() == 2 && context->retainCalls() == 1, message);

    result = module.detachPageCallbacks(page);
    const bool detached = result == WKW10MResultOK && context->releaseCalls() == 1;
    snprintf(message, sizeof(message), "detach outside dispatch -> result=%d expected=0, releases=%u",
        static_cast<int>(result), context->releaseCalls());
    reportCheck("attach/detach-immediate", detached, message);

    context->release();
    snprintf(message, sizeof(message), "setup release leaves open pages=%u", module.openPageCount());
    reportCheck("attach/setup-release", module.openPageCount() == 1, message);
    module.closePage(page);
}

// Borrowed spans are copied before the provider scrambles its buffers.
void checkBorrowedPayloadCopy(OwnershipProvider* provider, const wchar_t* path)
{
    WebKit::EngineModuleW10M module;
    WKW10MPageHandle page = kUntouchedHandle;
    if (!openTestPage(provider, path, &module, &page)) {
        reportCheck("payload/copy", false, "cannot open a test page");
        return;
    }
    WebKit::PageCallbackContextW10M* context = new (std::nothrow) WebKit::PageCallbackContextW10M();
    WKW10MPageCallbacks table;
    context->buildTable(&table);
    module.setPageCallbacks(page, &table);

    const char* uri = "https://example.test/a";
    const char* title = "Title A";
    WKW10MResult result = provider->dispatchNotify(page, WKW10MEventSourceChanged, uri, title);

    char message[256];
    const bool uriCopied = context->lastUriLength() == strlen(uri)
        && memcmp(context->lastUriBytes(), uri, strlen(uri)) == 0;
    const bool titleCopied = context->lastTitleLength() == strlen(title)
        && memcmp(context->lastTitleBytes(), title, strlen(title)) == 0;
    snprintf(message, sizeof(message), "notify -> result=%d, uri copied=%d, title copied=%d, kind=%u, sequence=%llu",
        static_cast<int>(result), uriCopied ? 1 : 0, titleCopied ? 1 : 0, context->lastEventKind(),
        context->lastSequence());
    reportCheck("payload/copy",
        result == WKW10MResultOK && uriCopied && titleCopied
            && context->lastEventKind() == WKW10MEventSourceChanged && context->lastSequence() == 1
            && context->notifyCalls() == 1,
        message);

    module.detachPageCallbacks(page);
    context->release();
    module.closePage(page);
}

// Policy verdicts and handler failures travel through starting without throwing.
void checkStartingPolicy(OwnershipProvider* provider, const wchar_t* path)
{
    WebKit::EngineModuleW10M module;
    WKW10MPageHandle page = kUntouchedHandle;
    if (!openTestPage(provider, path, &module, &page)) {
        reportCheck("policy/refuse", false, "cannot open a test page");
        return;
    }
    WebKit::PageCallbackContextW10M* context = new (std::nothrow) WebKit::PageCallbackContextW10M();
    WKW10MPageCallbacks table;
    context->buildTable(&table);
    module.setPageCallbacks(page, &table);

    char message[256];
    uint32_t cancel = kUntouchedCancel;
    WKW10MResult result = provider->dispatchStarting(page, "https://example.test/", &cancel);
    snprintf(message, sizeof(message), "starting -> result=%d expected=0, cancel=%u expected=0",
        static_cast<int>(result), cancel);
    reportCheck("policy/refuse", result == WKW10MResultOK && cancel == 0 && context->startingCalls() == 1, message);

    context->setCancelVerdict(true);
    result = provider->dispatchStarting(page, "https://example.test/", &cancel);
    snprintf(message, sizeof(message), "starting -> result=%d expected=0, cancel=%u expected=1",
        static_cast<int>(result), cancel);
    reportCheck("policy/allow", result == WKW10MResultOK && cancel == 1, message);

    context->setFailResult(kTestHandlerFailure);
    result = provider->dispatchStarting(page, "https://example.test/", &cancel);
    snprintf(message, sizeof(message), "failing handler -> result=%d expected=%d", static_cast<int>(result),
        static_cast<int>(kTestHandlerFailure));
    reportCheck("policy/handler-failure", result == kTestHandlerFailure, message);
    context->setFailResult(WKW10MResultOK);

    module.detachPageCallbacks(page);
    context->release();
    module.closePage(page);
}

// Null and malformed deliveries are refused or suppressed without faulting.
void checkNullGuards(OwnershipProvider* provider, const wchar_t* path)
{
    WebKit::EngineModuleW10M module;
    WKW10MPageHandle page = kUntouchedHandle;
    if (!openTestPage(provider, path, &module, &page)) {
        reportCheck("guards/null-starting", false, "cannot open a test page");
        return;
    }
    WebKit::PageCallbackContextW10M* context = new (std::nothrow) WebKit::PageCallbackContextW10M();
    WKW10MPageCallbacks table;
    context->buildTable(&table);
    module.setPageCallbacks(page, &table);

    char message[256];
    uint32_t cancel = kUntouchedCancel;
    WKW10MResult result = table.starting(context, nullptr, &cancel);
    snprintf(message, sizeof(message), "null starting -> result=%d expected=%d, cancel untouched=%d",
        static_cast<int>(result), static_cast<int>(WKW10MResultInvalidArgument),
        cancel == kUntouchedCancel ? 1 : 0);
    reportCheck("guards/null-starting",
        result == WKW10MResultInvalidArgument && cancel == kUntouchedCancel, message);

    table.notify(context, nullptr);
    snprintf(message, sizeof(message), "null notify suppressed=%u expected=1", context->suppressedCalls());
    reportCheck("guards/null-notify", context->suppressedCalls() == 1, message);

    result = provider->dispatchNullNotify(page);
    snprintf(message, sizeof(message), "dispatched null notify -> result=%d, suppressed=%u",
        static_cast<int>(result), context->suppressedCalls());
    reportCheck("guards/dispatched-null-notify",
        result == WKW10MResultOK && context->suppressedCalls() == 2, message);

    cancel = kUntouchedCancel;
    result = provider->dispatchNullStarting(page, &cancel);
    snprintf(message, sizeof(message), "dispatched null starting -> result=%d expected=%d, cancel untouched=%d",
        static_cast<int>(result), static_cast<int>(WKW10MResultInvalidArgument),
        cancel == kUntouchedCancel ? 1 : 0);
    reportCheck("guards/dispatched-null-starting",
        result == WKW10MResultInvalidArgument && cancel == kUntouchedCancel, message);

    module.detachPageCallbacks(page);
    context->release();
    module.closePage(page);
}

// Detach during dispatch answers Pending; the release frees storage on unwind.
void checkReleaseAfterUnwind(OwnershipProvider* provider, const wchar_t* path)
{
    WebKit::EngineModuleW10M module;
    WKW10MPageHandle page = kUntouchedHandle;
    if (!openTestPage(provider, path, &module, &page)) {
        reportCheck("detach/pending", false, "cannot open a test page");
        return;
    }
    DestroyedTracker tracker;
    memset(&tracker, 0, sizeof(tracker));
    DetachHookState hookState;
    hookState.module = &module;
    hookState.page = page;
    WebKit::PageCallbackContextW10M* context = new (std::nothrow) WebKit::PageCallbackContextW10M();
    context->setDestroyedHook(onTestObjectDestroyed, &tracker);
    context->setDetachHook(detachDuringNotify, &hookState);
    WKW10MPageCallbacks table;
    context->buildTable(&table);
    module.setPageCallbacks(page, &table);

    char message[256];
    provider->setDispatching(1);
    WKW10MResult result = provider->dispatchNotify(page, WKW10MEventTitleChanged, "https://example.test/", "T");
    snprintf(message, sizeof(message),
        "detach in dispatch -> dispatch=%d, detach fired=%d, detach answer=%d expected=%d, releases=%u, destroyed=%u",
        static_cast<int>(result), context->detachFired() ? 1 : 0, static_cast<int>(context->lastDetachResult()),
        static_cast<int>(WKW10MResultPending), context->releaseCalls(), tracker.destroyedCount);
    reportCheck("detach/pending",
        result == WKW10MResultOK && context->detachFired()
            && context->lastDetachResult() == WKW10MResultPending && context->releaseCalls() == 0
            && tracker.destroyedCount == 0,
        message);

    provider->setDispatching(0);
    result = provider->fireRelease(page);
    snprintf(message, sizeof(message),
        "unwind release -> result=%d expected=0, releases=%u expected=1, destroyed=%u expected=0",
        static_cast<int>(result), context->releaseCalls(), tracker.destroyedCount);
    // The release acknowledged the detach, but the harness still holds its
    // setup reference, so the object survives; only the setup release below
    // frees it, which is what proves storage outlived the detach call.
    reportCheck("detach/release-after-unwind",
        result == WKW10MResultOK && context->releaseCalls() == 1 && tracker.destroyedCount == 0, message);

    context->release();
    snprintf(message, sizeof(message), "setup release -> destroyed=%u expected=1", tracker.destroyedCount);
    reportCheck("detach/storage-freed", tracker.destroyedCount == 1, message);

    result = provider->fireRelease(page);
    snprintf(message, sizeof(message), "second release -> result=%d expected=%d", static_cast<int>(result),
        static_cast<int>(WKW10MResultInvalidState));
    reportCheck("detach/no-double-release", result == WKW10MResultInvalidState, message);

    result = provider->dispatchNotify(page, WKW10MEventTitleChanged, "https://example.test/", "T");
    snprintf(message, sizeof(message), "delivery after detach -> result=%d expected=%d", static_cast<int>(result),
        static_cast<int>(WKW10MResultInvalidState));
    reportCheck("detach/suppressed", result == WKW10MResultInvalidState, message);

    result = module.closePage(page);
    snprintf(message, sizeof(message), "close -> result=%d expected=0, open=%u", static_cast<int>(result),
        module.openPageCount());
    reportCheck("detach/close-baseline", result == WKW10MResultOK && module.openPageCount() == 0, message);
}

// Replacing an active attachment is rejected; re-attaching after ack succeeds.
void checkReplaceRejected(OwnershipProvider* provider, const wchar_t* path)
{
    WebKit::EngineModuleW10M module;
    WKW10MPageHandle page = kUntouchedHandle;
    if (!openTestPage(provider, path, &module, &page)) {
        reportCheck("attach/replace-rejected", false, "cannot open a test page");
        return;
    }
    WebKit::PageCallbackContextW10M* first = new (std::nothrow) WebKit::PageCallbackContextW10M();
    WebKit::PageCallbackContextW10M* second = new (std::nothrow) WebKit::PageCallbackContextW10M();
    WKW10MPageCallbacks firstTable;
    WKW10MPageCallbacks secondTable;
    first->buildTable(&firstTable);
    second->buildTable(&secondTable);
    module.setPageCallbacks(page, &firstTable);

    char message[256];
    WKW10MResult result = module.setPageCallbacks(page, &secondTable);
    snprintf(message, sizeof(message), "replacing active attachment -> result=%d expected=%d, second refcount=%u",
        static_cast<int>(result), static_cast<int>(WKW10MResultInvalidState), second->refCount());
    reportCheck("attach/replace-rejected",
        result == WKW10MResultInvalidState && second->refCount() == 1 && second->retainCalls() == 0, message);

    module.detachPageCallbacks(page);
    result = module.setPageCallbacks(page, &secondTable);
    snprintf(message, sizeof(message), "re-attaching after ack -> result=%d expected=0, second refcount=%u",
        static_cast<int>(result), second->refCount());
    reportCheck("attach/reattach-after-ack", result == WKW10MResultOK && second->refCount() == 2, message);

    first->release();
    module.detachPageCallbacks(page);
    second->release();
    module.closePage(page);
}

// A short first read reports only its need; the retry grows exactly and wins.
void checkSnapshotRetry(OwnershipProvider* provider, const wchar_t* path)
{
    WebKit::EngineModuleW10M module;
    WKW10MPageHandle page = kUntouchedHandle;
    if (!openTestPage(provider, path, &module, &page)) {
        reportCheck("snapshot/retry", false, "cannot open a test page");
        return;
    }
    provider->setStateMode(1);

    char message[256];
    WebKit::PageStateSnapshotW10M snapshot;
    WKW10MResult result = snapshot.fetch(module.api(), page);
    snprintf(message, sizeof(message), "fetch -> result=%d expected=0, attempts=%u expected=2, pageId=%llu",
        static_cast<int>(result), snapshot.attempts(), snapshot.state()->pageId);
    reportCheck("snapshot/retry",
        result == WKW10MResultOK && snapshot.attempts() == 2 && snapshot.state()->pageId == page
            && snapshot.storageBytes() == 64,
        message);

    WKW10MPageState probed;
    memset(&probed, 0xA5, sizeof(probed));
    uint8_t storage[8];
    memset(storage, 0xA5, sizeof(storage));
    uint32_t required = 0;
    result = module.api()->getState(page, &probed, storage, sizeof(storage), &required);
    bool untouched = required == 64;
    for (size_t index = 0; untouched && index < sizeof(probed); ++index)
        untouched = reinterpret_cast<const uint8_t*>(&probed)[index] == 0xA5;
    for (size_t index = 0; untouched && index < sizeof(storage); ++index)
        untouched = storage[index] == 0xA5;
    snprintf(message, sizeof(message), "short read -> result=%d expected=%d, need=%u, rest untouched=%d",
        static_cast<int>(result), static_cast<int>(WKW10MResultBufferTooSmall), required, untouched ? 1 : 0);
    reportCheck("snapshot/need-only",
        result == WKW10MResultBufferTooSmall && required == 64 && untouched, message);

    provider->setStateMode(2);
    WebKit::PageStateSnapshotW10M endless;
    result = endless.fetch(module.api(), page);
    snprintf(message, sizeof(message), "growing provider -> result=%d expected=%d, attempts=%u expected=%u",
        static_cast<int>(result), static_cast<int>(WKW10MResultBufferTooSmall), endless.attempts(),
        WebKit::PageStateSnapshotW10M::kMaxFetchAttempts);
    reportCheck("snapshot/bounded",
        result == WKW10MResultBufferTooSmall
            && endless.attempts() == WebKit::PageStateSnapshotW10M::kMaxFetchAttempts,
        message);

    provider->setStateMode(0);
    WebKit::PageStateSnapshotW10M refused;
    result = refused.fetch(module.api(), page);
    snprintf(message, sizeof(message), "refused read -> result=%d expected=%d, attempts=%u expected=1",
        static_cast<int>(result), static_cast<int>(WKW10MResultUnsupported), refused.attempts());
    reportCheck("snapshot/no-retry",
        result == WKW10MResultUnsupported && refused.attempts() == 1, message);

    module.closePage(page);
}

// An accepted Write completes once with cancellation on cancel, never twice.
void checkServiceWriteCancel()
{
    WebKit::PlatformServiceContextW10M* service = new (std::nothrow) WebKit::PlatformServiceContextW10M();
    WKW10MPlatformServices table;
    service->buildTable(&table);

    CompletionTracker tracker;
    memset(&tracker, 0, sizeof(tracker));
    char message[256];
    uint8_t source[] = "hello";
    WKW10MString text;
    text.data = source;
    text.length = static_cast<uint32_t>(sizeof(source) - 1);

    WKW10MResult result = table.requestClipboard(service, 7, 1, WKW10MClipboardWrite, &text, recordTestCompletion, &tracker);
    snprintf(message, sizeof(message), "write request -> result=%d expected=0, pending=%u", static_cast<int>(result),
        service->pendingCount());
    reportCheck("service/write-accept",
        result == WKW10MResultOK && service->pendingCount() == 1 && tracker.calls == 0, message);

    memset(source, 0xCD, sizeof(source));
    uint8_t copied[16];
    uint32_t needed = 0;
    const bool copiedOut = service->copyOutText(1, copied, sizeof(copied), &needed);
    snprintf(message, sizeof(message), "write copy survives source=%d, need=%u", copiedOut ? 1 : 0, needed);
    reportCheck("service/write-copy",
        copiedOut && needed == 5 && memcmp(copied, "hello", 5) == 0, message);

    result = table.cancelClipboard(service, 1);
    snprintf(message, sizeof(message),
        "cancel -> result=%d expected=0, completions=%u expected=1, answer=%d expected=%d, pending=%u",
        static_cast<int>(result), tracker.calls, static_cast<int>(tracker.lastResult),
        static_cast<int>(expectedCanceledResult()), service->pendingCount());
    reportCheck("service/cancel-once",
        result == WKW10MResultOK && tracker.calls == 1 && tracker.lastResult == expectedCanceledResult()
            && tracker.lastId == 1 && service->pendingCount() == 0,
        message);

    result = table.cancelClipboard(service, 1);
    snprintf(message, sizeof(message), "second cancel -> result=%d expected=%d, completions=%u",
        static_cast<int>(result), static_cast<int>(WKW10MResultInvalidArgument), tracker.calls);
    reportCheck("service/cancel-twice",
        result == WKW10MResultInvalidArgument && tracker.calls == 1, message);

    result = service->deliverCompletion(1, WKW10MResultOK, nullptr);
    snprintf(message, sizeof(message), "delivery after cancel -> result=%d expected=%d, completions=%u",
        static_cast<int>(result), static_cast<int>(WKW10MResultInvalidArgument), tracker.calls);
    reportCheck("service/no-second-delivery",
        result == WKW10MResultInvalidArgument && tracker.calls == 1, message);

    service->release();
}

// A Read delivers its borrowed text once; repeats are refused.
void checkServiceReadDeliver()
{
    WebKit::PlatformServiceContextW10M* service = new (std::nothrow) WebKit::PlatformServiceContextW10M();
    WKW10MPlatformServices table;
    service->buildTable(&table);

    CompletionTracker tracker;
    memset(&tracker, 0, sizeof(tracker));
    char message[256];
    WKW10MResult result = table.requestClipboard(service, 7, 2, WKW10MClipboardRead, nullptr, recordTestCompletion, &tracker);
    snprintf(message, sizeof(message), "read request -> result=%d expected=0, pending=%u", static_cast<int>(result),
        service->pendingCount());
    reportCheck("service/read-accept", result == WKW10MResultOK && service->pendingCount() == 1, message);

    uint8_t source[] = "data";
    WKW10MString text;
    text.data = source;
    text.length = static_cast<uint32_t>(sizeof(source) - 1);
    result = service->deliverCompletion(2, WKW10MResultOK, &text);
    memset(source, 0xCD, sizeof(source));
    snprintf(message, sizeof(message),
        "read delivery -> result=%d expected=0, completions=%u, text copied=%d, pending=%u", static_cast<int>(result),
        tracker.calls, tracker.textLength == 4 && memcmp(tracker.text, "data", 4) == 0 ? 1 : 0,
        service->pendingCount());
    reportCheck("service/read-deliver",
        result == WKW10MResultOK && tracker.calls == 1 && tracker.textLength == 4
            && memcmp(tracker.text, "data", 4) == 0 && service->pendingCount() == 0,
        message);

    result = service->deliverCompletion(2, WKW10MResultOK, &text);
    snprintf(message, sizeof(message), "second delivery -> result=%d expected=%d, completions=%u",
        static_cast<int>(result), static_cast<int>(WKW10MResultInvalidArgument), tracker.calls);
    reportCheck("service/read-once",
        result == WKW10MResultInvalidArgument && tracker.calls == 1, message);

    service->release();
}

// Rejected requests complete nothing; a full table fails fast with cleanup.
void checkServiceRejections()
{
    WebKit::PlatformServiceContextW10M* service = new (std::nothrow) WebKit::PlatformServiceContextW10M();
    WKW10MPlatformServices table;
    service->buildTable(&table);

    CompletionTracker tracker;
    memset(&tracker, 0, sizeof(tracker));
    char message[256];
    WKW10MString empty;
    empty.data = nullptr;
    empty.length = 0;

    const bool badKind = table.requestClipboard(service, 7, 3, 7, &empty, recordTestCompletion, &tracker)
        == WKW10MResultInvalidArgument;
    const bool nullCompletion = table.requestClipboard(service, 7, 3, WKW10MClipboardRead, nullptr, nullptr, &tracker)
        == WKW10MResultInvalidArgument;
    uint8_t byte = 0;
    WKW10MString dangling;
    dangling.data = nullptr;
    dangling.length = 1;
    const bool nullText = table.requestClipboard(service, 7, 3, WKW10MClipboardWrite, &dangling, recordTestCompletion, &tracker)
        == WKW10MResultInvalidArgument;
    (void)byte;
    WKW10MString content;
    content.data = &byte;
    content.length = 1;
    const bool readWithText = table.requestClipboard(service, 7, 3, WKW10MClipboardRead, &content, recordTestCompletion, &tracker)
        == WKW10MResultInvalidArgument;
    snprintf(message, sizeof(message), "bad kind/null completion/null text/read text rejected=%d%d%d%d, calls=%u",
        badKind ? 1 : 0, nullCompletion ? 1 : 0, nullText ? 1 : 0, readWithText ? 1 : 0, tracker.calls);
    reportCheck("service/rejections",
        badKind && nullCompletion && nullText && readWithText && tracker.calls == 0
            && service->pendingCount() == 0,
        message);

    for (uint64_t id = 100; id < 100 + WebKit::PlatformServiceContextW10M::kMaxPendingOperations; ++id) {
        if (table.requestClipboard(service, 7, id, WKW10MClipboardRead, nullptr, recordTestCompletion, &tracker)
            != WKW10MResultOK) {
            reportCheck("service/table-full", false, "filling the operation table failed early");
            service->release();
            return;
        }
    }
    WKW10MResult result = table.requestClipboard(service, 7, 200, WKW10MClipboardRead, nullptr, recordTestCompletion, &tracker);
    snprintf(message, sizeof(message), "seventeenth request -> result=%d expected=%d, pending=%u",
        static_cast<int>(result), static_cast<int>(WKW10MResultBudgetExceeded), service->pendingCount());
    reportCheck("service/table-full",
        result == WKW10MResultBudgetExceeded
            && service->pendingCount() == WebKit::PlatformServiceContextW10M::kMaxPendingOperations,
        message);

    for (uint64_t id = 100; id < 100 + WebKit::PlatformServiceContextW10M::kMaxPendingOperations; ++id)
        table.cancelClipboard(service, id);
    snprintf(message, sizeof(message), "after draining: completions=%u expected=%u, pending=%u", tracker.calls,
        WebKit::PlatformServiceContextW10M::kMaxPendingOperations, service->pendingCount());
    reportCheck("service/drain-once",
        tracker.calls == WebKit::PlatformServiceContextW10M::kMaxPendingOperations && service->pendingCount() == 0,
        message);

    service->release();
}

// Retain/release pairing and the wake entry point follow the ownership protocol.
void checkServiceLifetime()
{
    DestroyedTracker tracker;
    memset(&tracker, 0, sizeof(tracker));
    WebKit::PlatformServiceContextW10M* service = new (std::nothrow) WebKit::PlatformServiceContextW10M();
    service->setDestroyedHook(onTestObjectDestroyed, &tracker);
    WKW10MPlatformServices table;
    service->buildTable(&table);

    char message[256];
    table.retainContext(service);
    snprintf(message, sizeof(message), "engine retain -> refcount=%u expected=2, retains=%u",
        service->refCount(), service->retainCalls());
    reportCheck("service/retain", service->refCount() == 2 && service->retainCalls() == 1, message);

    WKW10MResult result = table.postWake(service);
    snprintf(message, sizeof(message), "postWake -> result=%d expected=0, wakes=%u", static_cast<int>(result),
        service->wakeCalls());
    reportCheck("service/wake", result == WKW10MResultOK && service->wakeCalls() == 1, message);

    table.releaseContext(service);
    snprintf(message, sizeof(message), "engine release -> refcount=%u expected=1, releases=%u, destroyed=%u",
        service->refCount(), service->releaseCalls(), tracker.destroyedCount);
    reportCheck("service/engine-release",
        service->refCount() == 1 && service->releaseCalls() == 1 && tracker.destroyedCount == 0, message);

    table.releaseContext(service);
    snprintf(message, sizeof(message), "setup release -> destroyed=%u expected=1, releases=%u",
        tracker.destroyedCount, 2u);
    // The service deleted itself in its release, so nothing about it is read
    // after this point; the tracker outlives it and proves single freeing.
    reportCheck("service/setup-release", tracker.destroyedCount == 1, message);
}

// Null contexts and unknown operations are refused without faulting.
void checkServiceNullGuards()
{
    char message[256];
    const bool requestGuarded = WebKit::PlatformServiceContextW10M::requestClipboardThunk(nullptr, 7, 1, 0, nullptr, nullptr, nullptr)
        == WKW10MResultInvalidArgument;
    const bool cancelGuarded = WebKit::PlatformServiceContextW10M::cancelClipboardThunk(nullptr, 1) == WKW10MResultInvalidArgument;
    const bool wakeGuarded = WebKit::PlatformServiceContextW10M::postWakeThunk(nullptr) == WKW10MResultInvalidArgument;
    snprintf(message, sizeof(message), "null request/cancel/wake refused=%d%d%d", requestGuarded ? 1 : 0,
        cancelGuarded ? 1 : 0, wakeGuarded ? 1 : 0);
    reportCheck("service/null-guards", requestGuarded && cancelGuarded && wakeGuarded, message);

    WebKit::PlatformServiceContextW10M* service = new (std::nothrow) WebKit::PlatformServiceContextW10M();
    const bool unknownDelivery = service->deliverCompletion(0x9999, WKW10MResultOK, nullptr) == WKW10MResultInvalidArgument;
    uint32_t needed = 0;
    const bool unknownCopy = !service->copyOutText(0x9999, nullptr, 0, &needed);
    snprintf(message, sizeof(message), "unknown delivery/copy refused=%d%d", unknownDelivery ? 1 : 0,
        unknownCopy ? 1 : 0);
    reportCheck("service/unknown-operations", unknownDelivery && unknownCopy, message);
    service->release();
}

} // namespace

// Runs the ownership gate against the provider image named on the command line.
//
// The consumers under test are linked into this executable; the provider image
// is reached only through the loader, exactly as the consumers reach it.
int main(int argc, char** argv)
{
    // Unbuffered output keeps every printed row even if a later check faults,
    // so a crash still identifies the row that caused it.
    setvbuf(stdout, nullptr, _IONBF, 0);

    if (argc < 2) {
        printf("usage: CallbackOwnershipTests <provider-module>\n");
        return 2;
    }

    wchar_t providerPath[MAX_PATH];
    if (!MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, providerPath, MAX_PATH)) {
        printf("FAIL arguments: provider module path does not fit in MAX_PATH\n");
        return 2;
    }

    OwnershipProvider provider;
    if (!loadOwnershipProvider(providerPath, &provider)) {
        printf("FAIL load: cannot load test provider %s (error %lu)\n", argv[1], GetLastError());
        return 1;
    }
    printf("PASS load: test provider resolved from %s\n\n", argv[1]);

    printf("callback context\n");
    checkAttachFlow(&provider, providerPath);
    checkBorrowedPayloadCopy(&provider, providerPath);
    checkStartingPolicy(&provider, providerPath);
    checkNullGuards(&provider, providerPath);

    printf("\ndetach acknowledgment\n");
    checkReleaseAfterUnwind(&provider, providerPath);
    checkReplaceRejected(&provider, providerPath);

    printf("\nstate snapshot\n");
    checkSnapshotRetry(&provider, providerPath);

    printf("\nplatform service\n");
    checkServiceWriteCancel();
    checkServiceReadDeliver();
    checkServiceRejections();
    checkServiceLifetime();
    checkServiceNullGuards();

    printf("\n");
    if (g_failures) {
        printf("FAILED: %d ownership check(s) did not hold.\n", g_failures);
        return 1;
    }

    printf("All callback ownership checks passed.\n");
    return 0;
}
