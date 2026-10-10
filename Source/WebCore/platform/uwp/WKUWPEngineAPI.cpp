/*
 * WKUWPEngineAPI.cpp - provider side of the frozen V1 WKUWP engine contract:
 * the discovery export, the capability mask and the module build identity.
 *
 * This is M10 task 3.2 of
 * openspec/changes/wkview-m10-uwp-cppcx-restructure/tasks.md, and implements the
 * negotiation rules of artifact/native-abi.md ("Exact engine function table").
 * That document remains the authority; the declarations it froze live in
 * Source/WebKit/Shared/uwp and are verified for both boundary compilers by
 * Tools/uwp/build/check-wkuwp-abi-layout.ps1.
 *
 * Scope limits are deliberate and are not implementation shortcuts:
 *
 *   - Negotiation, capabilities and build identity are delivered here. Configured
 *     engine builds also forward process service/suspension (task 6.2), after
 *     actual engine initialization; standalone negotiation tests have no WebCore
 *     dependency and retain Unsupported stubs for those slots. The
 *     24 table slots are filled, because the contract forbids a null call, but
 *     every slot whose implementation belongs to a later task reports
 *     WKUWPResultUnsupported. Nothing here creates a page, allocates an
 *     identifier or touches engine state, so a rejected negotiation request
 *     cannot have produced a side effect.
 *   - The capability mask advertises only what a consumer can rely on
 *     end to end: the engine must honour the bit and the control must be able
 *     to consume it through production forwarding. Engine-side work alone does
 *     not advertise; neither does a host-side service the engine never calls.
 *     Which today is nothing: production initialization still fails Unsupported
 *     by design, engine slots report Unsupported, and the runtime owner in
 *     task 7.5 is unwired. Each bit is added only by the task that completes
 *     its end-to-end path. Claiming a capability that the engine does not
 *     honour would make a consumer's negotiation succeed and then fail at use,
 *     which is precisely the silent-failure mode the contract is written to
 *     prevent.
 *   - Discovery is stateless, reentrant and safe to call from any thread before
 *     initialization, so the module-pinning and runtime-ownership state that task
 *     3.3 adds cannot be observed through an ordering bug here.
 *
 * This translation unit deliberately does not include WebCore's config.h. It uses
 * no platform or feature macro, and staying free of the configured engine tree is
 * what lets task 3.2's negotiation logic be compiled and executed with both
 * boundary compilers exactly as the frozen ABI layout check already does, instead
 * of only inside one configured build tree. The engine target compiles it with its
 * usual WebCore settings either way.
 */

#include "WKUWPEngine.h"

#include <string.h>

#if defined(WKUWP_OWNER_SERVICE_FORWARD)
// Configured-engine forwards; standalone negotiation remains dependency-free.
extern "C" WKUWPResult __stdcall wkuwpEngineServiceUWP();
// Actual process resource-suspension operation, independent of page presentation.
extern "C" WKUWPResult __stdcall wkuwpEngineSetSuspendedUWP(uint32_t);
#endif

namespace {

// Capabilities this build delivers, as a bitwise OR of WKUWPCapability values.
//
// A bit is advertised only when its end-to-end path works through production
// forwarding, never for engine-side or host-side work alone. Engine state and
// the unblocking condition per bit (task 7.4 review):
// StructuredNotifications (0x001): engine navigation controller done (4.3,
// real signals); needs production forwarding integration.
// HostHtml (0x002): substitute-data contract done (4.5); needs the same.
// IndependentSessions (0x004): cookie isolation done (4.7); needs the same.
// CPUPaint (0x008): CPU presentation device-proven (6.1); needs production
// presenter wiring and resize correctness (6.3).
// GPUCompositor (0x010): engine-owned path exists; needs production visual
// attach plus the same resize gate.
// ClipboardService (0x020): host route device-proven (7.2); the engine still
// self-serves clipboard internally (7.3) and the runtime owner is unwired, so
// the bit needs an engine-forward decision plus task 7.5 wiring.
// CertificateDecisions (0x040): decision store done (4.8); needs production
// forwarding integration.
// Input (0x080): adapter done (6.4) with real-engine keyboard/focus explicit
// E_NOTIMPL; needs engine slot delivery plus production wiring.
// ImmutableTrustPolicy (0x100): immutable policy done (4.11); needs
// production forwarding integration.
// GlobalFontConfiguration (0x200): canonical fingerprint done (4.1/4.11);
// needs production initialization integration.
constexpr uint64_t s_deliveredCapabilities = 0;

// Every capability bit frozen by artifact/native-abi.md; nothing else may be claimed.
constexpr uint64_t kFrozenCapabilityBits = WKUWPCapabilityStructuredNotifications
    | WKUWPCapabilityHostHtml
    | WKUWPCapabilityIndependentSessions
    | WKUWPCapabilityCPUPaint
    | WKUWPCapabilityGPUCompositor
    | WKUWPCapabilityClipboardService
    | WKUWPCapabilityCertificateDecisions
    | WKUWPCapabilityInput
    | WKUWPCapabilityImmutableTrustPolicy
    | WKUWPCapabilityGlobalFontConfiguration;

static_assert(!(s_deliveredCapabilities & ~kFrozenCapabilityBits),
    "A capability bit outside the frozen V1 table must not be advertised");

// Defensive upper bound on the caller's declared structure extent.
//
// A conforming V1 caller declares exactly WKUWP_ENGINE_API_STRUCTURE_SIZE bytes.
// The contract asks the provider to zero the caller-declared extent, which makes
// that declared value a write length; without a bound a corrupt header could ask
// this module to write an arbitrary number of bytes. Anything above this bound is
// rejected as a revision mismatch instead of trusted, and no plausible future
// minor version approaches it, because slots are appended in 4-byte units.
constexpr uint32_t kMaximumDeclaredStructureSize = 4096;

// The frozen V1 table size must fit inside the table this provider fills.
//
// On the only supported boundary architecture, little-endian ARM32, a function
// pointer is four bytes and sizeof(WKUWPEngineAPI) is exactly
// WKUWP_ENGINE_API_STRUCTURE_SIZE, so the two rules below coincide. On a 64-bit
// development host used to exercise negotiation behaviour, function pointers are
// eight bytes and the structure is larger; the assertion keeps the frozen constant
// meaningful there instead of letting a host-only build quietly fill a table the
// caller never declared room for.
static_assert(WKUWP_ENGINE_API_STRUCTURE_SIZE <= sizeof(WKUWPEngineAPI),
    "The frozen V1 table size must fit inside the engine table this provider fills");

// Build stamp supplied by the build system, defaulting to the compiler's own
// translation-unit stamp.
//
// Orchestration and the staged identity manifest are task 10.3; defining
// WKUWP_BUILD_STAMP lets that work replace the fallback without changing this
// file. A string literal is used so the identity is immutable before the first
// instruction runs, which is what "module-owned immutable bytes, valid for the
// process lifetime" requires: no lazy initialization and therefore no
// initialization race between two threads that both call discovery first.
#ifndef WKUWP_BUILD_STAMP
#define WKUWP_BUILD_STAMP __DATE__ " " __TIME__
#endif

// Module build identity reported as WKUWPEngineAPI::buildId.
//
// Format: "wkuwp abi=<major>.<minor> arch=arm32 build=<stamp>". It identifies
// which engine image answered discovery, which is what a consumer, the staging
// audit and the M10 device records need to tell two builds apart.
const char s_buildId[] = "wkuwp abi="
                         "1.0"
                         " arch=arm32 build=" WKUWP_BUILD_STAMP;

// Unimplemented initialize slot; the engine process setup arrives with task 4.1.
WKUWPResult __stdcall unimplementedInitialize(const WKUWPEngineOptions*)
{
    return WKUWPResultUnsupported;
}

// Standalone service and pending shutdown slots; configured service uses the real owner forward.
WKUWPResult __stdcall unimplementedNoArguments()
{
    return WKUWPResultUnsupported;
}

// Unimplemented single-unsigned slots: setSuspended and setFocus.
WKUWPResult __stdcall unimplementedUnsignedFlag(uint32_t)
{
    return WKUWPResultUnsupported;
}

// Unimplemented createPage slot; the page/session host arrives with task 4.2.
WKUWPResult __stdcall unimplementedCreatePage(const WKUWPPageOptions*, WKUWPPageHandle*)
{
    return WKUWPResultUnsupported;
}

// Unimplemented handle-only slots: closePage, history, stop, callback and compositor detach.
WKUWPResult __stdcall unimplementedHandleOnly(WKUWPPageHandle)
{
    return WKUWPResultUnsupported;
}

// Unimplemented setFocus slot; keyboard focus handling arrives with task 6.4.
WKUWPResult __stdcall unimplementedSetFocus(WKUWPPageHandle, uint32_t)
{
    return WKUWPResultUnsupported;
}

// Unimplemented setCallbacks slot; retained per-page context arrives with task 3.4.
WKUWPResult __stdcall unimplementedSetCallbacks(WKUWPPageHandle, const WKUWPPageCallbacks*)
{
    return WKUWPResultUnsupported;
}

// Unimplemented navigation slots: navigate and loadHtml, arriving with tasks 4.3-4.5.
WKUWPResult __stdcall unimplementedNavigation(WKUWPPageHandle, const WKUWPNavigationRequest*, uint64_t*)
{
    return WKUWPResultUnsupported;
}

// Unimplemented getState slot; caller-owned string storage arrives with task 4.3.
WKUWPResult __stdcall unimplementedGetState(WKUWPPageHandle, WKUWPPageState*, uint8_t*, uint32_t, uint32_t*)
{
    return WKUWPResultUnsupported;
}

// Unimplemented certificate slot; the opaque decision store arrives with task 4.8.
WKUWPResult __stdcall unimplementedProceedWithCertificateFailure(WKUWPPageHandle, const WKUWPCertificateToken*, uint64_t*)
{
    return WKUWPResultUnsupported;
}

// Unimplemented sendInput slot; the input adapter arrives with task 6.4.
WKUWPResult __stdcall unimplementedSendInput(WKUWPPageHandle, const WKUWPInputEvent*)
{
    return WKUWPResultUnsupported;
}

// Unimplemented resize slot; size and DPI handling arrive with task 6.3.
WKUWPResult __stdcall unimplementedResize(WKUWPPageHandle, uint32_t, uint32_t, double, double)
{
    return WKUWPResultUnsupported;
}

// Unimplemented paint slot; the CPU presenter arrives with task 6.1.
WKUWPResult __stdcall unimplementedPaint(WKUWPPageHandle, const WKUWPPaintBuffer*)
{
    return WKUWPResultUnsupported;
}

// Unimplemented attachCompositor slot; engine-owned GPU surfaces arrive with task 6.1.
WKUWPResult __stdcall unimplementedAttachCompositor(WKUWPPageHandle, const WKUWPCompositorOptions*)
{
    return WKUWPResultUnsupported;
}

// Unimplemented renderCompositor slot; frame presentation arrives with task 6.2.
WKUWPResult __stdcall unimplementedRenderCompositor(WKUWPPageHandle, double)
{
    return WKUWPResultUnsupported;
}

// Fills the known V1 prefix of a negotiated table.
//
// The caller has already validated version, extent, minimum version and required
// capabilities, and has had its declared extent zeroed. Every one of the 24 slots is
// assigned, because the contract requires every V1 slot to exist and forbids a null
// call; a slot whose feature is not implemented yet reports WKUWPResultUnsupported
// when called instead of crashing the consumer.
void fillEngineTable(WKUWPEngineAPI* api)
{
    api->header.structureSize = sizeof(WKUWPEngineAPI);
    api->header.structureVersion = WKUWP_STRUCTURE_VERSION;
    api->abiMajor = WKUWP_ABI_MAJOR_VERSION;
    api->abiMinor = WKUWP_ABI_MINOR_VERSION;
    api->capabilities = s_deliveredCapabilities;
    api->buildId.data = reinterpret_cast<const uint8_t*>(s_buildId);
    api->buildId.length = static_cast<uint32_t>(sizeof(s_buildId) - 1);
    api->initialize = unimplementedInitialize;
    api->service = unimplementedNoArguments;
    api->setSuspended = unimplementedUnsignedFlag;
#if defined(WKUWP_OWNER_SERVICE_FORWARD)
    // Service delivery does not advertise still-unsupported page/rendering capability bits.
    api->service = wkuwpEngineServiceUWP;
    api->setSuspended = wkuwpEngineSetSuspendedUWP;
#endif
    api->createPage = unimplementedCreatePage;
    api->closePage = unimplementedHandleOnly;
    api->setCallbacks = unimplementedSetCallbacks;
    api->detachCallbacks = unimplementedHandleOnly;
    api->navigate = unimplementedNavigation;
    api->loadHtml = unimplementedNavigation;
    api->goBack = unimplementedHandleOnly;
    api->goForward = unimplementedHandleOnly;
    api->reload = unimplementedHandleOnly;
    api->stop = unimplementedHandleOnly;
    api->getState = unimplementedGetState;
    api->proceedWithCertificateFailure = unimplementedProceedWithCertificateFailure;
    api->sendInput = unimplementedSendInput;
    api->setFocus = unimplementedSetFocus;
    api->resize = unimplementedResize;
    api->paint = unimplementedPaint;
    api->attachCompositor = unimplementedAttachCompositor;
    api->detachCompositor = unimplementedHandleOnly;
    api->renderCompositor = unimplementedRenderCompositor;
    api->beginShutdown = unimplementedNoArguments;
    api->pollShutdown = unimplementedNoArguments;
}

} // namespace

extern "C" WKUWP_EXPORT WKUWPResult __stdcall WKUWPGetEngineAPI(
    uint32_t minimumMajor, uint32_t minimumMinor, uint64_t requiredCapabilities, WKUWPEngineAPI* outAPI)
{
    // A null output pointer is the caller's own bug, not a revision problem, so it
    // reports InvalidArgument before anything is read or written.
    if (!outAPI)
        return WKUWPResultInvalidArgument;

    // The caller's header is the only evidence of what it was built against. An
    // unreadable or foreign version, an extent too small to hold the V1 table, or
    // an extent beyond the write bound all fail before any output is written, so
    // a rejected caller's structure keeps whatever it held.
    //
    // The minimum extent is the frozen V1 size and, when this build's structure is
    // larger than that, its own size. Accepting a caller that declared less than
    // the provider is about to fill would let the provider write past the end of
    // the caller's structure; on ARM32 the two are the same value.
    if (outAPI->header.structureVersion != WKUWP_STRUCTURE_VERSION)
        return WKUWPResultAbiMismatch;
    if (outAPI->header.structureSize < WKUWP_ENGINE_API_STRUCTURE_SIZE)
        return WKUWPResultAbiMismatch;
    if (outAPI->header.structureSize < sizeof(WKUWPEngineAPI))
        return WKUWPResultAbiMismatch;
    if (outAPI->header.structureSize > kMaximumDeclaredStructureSize)
        return WKUWPResultAbiMismatch;

    // A major version is a hard compatibility boundary, so it must match exactly
    // rather than being satisfied by a greater value: a future provider could
    // legitimately change the meaning of a slot this consumer reads.
    if (minimumMajor != WKUWP_ABI_MAJOR_VERSION)
        return WKUWPResultAbiMismatch;

    // A consumer may accept an older table than the provider offers, never a newer one.
    if (minimumMinor > WKUWP_ABI_MINOR_VERSION)
        return WKUWPResultAbiMismatch;

    // Every requested capability must be delivered. Reporting MissingCapability
    // here, before the table is written, is what keeps a consumer from creating a
    // page against a feature this engine does not implement.
    if (requiredCapabilities & ~s_deliveredCapabilities)
        return WKUWPResultMissingCapability;

    // Zero the caller-declared extent first, so a consumer that declared more than
    // the V1 table sees a zeroed tail instead of uninitialized bytes in slots this
    // provider cannot fill. The documented filled extent is then the V1 size, which
    // is how a consumer detects that an appended slot is absent.
    memset(outAPI, 0, outAPI->header.structureSize);
    fillEngineTable(outAPI);
    return WKUWPResultOK;
}
