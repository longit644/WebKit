/*
 * LegacyExportMapUWP.h - explicit map from the legacy WK*UWP engine exports to
 * the frozen V1 WKUWP contract.
 *
 * This is M10 task 3.6 of
 * openspec/changes/wkview-m10-uwp-cppcx-restructure/tasks.md. The legacy
 * WK*UWP exports keep serving their existing smoke consumers with unchanged
 * numeric and layout semantics while the engine migrates; new consumers bind
 * only the negotiated V1 table. This header is the map between those two
 * worlds: for every legacy export it records the consumer status, the exact
 * signature, the V1 destination and the owning task.
 *
 * What "forwarder" means here, precisely:
 *
 *   - The result codes need no translation layer at all: every legacy
 *     WKPageResultUWP value the V1 contract also defines is numerically
 *     identical to its WKUWPResult counterpart, so forwarding a legacy result
 *     to a V1 consumer (and back) is the identity map, proven by the asserts
 *     below rather than trusted. The wrapper's HRESULT mapping is recorded per
 *     code and already matches the V1 public mapping.
 *   - The per-export call forwarding (legacy arguments into V1 table calls)
 *     needs engine state that does not exist yet: the void-pointer to page
 *     handle registry, the bound table and the 4.x engine behaviours behind
 *     the slots. Writing those callable forwards now would invent argument
 *     mappings (for example, legacy page creation has no CA bundle or trust
 *     fingerprint for WKUWPPageOptions), so each entry names its V1
 *     destination and owning task instead of pretending the call already works.
 *   - What this file does prove, at compile time on both boundary compilers:
 *     every legacy result code, validation limit, log boolean, pointer and
 *     history value is numerically frozen; both legacy state structures keep
 *     their exact ARM32 layout; and every declared legacy signature still
 *     matches the typedef recorded here (the check translation unit assigns
 *     each export to its typedef, which fails to compile on any drift).
 *
 * Status words used below: bound (the wrapper resolves it), smoke-only (a
 * diagnostic host resolves it), declared-unbound (declared but resolved by
 * nothing) and undeclared (exported with no header declaration at all).
 *
 * This header is standalone-includable with no WebCore configuration: PageUWP.h
 * needs only stddef.h, which is why the smoke surface can be frozen by an
 * independent check instead of only inside a configured engine tree.
 */

#ifndef LEGACY_EXPORT_MAP_UWP_H
#define LEGACY_EXPORT_MAP_UWP_H

#include "PageUWP.h"
#include "WKUWPEngine.h"

#include <stddef.h>

/*
 * MSVC accepts _Static_assert only as C, while the framework consumer compiles
 * these headers as C++, where the spelling is static_assert. Both spellings
 * must carry the same assertions, because a divergence that only shows up in
 * one language still breaks the boundary the smoke consumers share.
 */
#ifndef WKUWP_STATIC_ASSERT
#if defined(__cplusplus)
#define WKUWP_STATIC_ASSERT static_assert
#else
#define WKUWP_STATIC_ASSERT _Static_assert
#endif
#endif

/*
 * Legacy result codes are numerically identical to the V1 contract codes for
 * every value both define, so result forwarding is the identity map. The two
 * legacy gaps (2, 3) stay reserved on both sides and are never assigned.
 */
WKUWP_STATIC_ASSERT(WKPageResultOkUWP == 0, "legacy Ok value");
WKUWP_STATIC_ASSERT(static_cast<int>(WKPageResultOkUWP) == static_cast<int>(WKUWPResultOK), "OK forwards identically");
WKUWP_STATIC_ASSERT(WKPageResultInvalidArgumentUWP == 1, "legacy InvalidArgument value");
WKUWP_STATIC_ASSERT(static_cast<int>(WKPageResultInvalidArgumentUWP) == static_cast<int>(WKUWPResultInvalidArgument), "InvalidArgument forwards identically");
WKUWP_STATIC_ASSERT(WKPageResultNoMainFrameUWP == 4, "legacy NoMainFrame value");
WKUWP_STATIC_ASSERT(static_cast<int>(WKPageResultNoMainFrameUWP) == static_cast<int>(WKUWPResultNoMainFrame), "NoMainFrame forwards identically");
WKUWP_STATIC_ASSERT(WKPageResultNoActiveDocumentLoaderUWP == 5, "legacy NoActiveDocumentLoader value");
WKUWP_STATIC_ASSERT(static_cast<int>(WKPageResultNoActiveDocumentLoaderUWP) == static_cast<int>(WKUWPResultNoActiveDocumentLoader),
    "NoActiveDocumentLoader forwards identically");
WKUWP_STATIC_ASSERT(WKPageResultNoPaintSurfaceUWP == 6, "legacy NoPaintSurface value");
WKUWP_STATIC_ASSERT(static_cast<int>(WKPageResultNoPaintSurfaceUWP) == static_cast<int>(WKUWPResultNoPaintSurface), "NoPaintSurface forwards identically");
WKUWP_STATIC_ASSERT(WKPageResultDocumentWriterBeginFailedUWP == 7, "legacy DocumentWriterBeginFailed value");
WKUWP_STATIC_ASSERT(static_cast<int>(WKPageResultDocumentWriterBeginFailedUWP) == static_cast<int>(WKUWPResultDocumentWriterBeginFailed),
    "DocumentWriterBeginFailed forwards identically");
WKUWP_STATIC_ASSERT(WKPageResultWrongThreadUWP == 8, "legacy WrongThread value");
WKUWP_STATIC_ASSERT(static_cast<int>(WKPageResultWrongThreadUWP) == static_cast<int>(WKUWPResultWrongThread), "WrongThread forwards identically");
WKUWP_STATIC_ASSERT(WKPageResultCompositorConfigurationUnsupportedUWP == 9,
    "legacy CompositorConfigurationUnsupported value");
WKUWP_STATIC_ASSERT(static_cast<int>(WKPageResultCompositorConfigurationUnsupportedUWP) == static_cast<int>(WKUWPResultCompositorConfigurationUnsupported),
    "CompositorConfigurationUnsupported forwards identically");
WKUWP_STATIC_ASSERT(WKPageResultCompositorUnavailableUWP == 10, "legacy CompositorUnavailable value");
WKUWP_STATIC_ASSERT(static_cast<int>(WKPageResultCompositorUnavailableUWP) == static_cast<int>(WKUWPResultCompositorUnavailable),
    "CompositorUnavailable forwards identically");

/*
 * The wrapper maps these codes to HRESULTs for XAML callers, and the mapping
 * already agrees with the V1 public mapping: WrongThread travels as
 * RPC_E_WRONG_THREAD, InvalidArgument as E_INVALIDARG, success as S_OK and
 * every other failure as E_FAIL. No remapping is owed when the wrapper moves
 * to the negotiated table.
 */
WKUWP_STATIC_ASSERT(WKUWPResultWrongThread == 8, "V1 WrongThread matches the wrapper mapping");
WKUWP_STATIC_ASSERT(WKUWPResultInvalidArgument == 1, "V1 InvalidArgument matches the wrapper mapping");

/*
 * Validation limits enforced on both sides of the legacy ABI, frozen here
 * against the V1 bounds they must survive migration into: CSS viewport edges
 * stay within 2048, physical buffer edges within 4096, BGRA stride within
 * 16384, device scale within 8, and pixels stay premultiplied BGRA8.
 */
WKUWP_STATIC_ASSERT(UWPMaxPageDimension == 2048, "legacy CSS viewport bound");
WKUWP_STATIC_ASSERT(UWPMaxPaintDimension == 4096, "legacy physical buffer bound");
WKUWP_STATIC_ASSERT(UWPMaxPaintStride == 16384, "legacy BGRA stride bound");
WKUWP_STATIC_ASSERT(UWPMaxDeviceScale == 8, "legacy device scale bound");
WKUWP_STATIC_ASSERT(UWPBytesPerBGRA == 4, "legacy BGRA bytes per pixel");

/* Diagnostic log booleans shared by engine logging and the native hosts. */
WKUWP_STATIC_ASSERT(WKPageLogFalseUWP == 0, "legacy log-false value");
WKUWP_STATIC_ASSERT(WKPageLogTrueUWP == 1, "legacy log-true value");

/* Pointer, modifier, history and cancellation values read by every host. */
WKUWP_STATIC_ASSERT(WKPointerEventPressUWP == 0, "legacy press value");
WKUWP_STATIC_ASSERT(WKPointerEventMoveUWP == 1, "legacy move value");
WKUWP_STATIC_ASSERT(WKPointerEventReleaseUWP == 2, "legacy release value");
WKUWP_STATIC_ASSERT(WKPointerModifierNoneUWP == 0, "legacy no-modifier value");
WKUWP_STATIC_ASSERT(WKPointerModifierShiftUWP == 1, "legacy shift value");
WKUWP_STATIC_ASSERT(WKPointerModifierControlUWP == 2, "legacy control value");
WKUWP_STATIC_ASSERT(WKPointerModifierAltUWP == 4, "legacy alt value");
WKUWP_STATIC_ASSERT(WKDocumentChangeKindNoneUWP == 0, "legacy no-change value");
WKUWP_STATIC_ASSERT(WKDocumentChangeKindFragmentUWP == 1, "legacy fragment value");
WKUWP_STATIC_ASSERT(WKDocumentChangeKindPushStateUWP == 2, "legacy push-state value");
WKUWP_STATIC_ASSERT(WKDocumentChangeKindReplaceStateUWP == 3, "legacy replace-state value");
WKUWP_STATIC_ASSERT(WKDocumentChangeKindPopStateUWP == 4, "legacy pop-state value");
WKUWP_STATIC_ASSERT(WKNavigationFailureCancelledUWP == 1, "legacy cancellation value");

/*
 * Legacy page-state snapshot layout consumed by the wrapper and MiniBrowser.
 * Plain 32-bit members on both ARM32 compilers, so the frozen 32-byte layout
 * holds in each domain the smoke consumers compile in.
 */
WKUWP_STATIC_ASSERT(sizeof(WKPageStateUWP) == 32, "legacy page state size");
WKUWP_STATIC_ASSERT(offsetof(WKPageStateUWP, width) == 0, "legacy state width offset");
WKUWP_STATIC_ASSERT(offsetof(WKPageStateUWP, height) == 4, "legacy state height offset");
WKUWP_STATIC_ASSERT(offsetof(WKPageStateUWP, scrollX) == 8, "legacy state scrollX offset");
WKUWP_STATIC_ASSERT(offsetof(WKPageStateUWP, scrollY) == 12, "legacy state scrollY offset");
WKUWP_STATIC_ASSERT(offsetof(WKPageStateUWP, contentWidth) == 16, "legacy state contentWidth offset");
WKUWP_STATIC_ASSERT(offsetof(WKPageStateUWP, contentHeight) == 20, "legacy state contentHeight offset");
WKUWP_STATIC_ASSERT(offsetof(WKPageStateUWP, canGoBack) == 24, "legacy state canGoBack offset");
WKUWP_STATIC_ASSERT(offsetof(WKPageStateUWP, canGoForward) == 28, "legacy state canGoForward offset");

/*
 * Legacy document-state layout. The title member is a pointer, so these
 * 20-byte ARM32 offsets are valid only where the smoke hosts execute: the
 * 32-bit ARM engine. The V1 WKUWPPageState replacement carries no pointers
 * and is the portable contract going forward.
 */
WKUWP_STATIC_ASSERT(sizeof(WKPageDocumentStateUWP) == 20, "legacy document state size");
WKUWP_STATIC_ASSERT(offsetof(WKPageDocumentStateUWP, title) == 0, "legacy document title offset");
WKUWP_STATIC_ASSERT(offsetof(WKPageDocumentStateUWP, httpStatus) == 4, "legacy document httpStatus offset");
WKUWP_STATIC_ASSERT(offsetof(WKPageDocumentStateUWP, hasHttpStatus) == 8, "legacy document hasHttpStatus offset");
WKUWP_STATIC_ASSERT(offsetof(WKPageDocumentStateUWP, documentChange) == 12, "legacy document change offset");
WKUWP_STATIC_ASSERT(offsetof(WKPageDocumentStateUWP, redirectHops) == 16, "legacy document redirectHops offset");

/*
 * Exact legacy signatures, one typedef per export. The check translation unit
 * assigns each export to its typedef, so any signature drift in PageUWP.h fails
 * the gate instead of silently changing what the smoke consumers call.
 */

/* Declared-unbound. Starts the shared engine once per process. Resolved by
 * nothing outside the engine today; PageUWP calls it in-engine. V1
 * destination: initialize, task 4.1. */
typedef unsigned(__stdcall* WKInitializeEngineUWPType)(const wchar_t* fonts, WKPageLogUWP log);

/* Bound. Creates one page at the placeholder extent. V1 destination: createPage, task 4.2. */
typedef void*(__stdcall* WKCreatePageUWPType)(
    const wchar_t* fonts, unsigned width, unsigned height, WKPageLogUWP log, unsigned* result);

/* Bound. Tears down the page; null-safe. V1 destination: closePage, task 4.2. */
typedef unsigned(__stdcall* WKDestroyPageUWPType)(void* page);

/* Bound. Loads app-supplied markup without touching history. V1 destination: loadHtml, task 4.5. */
typedef unsigned(__stdcall* WKLoadHTMLUWPType)(void* page, const char* utf8);

/* Bound. Starts an async navigation verified against a CA bundle. V1 destination: navigate, task 4.3. */
typedef unsigned(__stdcall* WKNavigatePageUWPType)(void* page, const char* utf8, const wchar_t* caPath);

/* Bound. History back with no return payload. V1 destination: goBack, task 5.6 through the controller. */
typedef unsigned(__stdcall* WKGoBackPageUWPType)(void* page);

/* Bound. History forward with no return payload. V1 destination: goForward, task 5.6 through the controller. */
typedef unsigned(__stdcall* WKGoForwardPageUWPType)(void* page);

/* Bound. Re-requests the current document. V1 destination: reload, task 5.6 through the controller. */
typedef unsigned(__stdcall* WKReloadPageUWPType)(void* page);

/* Bound. Cancels the in-flight load. V1 destination: stop, task 5.6 through the controller. */
typedef unsigned(__stdcall* WKStopPageUWPType)(void* page);

/* Bound. Installs the synchronous host veto. V1 destination: setCallbacks plus Starting, task 4.3. */
typedef unsigned(__stdcall* WKSetNavigationPolicyHandlerPageUWPType)(
    void* page, WKNavigationPolicyCallback callback, void* context);

/* Bound. Rebuilds the backing store at the new size. V1 destination: resize, task 6.3. */
typedef unsigned(__stdcall* WKResizePageUWPType)(void* page, unsigned width, unsigned height);

/* Bound. Scrolls by a delta. V1 destination: input plus state via sendInput, task 6.4; no direct slot exists. */
typedef unsigned(__stdcall* WKScrollPageUWPType)(void* page, int dx, int dy);

/* Bound. Delivers one press/move/release event. V1 destination: sendInput, task 6.4. */
typedef unsigned(__stdcall* WKPointerPageUWPType)(
    void* page, unsigned type, double x, double y, unsigned modifiers);

/* Bound. Runs timers, inertia and the network pump for one frame. V1 destination: service, tasks 4.1 and 6.2. */
typedef unsigned(__stdcall* WKTickPageUWPType)(void* page);

/* Bound. Paints unscaled pixels into the caller buffer. V1 destination: paint, task 6.1. */
typedef unsigned(__stdcall* WKPaintPageUWPType)(void* page, unsigned char* bgra, size_t capacity, unsigned stride);

/* Bound. Paints with DPI scale applied. V1 destination: paint with scale, task 6.1. */
typedef unsigned(__stdcall* WKPaintPageScaledUWPType)(
    void* page, unsigned char* bgra, size_t capacity, unsigned stride, double scaleX, double scaleY);

/* Bound. Reports whether a repaint is pending. V1 destination: getState plus repaint events, task 4.3. */
typedef unsigned(__stdcall* WKPageNeedsPaintUWPType)(void* page, unsigned* needsPaint);

/* Bound. Snapshots scroll, content and history state. V1 destination: getState, task 4.3. */
typedef unsigned(__stdcall* WKGetPageStateUWPType)(void* page, WKPageStateUWP* state);

/* Declared-unbound. Typed title/status/change/redirect snapshot. No V1 call forwards to it:
 * the structured equivalent arrives with task 4.3, and this entry must not be
 * presented as a working feature meanwhile. Declared but resolved by nothing. */
typedef unsigned(__stdcall* WKGetPageDocumentStateUWPType)(void* page, WKPageDocumentStateUWP* state);

/* Bound. Starts the GPU compositor on a SwapChainPanel. V1 destination: attachCompositor, task 6.1. */
typedef unsigned(__stdcall* WKEnableCompositorUWPType)(void* page, void* nativePanel, double scaleX, double scaleY);

/* Bound. Stops the GPU compositor path. V1 destination: detachCompositor, task 6.1. */
typedef unsigned(__stdcall* WKDisableCompositorUWPType)(void* page);

/* Bound. Presents one compositor frame with overscroll. V1 destination: renderCompositor, task 6.2. */
typedef unsigned(__stdcall* WKRenderCompositorUWPType)(void* page, double overscrollY);

/*
 * Bound. Installs the clipboard dispatcher; returns an HRESULT, not a page
 * result. V1 destination: platform requestClipboard service, tasks 7.1 and
 * 7.2. The negative values a handler failure carries already follow the V1
 * HRESULT rule, so no code translation is owed.
 */
typedef long(__stdcall* WKInstallClipboardDispatcherUWPType)(void* page, void* dispatcher);

/*
 * Undeclared smoke-only first-page snapshot render. There is deliberately no
 * typedef assignment for it in the check translation unit: with no header
 * declaration there is nothing to freeze against, which is itself the finding.
 * Its FirstPageUWP.cpp spelling takes (fontDirectory, pixels, width, height,
 * log) and SmokeHost resolves it with an inline cast of exactly that shape,
 * reviewed matching; the smoke-only first-page payload moves under
 * diagnostics/ in task 8.5 instead of gaining a declaration here.
 */

#endif /* LEGACY_EXPORT_MAP_UWP_H */
