/*
 * wkw10m-legacy-surface-check.c - proves the legacy WK*W10M smoke surface still
 * has the exact signatures the explicit map records.
 *
 * Every assignment below binds one legacy export to the typedef that
 * Source/WebCore/platform/w10m/LegacyExportMapW10M.h records for it. An
 * incompatible pointer assignment is a compile error in both C and C++, so any
 * signature drift in PageW10M.h fails the gate instead of silently changing what
 * the smoke consumers call. The numeric and layout asserts live in the map
 * header itself; this file adds the signature half.
 *
 * WKRenderFirstPageUWP is deliberately absent here: with no header declaration
 * there is nothing to freeze against, and inventing one would hide the finding
 * the map records. Its name is proven present by the export inspection in
 * Tools/w10m/build/check-wkw10m-legacy-surface.ps1 instead.
 *
 * The file is compiled for ARM32 by that script with both the native clang-cl
 * driver and the MSVC C compiler used for C++/CX, as C and as C++. It is a
 * compile-only conformance check; it is never linked into the engine, the
 * component or the app.
 */

#include "LegacyExportMapW10M.h"

/* Each line below fails to compile if the recorded signature drifts. */
WKInitializeEngineW10MType wkw10mFreezeInitializeEngineW10M = WKInitializeEngineW10M;
WKCreatePageW10MType wkw10mFreezeCreatePageW10M = WKCreatePageW10M;
WKDestroyPageW10MType wkw10mFreezeDestroyPageW10M = WKDestroyPageW10M;
WKLoadHTMLW10MType wkw10mFreezeLoadHTMLW10M = WKLoadHTMLW10M;
WKNavigatePageW10MType wkw10mFreezeNavigatePageW10M = WKNavigatePageW10M;
WKGoBackPageW10MType wkw10mFreezeGoBackPageW10M = WKGoBackPageW10M;
WKGoForwardPageW10MType wkw10mFreezeGoForwardPageW10M = WKGoForwardPageW10M;
WKReloadPageW10MType wkw10mFreezeReloadPageW10M = WKReloadPageW10M;
WKStopPageW10MType wkw10mFreezeStopPageW10M = WKStopPageW10M;
WKSetNavigationPolicyHandlerPageW10MType wkw10mFreezeSetNavigationPolicyHandlerPageW10M =
    WKSetNavigationPolicyHandlerPageW10M;
WKResizePageW10MType wkw10mFreezeResizePageW10M = WKResizePageW10M;
WKScrollPageW10MType wkw10mFreezeScrollPageW10M = WKScrollPageW10M;
WKPointerPageW10MType wkw10mFreezePointerPageW10M = WKPointerPageW10M;
WKTickPageW10MType wkw10mFreezeTickPageW10M = WKTickPageW10M;
WKPaintPageW10MType wkw10mFreezePaintPageW10M = WKPaintPageW10M;
WKPaintPageScaledW10MType wkw10mFreezePaintPageScaledW10M = WKPaintPageScaledW10M;
WKPageNeedsPaintW10MType wkw10mFreezePageNeedsPaintW10M = WKPageNeedsPaintW10M;
WKGetPageStateW10MType wkw10mFreezeGetPageStateW10M = WKGetPageStateW10M;
WKGetPageDocumentStateW10MType wkw10mFreezeGetPageDocumentStateW10M = WKGetPageDocumentStateW10M;
WKEnableCompositorW10MType wkw10mFreezeEnableCompositorW10M = WKEnableCompositorW10M;
WKDisableCompositorW10MType wkw10mFreezeDisableCompositorW10M = WKDisableCompositorW10M;
WKRenderCompositorW10MType wkw10mFreezeRenderCompositorW10M = WKRenderCompositorW10M;
WKInstallClipboardDispatcherW10MType wkw10mFreezeInstallClipboardDispatcherW10M =
    WKInstallClipboardDispatcherW10M;
