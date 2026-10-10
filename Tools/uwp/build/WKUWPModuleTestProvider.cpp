/*
 * WKUWPModuleTestProvider.cpp - configurable test double for the V1 engine
 * provider, used only by the task 3.3 EngineModuleUWP harness, the task 3.4
 * callback-ownership harness and the task 3.5 bridge-ordering harness.
 *
 * This file is test tooling under Tools/uwp/build. It is never shipped, never
 * linked into the engine, the component or an app, and it proves nothing about
 * the real provider in Source/WebCore/platform/uwp/WKUWPEngineAPI.cpp. Its only
 * purpose is to let the consumer-side tests drive EngineModuleUWP and the task
 * 3.4 contexts through compatible resolution, truncated tables, null slots,
 * foreign identities, missing capabilities, working page slots, callback
 * attach/detach with deferred release, event dispatch with scrambled buffers,
 * and size-probing state reads, without waiting for tasks 4.x to implement
 * those behaviours for real.
 *
 * Behaviour is configured at run time through the exported setters below, so
 * one module image covers every identity the tests need. All configuration and
 * counters are file-static and single-threaded; the harnesses run on one thread.
 */

#include "WKUWPEngine.h"
#include "WKUWPNavigation.h"
#include "WKUWPPage.h"

#include <windows.h>

#include <string.h>

namespace {

// Test build identity; distinct from any real engine build on purpose.
const char kTestBuildId[] = "wkuwp-test-provider abi=1.0";

// Defensive write bound, mirroring the real provider's rule.
constexpr uint32_t kTestMaxDeclaredSize = 4096;

// First handle handed out by the working createPage slot; zero is never valid.
constexpr uint64_t kTestFirstHandle = 1;

// Slot numbers for WKUWPTestProviderSetNullSlot, in table declaration order.
constexpr uint32_t kTestNullSlotNone = 0;

// Default reported-extent override baked in at compile time.
//
// The rejection lifecycle image is built with this set, so its truncation is
// part of the image rather than run-time configuration: DLL globals do not
// survive an unload, so arming a fresh image through setters and then freeing
// the setup handle would lose the setting before the test even loads it.
#ifndef WKUWP_TEST_PROVIDER_DEFAULT_TRUNCATE
#define WKUWP_TEST_PROVIDER_DEFAULT_TRUNCATE 0
#endif

// Configurable provider behaviour, set by the exported setters.
uint64_t g_capabilities = 0;
uint32_t g_abiMajor = WKUWP_ABI_MAJOR_VERSION;
uint32_t g_abiMinor = WKUWP_ABI_MINOR_VERSION;
uint32_t g_truncateBytes = WKUWP_TEST_PROVIDER_DEFAULT_TRUNCATE;
uint32_t g_nullSlotIndex = kTestNullSlotNone;
WKUWPResult g_createResult = WKUWPResultUnsupported;
WKUWPResult g_closeResult = WKUWPResultOK;
WKUWPResult g_shutdownResult = WKUWPResultOK;
WKUWPResult g_pollResult = WKUWPResultOK;

// Call counters the harness reads back through the exported getters.
uint32_t g_discoveryCalls = 0;
uint32_t g_createCalls = 0;
uint32_t g_closeCalls = 0;
uint32_t g_beginShutdownCalls = 0;
uint32_t g_pollShutdownCalls = 0;

// Next handle for the working createPage slot; never wraps in a test run.
uint64_t g_nextHandle = kTestFirstHandle;

// Maximum pages the test double tracks; test runs stay far below it.
constexpr uint32_t kTestMaxPages = 64;

// Maximum callback attachments; one active attachment per page.
constexpr uint32_t kTestMaxAttachments = 16;

// Dispatch span buffer per text field; longer inputs truncate, never overflow.
constexpr uint32_t kTestEventTextBytes = 256;

// Storage need reported by the size-probing getState mode.
constexpr uint32_t kTestStateNeedBytes = 64;

// getState behaviour mode: 0 answers Unsupported exactly like task 3.3, 1 probes
// then succeeds, 2 grows its answer forever so a retry bound can be proven.
uint32_t g_stateMode = 0;

// getState calls since the last reset, across all modes.
uint32_t g_stateCalls = 0;

// Simulated dispatch state: nonzero while a dispatch is logically active.
uint32_t g_dispatching = 0;

// Owner-thread enforcement: zero disables it; nonzero means only the thread
// recorded when enforcement was enabled may call the working slots.
uint32_t g_enforceThread = 0;
DWORD g_ownerThreadId = 0;

// True when the calling thread may use the engine slots under enforcement.
bool onOwnerThread()
{
    return !g_enforceThread || GetCurrentThreadId() == g_ownerThreadId;
}

// Event sequence handed out to dispatches; strictly increasing per process.
uint64_t g_sequence = 0;

// Provider-owned dispatch buffers, scrambled after each delivery.
uint8_t g_eventUri[kTestEventTextBytes];
uint8_t g_eventTitle[kTestEventTextBytes];

// Handles handed out by the working createPage slot; closePage never forgets
// them, which is a documented test-double simplification: the real engine would
// invalidate closed handles, but no 3.4 test closes-then-reattaches.
uint64_t g_createdPages[kTestMaxPages];
uint32_t g_createdCount = 0;

// One simulated callback attachment with its copied table and ack state.
struct TestAttachment {
    // Whether this slot was ever attached for its page.
    bool used;

    // Page the attachment belongs to; never a process-global route.
    uint64_t page;

    // False the moment detach runs, even while the release is deferred.
    bool active;

    // True once releaseContext has run for the current attachment.
    bool acked;

    // Engine-side copy of the consumer's table.
    WKUWPPageCallbacks table;
};

// Simulated attachment slots; zero-initialized, cleared by Reset.
TestAttachment g_attachments[kTestMaxAttachments];

// True when the test double has handed out this handle from createPage.
bool pageKnown(WKUWPPageHandle page)
{
    for (uint32_t index = 0; index < g_createdCount; ++index) {
        if (g_createdPages[index] == page)
            return true;
    }
    return false;
}

// Records a created handle for later attach and state validation.
void recordCreated(WKUWPPageHandle page)
{
    if (g_createdCount < kTestMaxPages)
        g_createdPages[g_createdCount++] = page;
}

// Attachment slot for this page, or kTestMaxAttachments when never attached.
uint32_t findAttachment(WKUWPPageHandle page)
{
    for (uint32_t index = 0; index < kTestMaxAttachments; ++index) {
        if (g_attachments[index].used && g_attachments[index].page == page)
            return index;
    }
    return kTestMaxAttachments;
}

// Runs the deferred release exactly once and marks it acknowledged.
void releaseAttachmentOnce(uint32_t index)
{
    if (g_attachments[index].table.releaseContext)
        g_attachments[index].table.releaseContext(g_attachments[index].table.context);
    g_attachments[index].acked = true;
}

// Working createPage slot; hands out incrementing handles on success only.
WKUWPResult __stdcall testCreatePage(const WKUWPPageOptions*, WKUWPPageHandle* page)
{
    ++g_createCalls;
    // Thread affinity is the outermost precondition: an off-owner-thread call
    // is rejected before any argument is read, exactly as the real engine must.
    if (!onOwnerThread())
        return WKUWPResultWrongThread;
    if (g_createResult != WKUWPResultOK)
        return g_createResult;
    if (!page)
        return WKUWPResultInvalidArgument;
    *page = g_nextHandle++;
    recordCreated(*page);
    return WKUWPResultOK;
}

// Counting closePage slot; the harness decides the reported result.
WKUWPResult __stdcall testClosePage(WKUWPPageHandle page)
{
    ++g_closeCalls;
    if (!onOwnerThread())
        return WKUWPResultWrongThread;
    // Closing follows the same deferred safe release as detaching: an active
    // attachment goes inactive at once, and its acknowledgment runs now unless
    // a dispatch is logically active, in which case FireRelease runs it after.
    uint32_t index = findAttachment(page);
    if (index != kTestMaxAttachments && g_attachments[index].active) {
        g_attachments[index].active = false;
        if (!g_dispatching)
            releaseAttachmentOnce(index);
    }
    return g_closeResult;
}

// Counting beginShutdown slot; the harness decides the reported result.
WKUWPResult __stdcall testBeginShutdown()
{
    ++g_beginShutdownCalls;
    if (!onOwnerThread())
        return WKUWPResultWrongThread;
    return g_shutdownResult;
}

// Counting pollShutdown slot; the harness decides the reported result.
WKUWPResult __stdcall testPollShutdown()
{
    ++g_pollShutdownCalls;
    if (!onOwnerThread())
        return WKUWPResultWrongThread;
    return g_pollResult;
}

// Unimplemented initialize slot.
WKUWPResult __stdcall testUnsupportedInitialize(const WKUWPEngineOptions*)
{
    return WKUWPResultUnsupported;
}

// Unimplemented slot used for every table entry the tests do not drive.
WKUWPResult __stdcall testUnsupportedNoArguments()
{
    return WKUWPResultUnsupported;
}

// Unimplemented single-unsigned slot used for setSuspended.
WKUWPResult __stdcall testUnsupportedUnsignedFlag(uint32_t)
{
    return WKUWPResultUnsupported;
}

// Unimplemented handle-only slot used for history, stop and detach entries.
WKUWPResult __stdcall testUnsupportedHandleOnly(WKUWPPageHandle)
{
    return WKUWPResultUnsupported;
}

// Simulated setCallbacks slot; copies the table and retains once on install.
WKUWPResult __stdcall testSetCallbacks(WKUWPPageHandle page, const WKUWPPageCallbacks* callbacks)
{
    if (!onOwnerThread())
        return WKUWPResultWrongThread;
    if (page == 0 || !callbacks)
        return WKUWPResultInvalidArgument;
    if (!pageKnown(page))
        return WKUWPResultInvalidArgument;
    if (callbacks->header.structureVersion != WKUWP_STRUCTURE_VERSION)
        return WKUWPResultInvalidArgument;
    uint32_t index = findAttachment(page);
    if (index != kTestMaxAttachments) {
        // Replacing an active attachment is rejected until its detach is
        // acknowledged; re-attaching after a completed detach is allowed.
        if (g_attachments[index].active)
            return WKUWPResultInvalidState;
    } else {
        index = kTestMaxAttachments;
        for (uint32_t free = 0; free < kTestMaxAttachments; ++free) {
            if (!g_attachments[free].used) {
                index = free;
                break;
            }
        }
        if (index == kTestMaxAttachments)
            return WKUWPResultInvalidState;
    }

    // Retain before install, exactly once; the thunk null-guards for test tables.
    if (callbacks->retainContext)
        callbacks->retainContext(callbacks->context);
    g_attachments[index].used = true;
    g_attachments[index].page = page;
    g_attachments[index].active = true;
    g_attachments[index].acked = false;
    memcpy(&g_attachments[index].table, callbacks, sizeof(g_attachments[index].table));
    return WKUWPResultOK;
}

// Simulated detachCallbacks slot; deactivates now, releases now or later.
WKUWPResult __stdcall testDetachCallbacks(WKUWPPageHandle page)
{
    if (!onOwnerThread())
        return WKUWPResultWrongThread;
    if (page == 0)
        return WKUWPResultInvalidArgument;
    uint32_t index = findAttachment(page);
    if (index == kTestMaxAttachments)
        return pageKnown(page) ? WKUWPResultOK : WKUWPResultInvalidArgument;
    if (!g_attachments[index].active)
        return WKUWPResultOK;
    // Inactive immediately; the release acknowledging it runs now, unless a
    // dispatch is logically active, in which case FireRelease runs it after.
    g_attachments[index].active = false;
    if (g_dispatching)
        return WKUWPResultPending;
    releaseAttachmentOnce(index);
    return WKUWPResultOK;
}

// Unimplemented navigation slots.
WKUWPResult __stdcall testUnsupportedNavigation(WKUWPPageHandle, const WKUWPNavigationRequest*, uint64_t*)
{
    return WKUWPResultUnsupported;
}

// Simulated getState slot; mode 0 preserves the task 3.3 Unsupported default.
WKUWPResult __stdcall testGetState(WKUWPPageHandle page, WKUWPPageState* state, uint8_t*,
    uint32_t capacityBytes, uint32_t* requiredBytes)
{
    ++g_stateCalls;
    if (!onOwnerThread())
        return WKUWPResultWrongThread;
    if (g_stateMode == 0)
        return WKUWPResultUnsupported;
    if (page == 0 || !pageKnown(page))
        return WKUWPResultInvalidArgument;
    if (!state || !requiredBytes)
        return WKUWPResultInvalidArgument;
    if (g_stateMode == 1) {
        // Size-probing engine: short storage reports only the need, leaving
        // state and storage untouched; adequate storage fills a minimal state.
        // Success deliberately leaves requiredBytes untouched.
        if (capacityBytes < kTestStateNeedBytes) {
            *requiredBytes = kTestStateNeedBytes;
            return WKUWPResultBufferTooSmall;
        }
        memset(state, 0, sizeof(*state));
        state->header.structureSize = sizeof(*state);
        state->header.structureVersion = WKUWP_STRUCTURE_VERSION;
        state->pageId = page;
        state->documentGeneration = 1;
        return WKUWPResultOK;
    }

    // Ever-growing engine: every answer needs more than was offered, so only a
    // bounded retry can terminate the fetch.
    *requiredBytes = capacityBytes + kTestStateNeedBytes;
    return WKUWPResultBufferTooSmall;
}

// Unimplemented certificate slot.
WKUWPResult __stdcall testUnsupportedCertificate(WKUWPPageHandle, const WKUWPCertificateToken*, uint64_t*)
{
    return WKUWPResultUnsupported;
}

// Unimplemented sendInput slot.
WKUWPResult __stdcall testUnsupportedSendInput(WKUWPPageHandle, const WKUWPInputEvent*)
{
    return WKUWPResultUnsupported;
}

// Unimplemented setFocus slot.
WKUWPResult __stdcall testUnsupportedSetFocus(WKUWPPageHandle, uint32_t)
{
    return WKUWPResultUnsupported;
}

// Unimplemented resize slot.
WKUWPResult __stdcall testUnsupportedResize(WKUWPPageHandle, uint32_t, uint32_t, double, double)
{
    return WKUWPResultUnsupported;
}

// Unimplemented paint slot.
WKUWPResult __stdcall testUnsupportedPaint(WKUWPPageHandle, const WKUWPPaintBuffer*)
{
    return WKUWPResultUnsupported;
}

// Unimplemented attachCompositor slot.
WKUWPResult __stdcall testUnsupportedAttach(WKUWPPageHandle, const WKUWPCompositorOptions*)
{
    return WKUWPResultUnsupported;
}

// Unimplemented renderCompositor slot.
WKUWPResult __stdcall testUnsupportedRender(WKUWPPageHandle, double)
{
    return WKUWPResultUnsupported;
}

// Nulls one table slot by declaration order, proving the consumer checks every
// slot instead of assuming the fill.
void applyNullSlot(WKUWPEngineAPI* api)
{
    if (g_nullSlotIndex == kTestNullSlotNone)
        return;
    void** slots[] = {
        reinterpret_cast<void**>(&api->initialize),
        reinterpret_cast<void**>(&api->service),
        reinterpret_cast<void**>(&api->setSuspended),
        reinterpret_cast<void**>(&api->createPage),
        reinterpret_cast<void**>(&api->closePage),
        reinterpret_cast<void**>(&api->setCallbacks),
        reinterpret_cast<void**>(&api->detachCallbacks),
        reinterpret_cast<void**>(&api->navigate),
        reinterpret_cast<void**>(&api->loadHtml),
        reinterpret_cast<void**>(&api->goBack),
        reinterpret_cast<void**>(&api->goForward),
        reinterpret_cast<void**>(&api->reload),
        reinterpret_cast<void**>(&api->stop),
        reinterpret_cast<void**>(&api->getState),
        reinterpret_cast<void**>(&api->proceedWithCertificateFailure),
        reinterpret_cast<void**>(&api->sendInput),
        reinterpret_cast<void**>(&api->setFocus),
        reinterpret_cast<void**>(&api->resize),
        reinterpret_cast<void**>(&api->paint),
        reinterpret_cast<void**>(&api->attachCompositor),
        reinterpret_cast<void**>(&api->detachCompositor),
        reinterpret_cast<void**>(&api->renderCompositor),
        reinterpret_cast<void**>(&api->beginShutdown),
        reinterpret_cast<void**>(&api->pollShutdown),
    };
    const uint32_t count = static_cast<uint32_t>(sizeof(slots) / sizeof(slots[0]));
    if (g_nullSlotIndex <= count)
        *slots[g_nullSlotIndex - 1] = nullptr;
}

} // namespace

extern "C" {

// Test discovery export; mirrors the real provider's negotiation order so the
// consumer cannot tell them apart, except where a setter overrides the identity.
__declspec(dllexport) WKUWPResult __stdcall WKUWPGetEngineAPI(
    uint32_t minimumMajor, uint32_t minimumMinor, uint64_t requiredCapabilities, WKUWPEngineAPI* outAPI)
{
    ++g_discoveryCalls;
    if (!outAPI)
        return WKUWPResultInvalidArgument;
    if (outAPI->header.structureVersion != WKUWP_STRUCTURE_VERSION)
        return WKUWPResultAbiMismatch;
    if (outAPI->header.structureSize < sizeof(WKUWPEngineAPI))
        return WKUWPResultAbiMismatch;
    if (outAPI->header.structureSize > kTestMaxDeclaredSize)
        return WKUWPResultAbiMismatch;
    if (minimumMajor != WKUWP_ABI_MAJOR_VERSION)
        return WKUWPResultAbiMismatch;
    if (minimumMinor > g_abiMinor)
        return WKUWPResultAbiMismatch;
    if (requiredCapabilities & ~g_capabilities)
        return WKUWPResultMissingCapability;

    memset(outAPI, 0, outAPI->header.structureSize);
    outAPI->header.structureSize = sizeof(WKUWPEngineAPI);
    outAPI->header.structureVersion = WKUWP_STRUCTURE_VERSION;
    // The reported identity follows the setter, while negotiation above always
    // accepts major 1; that combination models a provider that answered
    // discovery yet reports an incompatible identity, which is exactly what the
    // consumer-side check must catch.
    outAPI->abiMajor = g_abiMajor;
    outAPI->abiMinor = g_abiMinor;
    outAPI->capabilities = g_capabilities;
    outAPI->buildId.data = reinterpret_cast<const uint8_t*>(kTestBuildId);
    outAPI->buildId.length = static_cast<uint32_t>(sizeof(kTestBuildId) - 1);
    outAPI->initialize = testUnsupportedInitialize;
    outAPI->service = testUnsupportedNoArguments;
    outAPI->setSuspended = testUnsupportedUnsignedFlag;
    outAPI->createPage = testCreatePage;
    outAPI->closePage = testClosePage;
    outAPI->setCallbacks = testSetCallbacks;
    outAPI->detachCallbacks = testDetachCallbacks;
    outAPI->navigate = testUnsupportedNavigation;
    outAPI->loadHtml = testUnsupportedNavigation;
    outAPI->goBack = testUnsupportedHandleOnly;
    outAPI->goForward = testUnsupportedHandleOnly;
    outAPI->reload = testUnsupportedHandleOnly;
    outAPI->stop = testUnsupportedHandleOnly;
    outAPI->getState = testGetState;
    outAPI->proceedWithCertificateFailure = testUnsupportedCertificate;
    outAPI->sendInput = testUnsupportedSendInput;
    outAPI->setFocus = testUnsupportedSetFocus;
    outAPI->resize = testUnsupportedResize;
    outAPI->paint = testUnsupportedPaint;
    outAPI->attachCompositor = testUnsupportedAttach;
    outAPI->detachCompositor = testUnsupportedHandleOnly;
    outAPI->renderCompositor = testUnsupportedRender;
    outAPI->beginShutdown = testBeginShutdown;
    outAPI->pollShutdown = testPollShutdown;
    applyNullSlot(outAPI);
    // A truncated reported extent simulates a provider whose known prefix is
    // shorter than this build's table; the consumer must reject it on the size
    // alone, without reading past the reported extent.
    if (g_truncateBytes)
        outAPI->header.structureSize = g_truncateBytes;
    return WKUWPResultOK;
}

// Restores every default and zeroes every counter for the next test case.
__declspec(dllexport) void __stdcall WKUWPTestProviderReset()
{
    g_capabilities = 0;
    g_abiMajor = WKUWP_ABI_MAJOR_VERSION;
    g_abiMinor = WKUWP_ABI_MINOR_VERSION;
    g_truncateBytes = WKUWP_TEST_PROVIDER_DEFAULT_TRUNCATE;
    g_nullSlotIndex = kTestNullSlotNone;
    g_createResult = WKUWPResultUnsupported;
    g_closeResult = WKUWPResultOK;
    g_shutdownResult = WKUWPResultOK;
    g_pollResult = WKUWPResultOK;
    g_discoveryCalls = 0;
    g_createCalls = 0;
    g_closeCalls = 0;
    g_beginShutdownCalls = 0;
    g_pollShutdownCalls = 0;
    g_nextHandle = kTestFirstHandle;
    g_stateMode = 0;
    g_stateCalls = 0;
    g_dispatching = 0;
    g_enforceThread = 0;
    g_sequence = 0;
    g_createdCount = 0;
    // Attachments are dropped without releasing: the harness owns every context
    // it created and releases its own setup references itself, so no simulated
    // engine reference can leak anything but a counter.
    memset(g_attachments, 0, sizeof(g_attachments));
    memset(g_eventUri, 0, sizeof(g_eventUri));
    memset(g_eventTitle, 0, sizeof(g_eventTitle));
}

// Advertises a capability mask to negotiating consumers.
__declspec(dllexport) void __stdcall WKUWPTestProviderSetCapabilities(uint64_t capabilities)
{
    g_capabilities = capabilities;
}

// Overrides the identity reported in a successfully negotiated table.
__declspec(dllexport) void __stdcall WKUWPTestProviderSetAbiVersion(uint32_t major, uint32_t minor)
{
    g_abiMajor = major;
    g_abiMinor = minor;
}

// Overrides the reported filled extent after a successful fill.
__declspec(dllexport) void __stdcall WKUWPTestProviderSetTruncateBytes(uint32_t bytes)
{
    g_truncateBytes = bytes;
}

// Nulls one table slot by declaration order; 0 disables the override.
__declspec(dllexport) void __stdcall WKUWPTestProviderSetNullSlot(uint32_t slotIndex)
{
    g_nullSlotIndex = slotIndex;
}

// Decides the working createPage slot's next answer.
__declspec(dllexport) void __stdcall WKUWPTestProviderSetCreateResult(WKUWPResult result)
{
    g_createResult = result;
}

// Decides the counting closePage slot's next answer.
__declspec(dllexport) void __stdcall WKUWPTestProviderSetCloseResult(WKUWPResult result)
{
    g_closeResult = result;
}

// Decides the counting beginShutdown slot's next answer.
__declspec(dllexport) void __stdcall WKUWPTestProviderSetShutdownResult(WKUWPResult result)
{
    g_shutdownResult = result;
}

// Decides the counting pollShutdown slot's next answer.
__declspec(dllexport) void __stdcall WKUWPTestProviderSetPollResult(WKUWPResult result)
{
    g_pollResult = result;
}

// Number of discovery calls since the last reset.
__declspec(dllexport) uint32_t __stdcall WKUWPTestProviderDiscoveryCalls()
{
    return g_discoveryCalls;
}

// Number of createPage calls since the last reset.
__declspec(dllexport) uint32_t __stdcall WKUWPTestProviderCreateCalls()
{
    return g_createCalls;
}

// Number of closePage calls since the last reset.
__declspec(dllexport) uint32_t __stdcall WKUWPTestProviderCloseCalls()
{
    return g_closeCalls;
}

// Number of beginShutdown calls since the last reset.
__declspec(dllexport) uint32_t __stdcall WKUWPTestProviderBeginShutdownCalls()
{
    return g_beginShutdownCalls;
}

// Number of pollShutdown calls since the last reset.
__declspec(dllexport) uint32_t __stdcall WKUWPTestProviderPollShutdownCalls()
{
    return g_pollShutdownCalls;
}

// Number of getState calls since the last reset, across all modes.
__declspec(dllexport) uint32_t __stdcall WKUWPTestProviderStateCalls()
{
    return g_stateCalls;
}

// Selects the getState behaviour mode for state-snapshot tests.
__declspec(dllexport) void __stdcall WKUWPTestProviderSetStateMode(uint32_t mode)
{
    g_stateMode = mode;
}

// Marks a dispatch logically active so detach answers Pending until release.
__declspec(dllexport) void __stdcall WKUWPTestProviderSetDispatching(uint32_t dispatching)
{
    g_dispatching = dispatching;
}

// Enables owner-thread enforcement, recording the calling thread as the owner.
//
// While enabled, every working slot rejects other threads with WrongThread
// before reading any argument, simulating the real engine's owner-thread rule.
// Disabling returns the double to single-threaded service. Dispatch helpers
// and FireRelease are the engine's own side of a dispatch and stay unchecked.
__declspec(dllexport) void __stdcall WKUWPTestProviderSetEnforceThread(uint32_t enforce)
{
    g_enforceThread = enforce;
    if (enforce)
        g_ownerThreadId = GetCurrentThreadId();
}

// Runs a release deferred by an active dispatch, exactly once per detachment.
//
// Covers both detach-pending and close-pending releases: closing follows the
// same deferred safe release, so a close during dispatch also waits here.
__declspec(dllexport) WKUWPResult __stdcall WKUWPTestProviderFireRelease(WKUWPPageHandle page)
{
    uint32_t index = findAttachment(page);
    if (index == kTestMaxAttachments)
        return pageKnown(page) ? WKUWPResultInvalidState : WKUWPResultInvalidArgument;
    if (g_attachments[index].active || g_attachments[index].acked)
        return WKUWPResultInvalidState;
    releaseAttachmentOnce(index);
    return WKUWPResultOK;
}

// Copies harness text into the provider-owned dispatch buffer, truncating.
uint32_t copyDispatchText(const char* text, uint8_t* buffer)
{
    if (!text)
        return 0;
    size_t length = strlen(text);
    if (length > kTestEventTextBytes)
        length = kTestEventTextBytes;
    memcpy(buffer, text, length);
    return static_cast<uint32_t>(length);
}

// Delivers one notification into the attached table, then scrambles the source.
__declspec(dllexport) WKUWPResult __stdcall WKUWPTestProviderDispatchNotify(
    WKUWPPageHandle page, uint32_t eventKind, const char* uri, const char* title)
{
    uint32_t index = findAttachment(page);
    if (index == kTestMaxAttachments || !g_attachments[index].active)
        return WKUWPResultInvalidState;
    if (!g_attachments[index].table.notify)
        return WKUWPResultInvalidState;
    WKUWPPageEvent event;
    memset(&event, 0, sizeof(event));
    event.header.structureSize = sizeof(event);
    event.header.structureVersion = WKUWP_STRUCTURE_VERSION;
    event.pageId = page;
    event.callbackSequence = ++g_sequence;
    event.eventKind = eventKind;
    event.contentKind = WKUWPContentRemote;
    event.uri.data = g_eventUri;
    event.uri.length = copyDispatchText(uri, g_eventUri);
    event.title.data = g_eventTitle;
    event.title.length = copyDispatchText(title, g_eventTitle);
    g_attachments[index].table.notify(g_attachments[index].table.context, &event);
    // The consumer must have copied what it keeps: nothing borrowed survives.
    memset(g_eventUri, 0xCD, sizeof(g_eventUri));
    memset(g_eventTitle, 0xCD, sizeof(g_eventTitle));
    return WKUWPResultOK;
}

// Delivers one policy callback, reporting the consumer's answer and verdict.
__declspec(dllexport) WKUWPResult __stdcall WKUWPTestProviderDispatchStarting(
    WKUWPPageHandle page, const char* uri, uint32_t* cancelOut)
{
    uint32_t index = findAttachment(page);
    if (index == kTestMaxAttachments || !g_attachments[index].active)
        return WKUWPResultInvalidState;
    if (!g_attachments[index].table.starting || !cancelOut)
        return WKUWPResultInvalidArgument;
    WKUWPPageEvent event;
    memset(&event, 0, sizeof(event));
    event.header.structureSize = sizeof(event);
    event.header.structureVersion = WKUWP_STRUCTURE_VERSION;
    event.pageId = page;
    event.callbackSequence = ++g_sequence;
    event.eventKind = WKUWPEventStarting;
    event.contentKind = WKUWPContentRemote;
    event.uri.data = g_eventUri;
    event.uri.length = copyDispatchText(uri, g_eventUri);
    // The provider sets cancel to 0 before the call, per the contract.
    uint32_t cancel = 0;
    WKUWPResult result = g_attachments[index].table.starting(g_attachments[index].table.context, &event, &cancel);
    *cancelOut = cancel;
    memset(g_eventUri, 0xCD, sizeof(g_eventUri));
    return result;
}

// Delivers a null notification, proving the consumer guards instead of faulting.
__declspec(dllexport) WKUWPResult __stdcall WKUWPTestProviderDispatchNullNotify(WKUWPPageHandle page)
{
    uint32_t index = findAttachment(page);
    if (index == kTestMaxAttachments || !g_attachments[index].active)
        return WKUWPResultInvalidState;
    if (!g_attachments[index].table.notify)
        return WKUWPResultInvalidState;
    g_attachments[index].table.notify(g_attachments[index].table.context, nullptr);
    return WKUWPResultOK;
}

// Delivers a null policy callback, reporting the consumer's guard answer.
__declspec(dllexport) WKUWPResult __stdcall WKUWPTestProviderDispatchNullStarting(
    WKUWPPageHandle page, uint32_t* cancelOut)
{
    uint32_t index = findAttachment(page);
    if (index == kTestMaxAttachments || !g_attachments[index].active)
        return WKUWPResultInvalidState;
    if (!g_attachments[index].table.starting || !cancelOut)
        return WKUWPResultInvalidArgument;
    // A sentinel the consumer must leave alone while refusing the call.
    uint32_t cancel = 0xDEADBEEFu;
    WKUWPResult result = g_attachments[index].table.starting(g_attachments[index].table.context, nullptr, &cancel);
    *cancelOut = cancel;
    return result;
}

} // extern "C"
