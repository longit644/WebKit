/*
 * wkuwp-abi-layout-check.c - proves the frozen V1 ABI layout is identical for the
 * native engine compiler and the MSVC C++/CX compiler on ARM32.
 *
 * Every assertion here restates a row of the structure table in
 * openspec/changes/wkview-m10-uwp-cppcx-restructure/artifact/native-abi.md. If a
 * compiler disagrees about any size, offset or enumeration width, this translation
 * unit fails to compile and the build stops before any producer or consumer is
 * written against the wrong layout.
 *
 * The file is compiled for ARM32 by Tools/uwp/build/check-wkuwp-abi-layout.ps1
 * with both the native clang-cl driver and the MSVC C compiler used for
 * C++/CX. It is a compile-only conformance check; it is never linked into the
 * engine, the component or the app.
 */

#include "WKUWPEngine.h"
#include "WKUWPPage.h"
#include "WKUWPNavigation.h"
#include "WKUWPInput.h"
#include "WKUWPRendering.h"

#include <stddef.h>

/*
 * MSVC accepts _Static_assert only as C, while the framework consumer compiles
 * these headers as C++, where the spelling is static_assert. Both spellings must
 * carry the same assertions, because a divergence that only shows up in one
 * language is exactly how a frozen structure ends up with two meanings.
 */
#if defined(__cplusplus)
#define WKUWP_STATIC_ASSERT static_assert
#else
#define WKUWP_STATIC_ASSERT _Static_assert
#endif

/* ARM32 is the only supported boundary architecture. */
WKUWP_STATIC_ASSERT(sizeof(void*) == 4, "WKUWP ABI is defined for little-endian ARM32 pointers only");

/* Primitives. */
WKUWP_STATIC_ASSERT(sizeof(uint8_t) == 1, "uint8_t width");
WKUWP_STATIC_ASSERT(sizeof(uint32_t) == 4, "uint32_t width");
WKUWP_STATIC_ASSERT(sizeof(int32_t) == 4, "int32_t width");
WKUWP_STATIC_ASSERT(sizeof(uint64_t) == 8, "uint64_t width");
WKUWP_STATIC_ASSERT(sizeof(double) == 8, "double width");

/* Enumerations are specified as 32-bit values, never compiler-chosen widths. */
WKUWP_STATIC_ASSERT(sizeof(WKUWPResult) == 4, "WKUWPResult width");
WKUWP_STATIC_ASSERT(sizeof(WKUWPCapability) == 4, "WKUWPCapability width");
WKUWP_STATIC_ASSERT(sizeof(WKUWPClipboardKind) == 4, "WKUWPClipboardKind width");
WKUWP_STATIC_ASSERT(sizeof(WKUWPContentKind) == 4, "WKUWPContentKind width");
WKUWP_STATIC_ASSERT(sizeof(WKUWPOutcome) == 4, "WKUWPOutcome width");
WKUWP_STATIC_ASSERT(sizeof(WKUWPErrorDomain) == 4, "WKUWPErrorDomain width");
WKUWP_STATIC_ASSERT(sizeof(WKUWPChangeKind) == 4, "WKUWPChangeKind width");
WKUWP_STATIC_ASSERT(sizeof(WKUWPEventKind) == 4, "WKUWPEventKind width");
WKUWP_STATIC_ASSERT(sizeof(WKUWPInputKind) == 4, "WKUWPInputKind width");
WKUWP_STATIC_ASSERT(sizeof(WKUWPModifier) == 4, "WKUWPModifier width");

/* Enumeration values are frozen; renumbering them is an ABI break. */
WKUWP_STATIC_ASSERT(WKUWPResultOK == 0, "WKUWPResultOK value");
WKUWP_STATIC_ASSERT(WKUWPResultInvalidArgument == 1, "WKUWPResultInvalidArgument value");
WKUWP_STATIC_ASSERT(WKUWPResultNoMainFrame == 4, "WKUWPResultNoMainFrame value");
WKUWP_STATIC_ASSERT(WKUWPResultNoActiveDocumentLoader == 5, "WKUWPResultNoActiveDocumentLoader value");
WKUWP_STATIC_ASSERT(WKUWPResultNoPaintSurface == 6, "WKUWPResultNoPaintSurface value");
WKUWP_STATIC_ASSERT(WKUWPResultDocumentWriterBeginFailed == 7, "WKUWPResultDocumentWriterBeginFailed value");
WKUWP_STATIC_ASSERT(WKUWPResultWrongThread == 8, "WKUWPResultWrongThread value");
WKUWP_STATIC_ASSERT(WKUWPResultCompositorConfigurationUnsupported == 9, "WKUWPResultCompositorConfigurationUnsupported value");
WKUWP_STATIC_ASSERT(WKUWPResultCompositorUnavailable == 10, "WKUWPResultCompositorUnavailable value");
WKUWP_STATIC_ASSERT(WKUWPResultClosed == 16, "WKUWPResultClosed value");
WKUWP_STATIC_ASSERT(WKUWPResultAbiMismatch == 17, "WKUWPResultAbiMismatch value");
WKUWP_STATIC_ASSERT(WKUWPResultMissingCapability == 18, "WKUWPResultMissingCapability value");
WKUWP_STATIC_ASSERT(WKUWPResultInvalidState == 19, "WKUWPResultInvalidState value");
WKUWP_STATIC_ASSERT(WKUWPResultPending == 20, "WKUWPResultPending value");
WKUWP_STATIC_ASSERT(WKUWPResultBufferTooSmall == 21, "WKUWPResultBufferTooSmall value");
WKUWP_STATIC_ASSERT(WKUWPResultUnsupported == 22, "WKUWPResultUnsupported value");
WKUWP_STATIC_ASSERT(WKUWPResultBudgetExceeded == 23, "WKUWPResultBudgetExceeded value");
WKUWP_STATIC_ASSERT(WKUWPResultShuttingDown == 24, "WKUWPResultShuttingDown value");
WKUWP_STATIC_ASSERT(WKUWPResultConfigurationConflict == 25, "WKUWPResultConfigurationConflict value");
WKUWP_STATIC_ASSERT(WKUWPResultInvalidCertificateToken == 26, "WKUWPResultInvalidCertificateToken value");
WKUWP_STATIC_ASSERT(WKUWPResultNotInitialized == 27, "WKUWPResultNotInitialized value");
WKUWP_STATIC_ASSERT(WKUWPResultDispatcherUnavailable == 28, "WKUWPResultDispatcherUnavailable value");

WKUWP_STATIC_ASSERT(WKUWPCapabilityStructuredNotifications == 0x001, "StructuredNotifications bit");
WKUWP_STATIC_ASSERT(WKUWPCapabilityHostHtml == 0x002, "HostHtml bit");
WKUWP_STATIC_ASSERT(WKUWPCapabilityIndependentSessions == 0x004, "IndependentSessions bit");
WKUWP_STATIC_ASSERT(WKUWPCapabilityCPUPaint == 0x008, "CPUPaint bit");
WKUWP_STATIC_ASSERT(WKUWPCapabilityGPUCompositor == 0x010, "GPUCompositor bit");
WKUWP_STATIC_ASSERT(WKUWPCapabilityClipboardService == 0x020, "ClipboardService bit");
WKUWP_STATIC_ASSERT(WKUWPCapabilityCertificateDecisions == 0x040, "CertificateDecisions bit");
WKUWP_STATIC_ASSERT(WKUWPCapabilityInput == 0x080, "Input bit");
WKUWP_STATIC_ASSERT(WKUWPCapabilityImmutableTrustPolicy == 0x100, "ImmutableTrustPolicy bit");
WKUWP_STATIC_ASSERT(WKUWPCapabilityGlobalFontConfiguration == 0x200, "GlobalFontConfiguration bit");

WKUWP_STATIC_ASSERT(WKUWPClipboardRead == 0, "ClipboardRead value");
WKUWP_STATIC_ASSERT(WKUWPClipboardWrite == 1, "ClipboardWrite value");
WKUWP_STATIC_ASSERT(WKUWPContentRemote == 0, "ContentRemote value");
WKUWP_STATIC_ASSERT(WKUWPContentHostHtml == 1, "ContentHostHtml value");
WKUWP_STATIC_ASSERT(WKUWPOutcomeCompleted == 0, "OutcomeCompleted value");
WKUWP_STATIC_ASSERT(WKUWPOutcomeFailed == 1, "OutcomeFailed value");
WKUWP_STATIC_ASSERT(WKUWPOutcomeCancelled == 2, "OutcomeCancelled value");
WKUWP_STATIC_ASSERT(WKUWPErrorDomainNone == 0, "ErrorDomainNone value");
WKUWP_STATIC_ASSERT(WKUWPErrorDomainPlatform == 4, "ErrorDomainPlatform value");
WKUWP_STATIC_ASSERT(WKUWPChangeNewDocument == 0, "ChangeNewDocument value");
WKUWP_STATIC_ASSERT(WKUWPChangeHistoryTraversal == 4, "ChangeHistoryTraversal value");
WKUWP_STATIC_ASSERT(WKUWPEventStarting == 0, "EventStarting value");
WKUWP_STATIC_ASSERT(WKUWPEventRepaintRequested == 5, "EventRepaintRequested value");
WKUWP_STATIC_ASSERT(WKUWPInputKeyDown == 0, "InputKeyDown value");
WKUWP_STATIC_ASSERT(WKUWPInputWheel == 5, "InputWheel value");
WKUWP_STATIC_ASSERT(WKUWPModifierAlt == 0x0001, "ModifierAlt value");
WKUWP_STATIC_ASSERT(WKUWPModifierWindows == 0x0008, "ModifierWindows value");

/* Scalars. */
WKUWP_STATIC_ASSERT(sizeof(WKUWPPageHandle) == 8, "WKUWPPageHandle width");
WKUWP_STATIC_ASSERT(WKUWP_ABI_MAJOR_VERSION == 1, "ABI major version");
WKUWP_STATIC_ASSERT(WKUWP_ABI_MINOR_VERSION == 0, "ABI minor version");
WKUWP_STATIC_ASSERT(WKUWP_STRUCTURE_VERSION == 1, "structure version");
WKUWP_STATIC_ASSERT(WKUWP_ENGINE_API_STRUCTURE_SIZE == 128, "engine API structure size");

/* WKUWPHeader. */
WKUWP_STATIC_ASSERT(sizeof(WKUWPHeader) == 8, "WKUWPHeader size");
WKUWP_STATIC_ASSERT(offsetof(WKUWPHeader, structureSize) == 0, "WKUWPHeader.structureSize offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPHeader, structureVersion) == 4, "WKUWPHeader.structureVersion offset");

/* WKUWPString. */
WKUWP_STATIC_ASSERT(sizeof(WKUWPString) == 8, "WKUWPString size");
WKUWP_STATIC_ASSERT(offsetof(WKUWPString, data) == 0, "WKUWPString.data offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPString, length) == 4, "WKUWPString.length offset");

/* WKUWPPath. */
WKUWP_STATIC_ASSERT(sizeof(WKUWPPath) == 8, "WKUWPPath size");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPath, data) == 0, "WKUWPPath.data offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPath, length) == 4, "WKUWPPath.length offset");

/* WKUWPCertificateToken. */
WKUWP_STATIC_ASSERT(sizeof(WKUWPCertificateToken) == 16, "WKUWPCertificateToken size");
WKUWP_STATIC_ASSERT(offsetof(WKUWPCertificateToken, issuer) == 0, "WKUWPCertificateToken.issuer offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPCertificateToken, decision) == 8, "WKUWPCertificateToken.decision offset");

/* WKUWPPlatformServices. */
WKUWP_STATIC_ASSERT(sizeof(WKUWPPlatformServices) == 32, "WKUWPPlatformServices size");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPlatformServices, header) == 0, "WKUWPPlatformServices.header offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPlatformServices, context) == 8, "WKUWPPlatformServices.context offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPlatformServices, retainContext) == 12, "WKUWPPlatformServices.retainContext offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPlatformServices, releaseContext) == 16, "WKUWPPlatformServices.releaseContext offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPlatformServices, requestClipboard) == 20, "WKUWPPlatformServices.requestClipboard offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPlatformServices, cancelClipboard) == 24, "WKUWPPlatformServices.cancelClipboard offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPlatformServices, postWake) == 28, "WKUWPPlatformServices.postWake offset");

/* WKUWPEngineOptions. */
WKUWP_STATIC_ASSERT(sizeof(WKUWPEngineOptions) == 28, "WKUWPEngineOptions size");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineOptions, header) == 0, "WKUWPEngineOptions.header offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineOptions, fontDirectory) == 8, "WKUWPEngineOptions.fontDirectory offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineOptions, fontFingerprint) == 16, "WKUWPEngineOptions.fontFingerprint offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineOptions, platformServices) == 24, "WKUWPEngineOptions.platformServices offset");

/* WKUWPPageOptions. */
WKUWP_STATIC_ASSERT(sizeof(WKUWPPageOptions) == 56, "WKUWPPageOptions size");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageOptions, header) == 0, "WKUWPPageOptions.header offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageOptions, caBundle) == 8, "WKUWPPageOptions.caBundle offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageOptions, trustFingerprint) == 16, "WKUWPPageOptions.trustFingerprint offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageOptions, maximumConcurrentRequests) == 24, "WKUWPPageOptions.maximumConcurrentRequests offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageOptions, maximumResourceBytes) == 28, "WKUWPPageOptions.maximumResourceBytes offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageOptions, maximumBufferedBytes) == 36, "WKUWPPageOptions.maximumBufferedBytes offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageOptions, requestTimeoutMilliseconds) == 44, "WKUWPPageOptions.requestTimeoutMilliseconds offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageOptions, maximumQueuedRequests) == 48, "WKUWPPageOptions.maximumQueuedRequests offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageOptions, maximumRedirects) == 52, "WKUWPPageOptions.maximumRedirects offset");

/* WKUWPPageState. */
WKUWP_STATIC_ASSERT(sizeof(WKUWPPageState) == 92, "WKUWPPageState size");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageState, header) == 0, "WKUWPPageState.header offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageState, pageId) == 8, "WKUWPPageState.pageId offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageState, documentGeneration) == 16, "WKUWPPageState.documentGeneration offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageState, activeNavigationId) == 24, "WKUWPPageState.activeNavigationId offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageState, canGoBack) == 32, "WKUWPPageState.canGoBack offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageState, canGoForward) == 36, "WKUWPPageState.canGoForward offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageState, isLoading) == 40, "WKUWPPageState.isLoading offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageState, widthCssPixels) == 44, "WKUWPPageState.widthCssPixels offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageState, heightCssPixels) == 48, "WKUWPPageState.heightCssPixels offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageState, scrollX) == 52, "WKUWPPageState.scrollX offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageState, scrollY) == 56, "WKUWPPageState.scrollY offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageState, contentWidth) == 60, "WKUWPPageState.contentWidth offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageState, contentHeight) == 64, "WKUWPPageState.contentHeight offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageState, uri) == 68, "WKUWPPageState.uri offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageState, title) == 76, "WKUWPPageState.title offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageState, hasHttpStatus) == 84, "WKUWPPageState.hasHttpStatus offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageState, httpStatus) == 88, "WKUWPPageState.httpStatus offset");

/* WKUWPNavigationRequest. */
WKUWP_STATIC_ASSERT(sizeof(WKUWPNavigationRequest) == 44, "WKUWPNavigationRequest size");
WKUWP_STATIC_ASSERT(offsetof(WKUWPNavigationRequest, header) == 0, "WKUWPNavigationRequest.header offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPNavigationRequest, contentKind) == 8, "WKUWPNavigationRequest.contentKind offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPNavigationRequest, uri) == 12, "WKUWPNavigationRequest.uri offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPNavigationRequest, html) == 20, "WKUWPNavigationRequest.html offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPNavigationRequest, warningToken) == 28, "WKUWPNavigationRequest.warningToken offset");

/* WKUWPPageEvent. */
WKUWP_STATIC_ASSERT(sizeof(WKUWPPageEvent) == 116, "WKUWPPageEvent size");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageEvent, header) == 0, "WKUWPPageEvent.header offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageEvent, pageId) == 8, "WKUWPPageEvent.pageId offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageEvent, callbackSequence) == 16, "WKUWPPageEvent.callbackSequence offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageEvent, navigationId) == 24, "WKUWPPageEvent.navigationId offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageEvent, documentGeneration) == 32, "WKUWPPageEvent.documentGeneration offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageEvent, eventKind) == 40, "WKUWPPageEvent.eventKind offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageEvent, contentKind) == 44, "WKUWPPageEvent.contentKind offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageEvent, outcome) == 48, "WKUWPPageEvent.outcome offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageEvent, errorDomain) == 52, "WKUWPPageEvent.errorDomain offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageEvent, errorCode) == 56, "WKUWPPageEvent.errorCode offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageEvent, httpStatus) == 60, "WKUWPPageEvent.httpStatus offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageEvent, hasHttpStatus) == 64, "WKUWPPageEvent.hasHttpStatus offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageEvent, changeKind) == 68, "WKUWPPageEvent.changeKind offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageEvent, canGoBack) == 72, "WKUWPPageEvent.canGoBack offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageEvent, canGoForward) == 76, "WKUWPPageEvent.canGoForward offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageEvent, isLoading) == 80, "WKUWPPageEvent.isLoading offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageEvent, uri) == 84, "WKUWPPageEvent.uri offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageEvent, title) == 92, "WKUWPPageEvent.title offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageEvent, certificateToken) == 100, "WKUWPPageEvent.certificateToken offset");

/* WKUWPPageCallbacks. */
WKUWP_STATIC_ASSERT(sizeof(WKUWPPageCallbacks) == 28, "WKUWPPageCallbacks size");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageCallbacks, header) == 0, "WKUWPPageCallbacks.header offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageCallbacks, context) == 8, "WKUWPPageCallbacks.context offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageCallbacks, retainContext) == 12, "WKUWPPageCallbacks.retainContext offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageCallbacks, releaseContext) == 16, "WKUWPPageCallbacks.releaseContext offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageCallbacks, starting) == 20, "WKUWPPageCallbacks.starting offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPageCallbacks, notify) == 24, "WKUWPPageCallbacks.notify offset");

/* WKUWPInputEvent. */
WKUWP_STATIC_ASSERT(sizeof(WKUWPInputEvent) == 52, "WKUWPInputEvent size");
WKUWP_STATIC_ASSERT(offsetof(WKUWPInputEvent, header) == 0, "WKUWPInputEvent.header offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPInputEvent, kind) == 8, "WKUWPInputEvent.kind offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPInputEvent, modifiers) == 12, "WKUWPInputEvent.modifiers offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPInputEvent, pointerId) == 16, "WKUWPInputEvent.pointerId offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPInputEvent, virtualKey) == 20, "WKUWPInputEvent.virtualKey offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPInputEvent, unicodeScalar) == 24, "WKUWPInputEvent.unicodeScalar offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPInputEvent, xCssPixels) == 28, "WKUWPInputEvent.xCssPixels offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPInputEvent, yCssPixels) == 36, "WKUWPInputEvent.yCssPixels offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPInputEvent, wheelDeltaX) == 44, "WKUWPInputEvent.wheelDeltaX offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPInputEvent, wheelDeltaY) == 48, "WKUWPInputEvent.wheelDeltaY offset");

/* WKUWPPaintBuffer. */
WKUWP_STATIC_ASSERT(sizeof(WKUWPPaintBuffer) == 32, "WKUWPPaintBuffer size");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPaintBuffer, header) == 0, "WKUWPPaintBuffer.header offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPaintBuffer, data) == 8, "WKUWPPaintBuffer.data offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPaintBuffer, capacityBytes) == 12, "WKUWPPaintBuffer.capacityBytes offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPaintBuffer, strideBytes) == 20, "WKUWPPaintBuffer.strideBytes offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPaintBuffer, widthPixels) == 24, "WKUWPPaintBuffer.widthPixels offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPPaintBuffer, heightPixels) == 28, "WKUWPPaintBuffer.heightPixels offset");

/* WKUWPCompositorOptions. */
WKUWP_STATIC_ASSERT(sizeof(WKUWPCompositorOptions) == 28, "WKUWPCompositorOptions size");
WKUWP_STATIC_ASSERT(offsetof(WKUWPCompositorOptions, header) == 0, "WKUWPCompositorOptions.header offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPCompositorOptions, panelInspectable) == 8, "WKUWPCompositorOptions.panelInspectable offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPCompositorOptions, scaleX) == 12, "WKUWPCompositorOptions.scaleX offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPCompositorOptions, scaleY) == 20, "WKUWPCompositorOptions.scaleY offset");

/*
 * WKUWPEngineAPI.
 *
 * The 24 function slots are asserted individually because a reordered or dropped
 * slot is invisible to a size check and would silently call the wrong function.
 */
WKUWP_STATIC_ASSERT(sizeof(WKUWPEngineAPI) == 128, "WKUWPEngineAPI size");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, header) == 0, "WKUWPEngineAPI.header offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, abiMajor) == 8, "WKUWPEngineAPI.abiMajor offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, abiMinor) == 12, "WKUWPEngineAPI.abiMinor offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, capabilities) == 16, "WKUWPEngineAPI.capabilities offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, buildId) == 24, "WKUWPEngineAPI.buildId offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, initialize) == 32, "WKUWPEngineAPI.initialize offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, service) == 36, "WKUWPEngineAPI.service offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, setSuspended) == 40, "WKUWPEngineAPI.setSuspended offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, createPage) == 44, "WKUWPEngineAPI.createPage offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, closePage) == 48, "WKUWPEngineAPI.closePage offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, setCallbacks) == 52, "WKUWPEngineAPI.setCallbacks offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, detachCallbacks) == 56, "WKUWPEngineAPI.detachCallbacks offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, navigate) == 60, "WKUWPEngineAPI.navigate offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, loadHtml) == 64, "WKUWPEngineAPI.loadHtml offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, goBack) == 68, "WKUWPEngineAPI.goBack offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, goForward) == 72, "WKUWPEngineAPI.goForward offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, reload) == 76, "WKUWPEngineAPI.reload offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, stop) == 80, "WKUWPEngineAPI.stop offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, getState) == 84, "WKUWPEngineAPI.getState offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, proceedWithCertificateFailure) == 88, "WKUWPEngineAPI.proceedWithCertificateFailure offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, sendInput) == 92, "WKUWPEngineAPI.sendInput offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, setFocus) == 96, "WKUWPEngineAPI.setFocus offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, resize) == 100, "WKUWPEngineAPI.resize offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, paint) == 104, "WKUWPEngineAPI.paint offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, attachCompositor) == 108, "WKUWPEngineAPI.attachCompositor offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, detachCompositor) == 112, "WKUWPEngineAPI.detachCompositor offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, renderCompositor) == 116, "WKUWPEngineAPI.renderCompositor offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, beginShutdown) == 120, "WKUWPEngineAPI.beginShutdown offset");
WKUWP_STATIC_ASSERT(offsetof(WKUWPEngineAPI, pollShutdown) == 124, "WKUWPEngineAPI.pollShutdown offset");

/*
 * Calling-convention signature checks. These fail if a compiler ever treats
 * __stdcall differently here, which is the classic source of silent ABI drift
 * between a native module and a C++/CX module.
 */
typedef WKUWPResult(__stdcall* CheckGetEngineAPISignature)(uint32_t, uint32_t, uint64_t, WKUWPEngineAPI*);
WKUWP_STATIC_ASSERT(sizeof(&WKUWPGetEngineAPI) == sizeof(CheckGetEngineAPISignature),
    "WKUWPGetEngineAPI calling convention");
WKUWP_STATIC_ASSERT(sizeof(((WKUWPEngineAPI*)0)->initialize) == sizeof(CheckGetEngineAPISignature),
    "initialize calling convention");
WKUWP_STATIC_ASSERT(sizeof(((WKUWPPageCallbacks*)0)->notify) == sizeof(void(__stdcall*)(void*, const WKUWPPageEvent*)),
    "notify calling convention");
WKUWP_STATIC_ASSERT(sizeof(((WKUWPPageCallbacks*)0)->starting) == sizeof(WKUWPStarting),
    "starting calling convention");
WKUWP_STATIC_ASSERT(sizeof(((WKUWPPageCallbacks*)0)->retainContext) == sizeof(WKUWPRetainContext),
    "retainContext calling convention");
WKUWP_STATIC_ASSERT(sizeof(((WKUWPPlatformServices*)0)->requestClipboard) == sizeof(void(__stdcall*)(void*, WKUWPPageHandle,
        uint64_t, uint32_t, const WKUWPString*, WKUWPClipboardCompletion, void*)),
    "requestClipboard calling convention");
