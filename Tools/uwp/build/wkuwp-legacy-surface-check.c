/*
 * wkuwp-legacy-surface-check.c - proves the legacy WK*UWP smoke surface still
 * has the exact signatures the explicit map records.
 *
 * Every assignment below binds one legacy export to the typedef that
 * Source/WebCore/platform/uwp/LegacyExportMapUWP.h records for it. An
 * incompatible pointer assignment is a compile error in both C and C++, so any
 * signature drift in PageUWP.h fails the gate instead of silently changing what
 * the smoke consumers call. The numeric and layout asserts live in the map
 * header itself; this file adds the signature half.
 *
 * WKRenderFirstPageUWP is deliberately absent here: with no header declaration
 * there is nothing to freeze against, and inventing one would hide the finding
 * the map records. Its name is proven present by the export inspection in
 * Tools/uwp/build/check-wkuwp-legacy-surface.ps1 instead.
 *
 * The file is compiled for ARM32 by that script with both the native clang-cl
 * driver and the MSVC C compiler used for C++/CX, as C and as C++. It is a
 * compile-only conformance check; it is never linked into the engine, the
 * component or the app.
 */

#include "LegacyExportMapUWP.h"

/* Each line below fails to compile if the recorded signature drifts. */
WKInitializeEngineUWPType wkuwpFreezeInitializeEngineUWP = WKInitializeEngineUWP;
WKCreatePageUWPType wkuwpFreezeCreatePageUWP = WKCreatePageUWP;
WKDestroyPageUWPType wkuwpFreezeDestroyPageUWP = WKDestroyPageUWP;
WKLoadHTMLUWPType wkuwpFreezeLoadHTMLUWP = WKLoadHTMLUWP;
WKNavigatePageUWPType wkuwpFreezeNavigatePageUWP = WKNavigatePageUWP;
WKGoBackPageUWPType wkuwpFreezeGoBackPageUWP = WKGoBackPageUWP;
WKGoForwardPageUWPType wkuwpFreezeGoForwardPageUWP = WKGoForwardPageUWP;
WKReloadPageUWPType wkuwpFreezeReloadPageUWP = WKReloadPageUWP;
WKStopPageUWPType wkuwpFreezeStopPageUWP = WKStopPageUWP;
WKSetNavigationPolicyHandlerPageUWPType wkuwpFreezeSetNavigationPolicyHandlerPageUWP =
    WKSetNavigationPolicyHandlerPageUWP;
WKResizePageUWPType wkuwpFreezeResizePageUWP = WKResizePageUWP;
WKScrollPageUWPType wkuwpFreezeScrollPageUWP = WKScrollPageUWP;
WKPointerPageUWPType wkuwpFreezePointerPageUWP = WKPointerPageUWP;
WKTickPageUWPType wkuwpFreezeTickPageUWP = WKTickPageUWP;
WKPaintPageUWPType wkuwpFreezePaintPageUWP = WKPaintPageUWP;
WKPaintPageScaledUWPType wkuwpFreezePaintPageScaledUWP = WKPaintPageScaledUWP;
WKPageNeedsPaintUWPType wkuwpFreezePageNeedsPaintUWP = WKPageNeedsPaintUWP;
WKGetPageStateUWPType wkuwpFreezeGetPageStateUWP = WKGetPageStateUWP;
WKGetPageDocumentStateUWPType wkuwpFreezeGetPageDocumentStateUWP = WKGetPageDocumentStateUWP;
WKEnableCompositorUWPType wkuwpFreezeEnableCompositorUWP = WKEnableCompositorUWP;
WKDisableCompositorUWPType wkuwpFreezeDisableCompositorUWP = WKDisableCompositorUWP;
WKRenderCompositorUWPType wkuwpFreezeRenderCompositorUWP = WKRenderCompositorUWP;
WKInstallClipboardDispatcherUWPType wkuwpFreezeInstallClipboardDispatcherUWP =
    WKInstallClipboardDispatcherUWP;
