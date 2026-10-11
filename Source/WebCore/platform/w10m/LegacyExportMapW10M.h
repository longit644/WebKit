/*
 * LegacyExportMapW10M.h - explicit map from the legacy WK*W10M engine exports to
 * the frozen V1 WKW10M contract.
 *
 * This is M10 task 3.6 of
 * openspec/changes/wkview-m10-uwp-cppcx-restructure/tasks.md. The legacy
 * WK*W10M exports keep serving their existing smoke consumers with unchanged
 * numeric and layout semantics while the engine migrates; new consumers bind
 * only the negotiated V1 table. This header is the map between those two
 * worlds: for every legacy export it records the consumer status, the exact
 * signature, the V1 destination and the owning task.
 *
 * What "forwarder" means here, precisely:
 *
 *   - The result codes need no translation layer at all: every legacy
 *     WKPageResultW10M value the V1 contract also defines is numerically
 *     identical to its WKW10MResult counterpart, so forwarding a legacy result
 *     to a V1 consumer (and back) is the identity map, proven by the asserts
 *     below rather than trusted. The wrapper's HRESULT mapping is recorded per
 *     code and already matches the V1 public mapping.
 *   - The per-export call forwarding (legacy arguments into V1 table calls)
 *     needs engine state that does not exist yet: the void-pointer to page
 *     handle registry, the bound table and the 4.x engine behaviours behind
 *     the slots. Writing those callable forwards now would invent argument
 *     mappings (for example, legacy page creation has no CA bundle or trust
 *     fingerprint for WKW10MPageOptions), so each entry names its V1
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
 * This header is standalone-includable with no WebCore configuration: PageW10M.h
 * needs only stddef.h, which is why the smoke surface can be frozen by an
 * independent check instead of only inside a configured engine tree.
 */

#ifndef LEGACY_EXPORT_MAP_W10M_H
#define LEGACY_EXPORT_MAP_W10M_H

#include "PageW10M.h"
#include "WKW10MEngine.h"

#include <stddef.h>

/*
 * MSVC accepts _Static_assert only as C, while the framework consumer compiles
 * these headers as C++, where the spelling is static_assert. Both spellings
 * must carry the same assertions, because a divergence that only shows up in
 * one language still breaks the boundary the smoke consumers share.
 */
#ifndef WKW10M_STATIC_ASSERT
#if defined(__cplusplus)
#define WKW10M_STATIC_ASSERT static_assert
#else
#define WKW10M_STATIC_ASSERT _Static_assert
#endif
#endif

/*
 * Legacy result codes are numerically identical to the V1 contract codes for
 * every value both define, so result forwarding is the identity map. The two
 * legacy gaps (2, 3) stay reserved on both sides and are never assigned.
 */
WKW10M_STATIC_ASSERT(WKPageResultOkW10M == 0, "legacy Ok value");
WKW10M_STATIC_ASSERT(static_cast<int>(WKPageResultOkW10M) == static_cast<int>(WKW10MResultOK), "OK forwards identically");
WKW10M_STATIC_ASSERT(WKPageResultInvalidArgumentW10M == 1, "legacy InvalidArgument value");
WKW10M_STATIC_ASSERT(static_cast<int>(WKPageResultInvalidArgumentW10M) == static_cast<int>(WKW10MResultInvalidArgument), "InvalidArgument forwards identically");
WKW10M_STATIC_ASSERT(WKPageResultNoMainFrameW10M == 4, "legacy NoMainFrame value");
WKW10M_STATIC_ASSERT(static_cast<int>(WKPageResultNoMainFrameW10M) == static_cast<int>(WKW10MResultNoMainFrame), "NoMainFrame forwards identically");
WKW10M_STATIC_ASSERT(WKPageResultNoActiveDocumentLoaderW10M == 5, "legacy NoActiveDocumentLoader value");
WKW10M_STATIC_ASSERT(static_cast<int>(WKPageResultNoActiveDocumentLoaderW10M) == static_cast<int>(WKW10MResultNoActiveDocumentLoader),
    "NoActiveDocumentLoader forwards identically");
WKW10M_STATIC_ASSERT(WKPageResultNoPaintSurfaceW10M == 6, "legacy NoPaintSurface value");
WKW10M_STATIC_ASSERT(static_cast<int>(WKPageResultNoPaintSurfaceW10M) == static_cast<int>(WKW10MResultNoPaintSurface), "NoPaintSurface forwards identically");
WKW10M_STATIC_ASSERT(WKPageResultDocumentWriterBeginFailedW10M == 7, "legacy DocumentWriterBeginFailed value");
WKW10M_STATIC_ASSERT(static_cast<int>(WKPageResultDocumentWriterBeginFailedW10M) == static_cast<int>(WKW10MResultDocumentWriterBeginFailed),
    "DocumentWriterBeginFailed forwards identically");
WKW10M_STATIC_ASSERT(WKPageResultWrongThreadW10M == 8, "legacy WrongThread value");
WKW10M_STATIC_ASSERT(static_cast<int>(WKPageResultWrongThreadW10M) == static_cast<int>(WKW10MResultWrongThread), "WrongThread forwards identically");
WKW10M_STATIC_ASSERT(WKPageResultCompositorConfigurationUnsupportedW10M == 9,
    "legacy CompositorConfigurationUnsupported value");
WKW10M_STATIC_ASSERT(static_cast<int>(WKPageResultCompositorConfigurationUnsupportedW10M) == static_cast<int>(WKW10MResultCompositorConfigurationUnsupported),
    "CompositorConfigurationUnsupported forwards identically");
WKW10M_STATIC_ASSERT(WKPageResultCompositorUnavailableW10M == 10, "legacy CompositorUnavailable value");
WKW10M_STATIC_ASSERT(static_cast<int>(WKPageResultCompositorUnavailableW10M) == static_cast<int>(WKW10MResultCompositorUnavailable),
    "CompositorUnavailable forwards identically");

/*
 * The wrapper maps these codes to HRESULTs for XAML callers, and the mapping
 * already agrees with the V1 public mapping: WrongThread travels as
 * RPC_E_WRONG_THREAD, InvalidArgument as E_INVALIDARG, success as S_OK and
 * every other failure as E_FAIL. No remapping is owed when the wrapper moves
 * to the negotiated table.
 */
WKW10M_STATIC_ASSERT(WKW10MResultWrongThread == 8, "V1 WrongThread matches the wrapper mapping");
WKW10M_STATIC_ASSERT(WKW10MResultInvalidArgument == 1, "V1 InvalidArgument matches the wrapper mapping");

/*
 * Validation limits enforced on both sides of the legacy ABI, frozen here
 * against the V1 bounds they must survive migration into: CSS viewport edges
 * stay within 2048, physical buffer edges within 4096, BGRA stride within
 * 16384, device scale within 8, and pixels stay premultiplied BGRA8.
 */
WKW10M_STATIC_ASSERT(W10MMaxPageDimension == 2048, "legacy CSS viewport bound");
WKW10M_STATIC_ASSERT(W10MMaxPaintDimension == 4096, "legacy physical buffer bound");
WKW10M_STATIC_ASSERT(W10MMaxPaintStride == 16384, "legacy BGRA stride bound");
WKW10M_STATIC_ASSERT(W10MMaxDeviceScale == 8, "legacy device scale bound");
WKW10M_STATIC_ASSERT(W10MBytesPerBGRA == 4, "legacy BGRA bytes per pixel");

/* Diagnostic log booleans shared by engine logging and the native hosts. */
WKW10M_STATIC_ASSERT(WKPageLogFalseW10M == 0, "legacy log-false value");
WKW10M_STATIC_ASSERT(WKPageLogTrueW10M == 1, "legacy log-true value");

/* Pointer, modifier, history and cancellation values read by every host. */
WKW10M_STATIC_ASSERT(WKPointerEventPressW10M == 0, "legacy press value");
WKW10M_STATIC_ASSERT(WKPointerEventMoveW10M == 1, "legacy move value");
WKW10M_STATIC_ASSERT(WKPointerEventReleaseW10M == 2, "legacy release value");
WKW10M_STATIC_ASSERT(WKPointerModifierNoneW10M == 0, "legacy no-modifier value");
WKW10M_STATIC_ASSERT(WKPointerModifierShiftW10M == 1, "legacy shift value");
WKW10M_STATIC_ASSERT(WKPointerModifierControlW10M == 2, "legacy control value");
WKW10M_STATIC_ASSERT(WKPointerModifierAltW10M == 4, "legacy alt value");
WKW10M_STATIC_ASSERT(WKDocumentChangeKindNoneW10M == 0, "legacy no-change value");
WKW10M_STATIC_ASSERT(WKDocumentChangeKindFragmentW10M == 1, "legacy fragment value");
WKW10M_STATIC_ASSERT(WKDocumentChangeKindPushStateW10M == 2, "legacy push-state value");
WKW10M_STATIC_ASSERT(WKDocumentChangeKindReplaceStateW10M == 3, "legacy replace-state value");
WKW10M_STATIC_ASSERT(WKDocumentChangeKindPopStateW10M == 4, "legacy pop-state value");
WKW10M_STATIC_ASSERT(WKNavigationFailureCancelledW10M == 1, "legacy cancellation value");

/*
 * Legacy page-state snapshot layout consumed by the wrapper and MiniBrowser.
 * Plain 32-bit members on both ARM32 compilers, so the frozen 32-byte layout
 * holds in each domain the smoke consumers compile in.
 */
WKW10M_STATIC_ASSERT(sizeof(WKPageStateW10M) == 32, "legacy page state size");
WKW10M_STATIC_ASSERT(offsetof(WKPageStateW10M, width) == 0, "legacy state width offset");
WKW10M_STATIC_ASSERT(offsetof(WKPageStateW10M, height) == 4, "legacy state height offset");
WKW10M_STATIC_ASSERT(offsetof(WKPageStateW10M, scrollX) == 8, "legacy state scrollX offset");
WKW10M_STATIC_ASSERT(offsetof(WKPageStateW10M, scrollY) == 12, "legacy state scrollY offset");
WKW10M_STATIC_ASSERT(offsetof(WKPageStateW10M, contentWidth) == 16, "legacy state contentWidth offset");
WKW10M_STATIC_ASSERT(offsetof(WKPageStateW10M, contentHeight) == 20, "legacy state contentHeight offset");
WKW10M_STATIC_ASSERT(offsetof(WKPageStateW10M, canGoBack) == 24, "legacy state canGoBack offset");
WKW10M_STATIC_ASSERT(offsetof(WKPageStateW10M, canGoForward) == 28, "legacy state canGoForward offset");

/*
 * Legacy document-state layout. The title member is a pointer, so these
 * 20-byte ARM32 offsets are valid only where the smoke hosts execute: the
 * 32-bit ARM engine. The V1 WKW10MPageState replacement carries no pointers
 * and is the portable contract going forward.
 */
WKW10M_STATIC_ASSERT(sizeof(WKPageDocumentStateW10M) == 20, "legacy document state size");
WKW10M_STATIC_ASSERT(offsetof(WKPageDocumentStateW10M, title) == 0, "legacy document title offset");
WKW10M_STATIC_ASSERT(offsetof(WKPageDocumentStateW10M, httpStatus) == 4, "legacy document httpStatus offset");
WKW10M_STATIC_ASSERT(offsetof(WKPageDocumentStateW10M, hasHttpStatus) == 8, "legacy document hasHttpStatus offset");
WKW10M_STATIC_ASSERT(offsetof(WKPageDocumentStateW10M, documentChange) == 12, "legacy document change offset");
WKW10M_STATIC_ASSERT(offsetof(WKPageDocumentStateW10M, redirectHops) == 16, "legacy document redirectHops offset");

/*
 * Exact legacy signatures, one typedef per export. The check translation unit
 * assigns each export to its typedef, so any signature drift in PageW10M.h fails
 * the gate instead of silently changing what the smoke consumers call.
 */

/* Declared-unbound. Starts the shared engine once per process. Resolved by
 * nothing outside the engine today; PageW10M calls it in-engine. V1
 * destination: initialize, task 4.1. */
typedef unsigned(__stdcall* WKInitializeEngineW10MType)(const wchar_t* fonts, WKPageLogW10M log);

/* Bound. Creates one page at the placeholder extent. V1 destination: createPage, task 4.2. */
typedef void*(__stdcall* WKCreatePageW10MType)(
    const wchar_t* fonts, unsigned width, unsigned height, WKPageLogW10M log, unsigned* result);

/* Bound. Tears down the page; null-safe. V1 destination: closePage, task 4.2. */
typedef unsigned(__stdcall* WKDestroyPageW10MType)(void* page);

/* Bound. Loads app-supplied markup without touching history. V1 destination: loadHtml, task 4.5. */
typedef unsigned(__stdcall* WKLoadHTMLW10MType)(void* page, const char* utf8);

/* Bound. Starts an async navigation verified against a CA bundle. V1 destination: navigate, task 4.3. */
typedef unsigned(__stdcall* WKNavigatePageW10MType)(void* page, const char* utf8, const wchar_t* caPath);

/* Bound. History back with no return payload. V1 destination: goBack, task 5.6 through the controller. */
typedef unsigned(__stdcall* WKGoBackPageW10MType)(void* page);

/* Bound. History forward with no return payload. V1 destination: goForward, task 5.6 through the controller. */
typedef unsigned(__stdcall* WKGoForwardPageW10MType)(void* page);

/* Bound. Re-requests the current document. V1 destination: reload, task 5.6 through the controller. */
typedef unsigned(__stdcall* WKReloadPageW10MType)(void* page);

/* Bound. Cancels the in-flight load. V1 destination: stop, task 5.6 through the controller. */
typedef unsigned(__stdcall* WKStopPageW10MType)(void* page);

/* Bound. Installs the synchronous host veto. V1 destination: setCallbacks plus Starting, task 4.3. */
typedef unsigned(__stdcall* WKSetNavigationPolicyHandlerPageW10MType)(
    void* page, WKNavigationPolicyCallback callback, void* context);

/* Bound. Rebuilds the backing store at the new size. V1 destination: resize, task 6.3. */
typedef unsigned(__stdcall* WKResizePageW10MType)(void* page, unsigned width, unsigned height);

/* Bound. Scrolls by a delta. V1 destination: input plus state via sendInput, task 6.4; no direct slot exists. */
typedef unsigned(__stdcall* WKScrollPageW10MType)(void* page, int dx, int dy);

/* Bound. Delivers one press/move/release event. V1 destination: sendInput, task 6.4. */
typedef unsigned(__stdcall* WKPointerPageW10MType)(
    void* page, unsigned type, double x, double y, unsigned modifiers);

/* Bound. Runs timers, inertia and the network pump for one frame. V1 destination: service, tasks 4.1 and 6.2. */
typedef unsigned(__stdcall* WKTickPageW10MType)(void* page);

/* Bound. Paints unscaled pixels into the caller buffer. V1 destination: paint, task 6.1. */
typedef unsigned(__stdcall* WKPaintPageW10MType)(void* page, unsigned char* bgra, size_t capacity, unsigned stride);

/* Bound. Paints with DPI scale applied. V1 destination: paint with scale, task 6.1. */
typedef unsigned(__stdcall* WKPaintPageScaledW10MType)(
    void* page, unsigned char* bgra, size_t capacity, unsigned stride, double scaleX, double scaleY);

/* Bound. Reports whether a repaint is pending. V1 destination: getState plus repaint events, task 4.3. */
typedef unsigned(__stdcall* WKPageNeedsPaintW10MType)(void* page, unsigned* needsPaint);

/* Bound. Snapshots scroll, content and history state. V1 destination: getState, task 4.3. */
typedef unsigned(__stdcall* WKGetPageStateW10MType)(void* page, WKPageStateW10M* state);

/* Declared-unbound. Typed title/status/change/redirect snapshot. No V1 call forwards to it:
 * the structured equivalent arrives with task 4.3, and this entry must not be
 * presented as a working feature meanwhile. Declared but resolved by nothing. */
typedef unsigned(__stdcall* WKGetPageDocumentStateW10MType)(void* page, WKPageDocumentStateW10M* state);

/* Bound. Starts the GPU compositor on a SwapChainPanel. V1 destination: attachCompositor, task 6.1. */
typedef unsigned(__stdcall* WKEnableCompositorW10MType)(void* page, void* nativePanel, double scaleX, double scaleY);

/* Bound. Stops the GPU compositor path. V1 destination: detachCompositor, task 6.1. */
typedef unsigned(__stdcall* WKDisableCompositorW10MType)(void* page);

/* Bound. Presents one compositor frame with overscroll. V1 destination: renderCompositor, task 6.2. */
typedef unsigned(__stdcall* WKRenderCompositorW10MType)(void* page, double overscrollY);

/*
 * Bound. Installs the clipboard dispatcher; returns an HRESULT, not a page
 * result. V1 destination: platform requestClipboard service, tasks 7.1 and
 * 7.2. The negative values a handler failure carries already follow the V1
 * HRESULT rule, so no code translation is owed.
 */
typedef long(__stdcall* WKInstallClipboardDispatcherW10MType)(void* page, void* dispatcher);

/*
 * Undeclared smoke-only first-page snapshot render. There is deliberately no
 * typedef assignment for it in the check translation unit: with no header
 * declaration there is nothing to freeze against, which is itself the finding.
 * Its FirstPageW10M.cpp spelling takes (fontDirectory, pixels, width, height,
 * log) and SmokeHost resolves it with an inline cast of exactly that shape,
 * reviewed matching; the smoke-only first-page payload moves under
 * diagnostics/ in task 8.5 instead of gaining a declaration here.
 */

#endif /* LEGACY_EXPORT_MAP_W10M_H */

