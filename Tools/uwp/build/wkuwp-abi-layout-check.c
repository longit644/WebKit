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

/* ARM32 is the only supported boundary architecture. */
_Static_assert(sizeof(void*) == 4, "WKUWP ABI is defined for little-endian ARM32 pointers only");

/* Primitives. */
_Static_assert(sizeof(uint8_t) == 1, "uint8_t width");
_Static_assert(sizeof(uint32_t) == 4, "uint32_t width");
_Static_assert(sizeof(int32_t) == 4, "int32_t width");
_Static_assert(sizeof(uint64_t) == 8, "uint64_t width");
_Static_assert(sizeof(double) == 8, "double width");

/* Enumerations are specified as 32-bit values, never compiler-chosen widths. */
_Static_assert(sizeof(WKUWPResult) == 4, "WKUWPResult width");
_Static_assert(sizeof(WKUWPCapability) == 4, "WKUWPCapability width");
_Static_assert(sizeof(WKUWPClipboardKind) == 4, "WKUWPClipboardKind width");
_Static_assert(sizeof(WKUWPContentKind) == 4, "WKUWPContentKind width");
_Static_assert(sizeof(WKUWPOutcome) == 4, "WKUWPOutcome width");
_Static_assert(sizeof(WKUWPErrorDomain) == 4, "WKUWPErrorDomain width");
_Static_assert(sizeof(WKUWPChangeKind) == 4, "WKUWPChangeKind width");
_Static_assert(sizeof(WKUWPEventKind) == 4, "WKUWPEventKind width");
_Static_assert(sizeof(WKUWPInputKind) == 4, "WKUWPInputKind width");
_Static_assert(sizeof(WKUWPModifier) == 4, "WKUWPModifier width");

/* Enumeration values are frozen; renumbering them is an ABI break. */
_Static_assert(WKUWPResultOk == 0, "WKUWPResultOk value");
_Static_assert(WKUWPResultInvalidArgument == 1, "WKUWPResultInvalidArgument value");
_Static_assert(WKUWPResultNoMainFrame == 4, "WKUWPResultNoMainFrame value");
_Static_assert(WKUWPResultNoActiveDocumentLoader == 5, "WKUWPResultNoActiveDocumentLoader value");
_Static_assert(WKUWPResultNoPaintSurface == 6, "WKUWPResultNoPaintSurface value");
_Static_assert(WKUWPResultDocumentWriterBeginFailed == 7, "WKUWPResultDocumentWriterBeginFailed value");
_Static_assert(WKUWPResultWrongThread == 8, "WKUWPResultWrongThread value");
_Static_assert(WKUWPResultCompositorConfigurationUnsupported == 9, "WKUWPResultCompositorConfigurationUnsupported value");
_Static_assert(WKUWPResultCompositorUnavailable == 10, "WKUWPResultCompositorUnavailable value");
_Static_assert(WKUWPResultClosed == 16, "WKUWPResultClosed value");
_Static_assert(WKUWPResultAbiMismatch == 17, "WKUWPResultAbiMismatch value");
_Static_assert(WKUWPResultMissingCapability == 18, "WKUWPResultMissingCapability value");
_Static_assert(WKUWPResultInvalidState == 19, "WKUWPResultInvalidState value");
_Static_assert(WKUWPResultPending == 20, "WKUWPResultPending value");
_Static_assert(WKUWPResultBufferTooSmall == 21, "WKUWPResultBufferTooSmall value");
_Static_assert(WKUWPResultUnsupported == 22, "WKUWPResultUnsupported value");
_Static_assert(WKUWPResultBudgetExceeded == 23, "WKUWPResultBudgetExceeded value");
_Static_assert(WKUWPResultShuttingDown == 24, "WKUWPResultShuttingDown value");
_Static_assert(WKUWPResultConfigurationConflict == 25, "WKUWPResultConfigurationConflict value");
_Static_assert(WKUWPResultInvalidCertificateToken == 26, "WKUWPResultInvalidCertificateToken value");
_Static_assert(WKUWPResultNotInitialized == 27, "WKUWPResultNotInitialized value");
_Static_assert(WKUWPResultDispatcherUnavailable == 28, "WKUWPResultDispatcherUnavailable value");

_Static_assert(WKUWPCapabilityStructuredNotifications == 0x001, "StructuredNotifications bit");
_Static_assert(WKUWPCapabilityHostHtml == 0x002, "HostHtml bit");
_Static_assert(WKUWPCapabilityIndependentSessions == 0x004, "IndependentSessions bit");
_Static_assert(WKUWPCapabilityCpuPaint == 0x008, "CpuPaint bit");
_Static_assert(WKUWPCapabilityGpuCompositor == 0x010, "GpuCompositor bit");
_Static_assert(WKUWPCapabilityClipboardService == 0x020, "ClipboardService bit");
_Static_assert(WKUWPCapabilityCertificateDecisions == 0x040, "CertificateDecisions bit");
_Static_assert(WKUWPCapabilityInput == 0x080, "Input bit");
_Static_assert(WKUWPCapabilityImmutableTrustPolicy == 0x100, "ImmutableTrustPolicy bit");
_Static_assert(WKUWPCapabilityGlobalFontConfiguration == 0x200, "GlobalFontConfiguration bit");

_Static_assert(WKUWPClipboardRead == 0, "ClipboardRead value");
_Static_assert(WKUWPClipboardWrite == 1, "ClipboardWrite value");
_Static_assert(WKUWPContentRemote == 0, "ContentRemote value");
_Static_assert(WKUWPContentHostHtml == 1, "ContentHostHtml value");
_Static_assert(WKUWPOutcomeCompleted == 0, "OutcomeCompleted value");
_Static_assert(WKUWPOutcomeFailed == 1, "OutcomeFailed value");
_Static_assert(WKUWPOutcomeCancelled == 2, "OutcomeCancelled value");
_Static_assert(WKUWPErrorDomainNone == 0, "ErrorDomainNone value");
_Static_assert(WKUWPErrorDomainPlatform == 4, "ErrorDomainPlatform value");
_Static_assert(WKUWPChangeNewDocument == 0, "ChangeNewDocument value");
_Static_assert(WKUWPChangeHistoryTraversal == 4, "ChangeHistoryTraversal value");
_Static_assert(WKUWPEventStarting == 0, "EventStarting value");
_Static_assert(WKUWPEventRepaintRequested == 5, "EventRepaintRequested value");
_Static_assert(WKUWPInputKeyDown == 0, "InputKeyDown value");
_Static_assert(WKUWPInputWheel == 5, "InputWheel value");
_Static_assert(WKUWPModifierAlt == 0x0001, "ModifierAlt value");
_Static_assert(WKUWPModifierWindows == 0x0008, "ModifierWindows value");

/* Scalars. */
_Static_assert(sizeof(WKUWPPageHandle) == 8, "WKUWPPageHandle width");
_Static_assert(WKUWP_ABI_MAJOR_VERSION == 1, "ABI major version");
_Static_assert(WKUWP_ABI_MINOR_VERSION == 0, "ABI minor version");
_Static_assert(WKUWP_STRUCTURE_VERSION == 1, "structure version");
_Static_assert(WKUWP_ENGINE_API_STRUCTURE_SIZE == 128, "engine API structure size");

/* WKUWPHeader. */
_Static_assert(sizeof(WKUWPHeader) == 8, "WKUWPHeader size");
_Static_assert(offsetof(WKUWPHeader, structureSize) == 0, "WKUWPHeader.structureSize offset");
_Static_assert(offsetof(WKUWPHeader, structureVersion) == 4, "WKUWPHeader.structureVersion offset");

/* WKUWPString. */
_Static_assert(sizeof(WKUWPString) == 8, "WKUWPString size");
_Static_assert(offsetof(WKUWPString, data) == 0, "WKUWPString.data offset");
_Static_assert(offsetof(WKUWPString, length) == 4, "WKUWPString.length offset");

/* WKUWPPath. */
_Static_assert(sizeof(WKUWPPath) == 8, "WKUWPPath size");
_Static_assert(offsetof(WKUWPPath, data) == 0, "WKUWPPath.data offset");
_Static_assert(offsetof(WKUWPPath, length) == 4, "WKUWPPath.length offset");

/* WKUWPCertificateToken. */
_Static_assert(sizeof(WKUWPCertificateToken) == 16, "WKUWPCertificateToken size");
_Static_assert(offsetof(WKUWPCertificateToken, issuer) == 0, "WKUWPCertificateToken.issuer offset");
_Static_assert(offsetof(WKUWPCertificateToken, decision) == 8, "WKUWPCertificateToken.decision offset");

/* WKUWPPlatformServices. */
_Static_assert(sizeof(WKUWPPlatformServices) == 32, "WKUWPPlatformServices size");
_Static_assert(offsetof(WKUWPPlatformServices, header) == 0, "WKUWPPlatformServices.header offset");
_Static_assert(offsetof(WKUWPPlatformServices, context) == 8, "WKUWPPlatformServices.context offset");
_Static_assert(offsetof(WKUWPPlatformServices, retainContext) == 12, "WKUWPPlatformServices.retainContext offset");
_Static_assert(offsetof(WKUWPPlatformServices, releaseContext) == 16, "WKUWPPlatformServices.releaseContext offset");
_Static_assert(offsetof(WKUWPPlatformServices, requestClipboard) == 20, "WKUWPPlatformServices.requestClipboard offset");
_Static_assert(offsetof(WKUWPPlatformServices, cancelClipboard) == 24, "WKUWPPlatformServices.cancelClipboard offset");
_Static_assert(offsetof(WKUWPPlatformServices, postWake) == 28, "WKUWPPlatformServices.postWake offset");

/* WKUWPEngineOptions. */
_Static_assert(sizeof(WKUWPEngineOptions) == 28, "WKUWPEngineOptions size");
_Static_assert(offsetof(WKUWPEngineOptions, header) == 0, "WKUWPEngineOptions.header offset");
_Static_assert(offsetof(WKUWPEngineOptions, fontDirectory) == 8, "WKUWPEngineOptions.fontDirectory offset");
_Static_assert(offsetof(WKUWPEngineOptions, fontFingerprint) == 16, "WKUWPEngineOptions.fontFingerprint offset");
_Static_assert(offsetof(WKUWPEngineOptions, platformServices) == 24, "WKUWPEngineOptions.platformServices offset");

/* WKUWPPageOptions. */
_Static_assert(sizeof(WKUWPPageOptions) == 56, "WKUWPPageOptions size");
_Static_assert(offsetof(WKUWPPageOptions, header) == 0, "WKUWPPageOptions.header offset");
_Static_assert(offsetof(WKUWPPageOptions, caBundle) == 8, "WKUWPPageOptions.caBundle offset");
_Static_assert(offsetof(WKUWPPageOptions, trustFingerprint) == 16, "WKUWPPageOptions.trustFingerprint offset");
_Static_assert(offsetof(WKUWPPageOptions, maximumConcurrentRequests) == 24, "WKUWPPageOptions.maximumConcurrentRequests offset");
_Static_assert(offsetof(WKUWPPageOptions, maximumResourceBytes) == 28, "WKUWPPageOptions.maximumResourceBytes offset");
_Static_assert(offsetof(WKUWPPageOptions, maximumBufferedBytes) == 36, "WKUWPPageOptions.maximumBufferedBytes offset");
_Static_assert(offsetof(WKUWPPageOptions, requestTimeoutMilliseconds) == 44, "WKUWPPageOptions.requestTimeoutMilliseconds offset");
_Static_assert(offsetof(WKUWPPageOptions, maximumQueuedRequests) == 48, "WKUWPPageOptions.maximumQueuedRequests offset");
_Static_assert(offsetof(WKUWPPageOptions, maximumRedirects) == 52, "WKUWPPageOptions.maximumRedirects offset");

/* WKUWPPageState. */
_Static_assert(sizeof(WKUWPPageState) == 92, "WKUWPPageState size");
_Static_assert(offsetof(WKUWPPageState, header) == 0, "WKUWPPageState.header offset");
_Static_assert(offsetof(WKUWPPageState, pageId) == 8, "WKUWPPageState.pageId offset");
_Static_assert(offsetof(WKUWPPageState, documentGeneration) == 16, "WKUWPPageState.documentGeneration offset");
_Static_assert(offsetof(WKUWPPageState, activeNavigationId) == 24, "WKUWPPageState.activeNavigationId offset");
_Static_assert(offsetof(WKUWPPageState, canGoBack) == 32, "WKUWPPageState.canGoBack offset");
_Static_assert(offsetof(WKUWPPageState, canGoForward) == 36, "WKUWPPageState.canGoForward offset");
_Static_assert(offsetof(WKUWPPageState, isLoading) == 40, "WKUWPPageState.isLoading offset");
_Static_assert(offsetof(WKUWPPageState, widthCssPixels) == 44, "WKUWPPageState.widthCssPixels offset");
_Static_assert(offsetof(WKUWPPageState, heightCssPixels) == 48, "WKUWPPageState.heightCssPixels offset");
_Static_assert(offsetof(WKUWPPageState, scrollX) == 52, "WKUWPPageState.scrollX offset");
_Static_assert(offsetof(WKUWPPageState, scrollY) == 56, "WKUWPPageState.scrollY offset");
_Static_assert(offsetof(WKUWPPageState, contentWidth) == 60, "WKUWPPageState.contentWidth offset");
_Static_assert(offsetof(WKUWPPageState, contentHeight) == 64, "WKUWPPageState.contentHeight offset");
_Static_assert(offsetof(WKUWPPageState, uri) == 68, "WKUWPPageState.uri offset");
_Static_assert(offsetof(WKUWPPageState, title) == 76, "WKUWPPageState.title offset");
_Static_assert(offsetof(WKUWPPageState, hasHttpStatus) == 84, "WKUWPPageState.hasHttpStatus offset");
_Static_assert(offsetof(WKUWPPageState, httpStatus) == 88, "WKUWPPageState.httpStatus offset");

/* WKUWPNavigationRequest. */
_Static_assert(sizeof(WKUWPNavigationRequest) == 44, "WKUWPNavigationRequest size");
_Static_assert(offsetof(WKUWPNavigationRequest, header) == 0, "WKUWPNavigationRequest.header offset");
_Static_assert(offsetof(WKUWPNavigationRequest, contentKind) == 8, "WKUWPNavigationRequest.contentKind offset");
_Static_assert(offsetof(WKUWPNavigationRequest, uri) == 12, "WKUWPNavigationRequest.uri offset");
_Static_assert(offsetof(WKUWPNavigationRequest, html) == 20, "WKUWPNavigationRequest.html offset");
_Static_assert(offsetof(WKUWPNavigationRequest, warningToken) == 28, "WKUWPNavigationRequest.warningToken offset");

/* WKUWPPageEvent. */
_Static_assert(sizeof(WKUWPPageEvent) == 116, "WKUWPPageEvent size");
_Static_assert(offsetof(WKUWPPageEvent, header) == 0, "WKUWPPageEvent.header offset");
_Static_assert(offsetof(WKUWPPageEvent, pageId) == 8, "WKUWPPageEvent.pageId offset");
_Static_assert(offsetof(WKUWPPageEvent, callbackSequence) == 16, "WKUWPPageEvent.callbackSequence offset");
_Static_assert(offsetof(WKUWPPageEvent, navigationId) == 24, "WKUWPPageEvent.navigationId offset");
_Static_assert(offsetof(WKUWPPageEvent, documentGeneration) == 32, "WKUWPPageEvent.documentGeneration offset");
_Static_assert(offsetof(WKUWPPageEvent, eventKind) == 40, "WKUWPPageEvent.eventKind offset");
_Static_assert(offsetof(WKUWPPageEvent, contentKind) == 44, "WKUWPPageEvent.contentKind offset");
_Static_assert(offsetof(WKUWPPageEvent, outcome) == 48, "WKUWPPageEvent.outcome offset");
_Static_assert(offsetof(WKUWPPageEvent, errorDomain) == 52, "WKUWPPageEvent.errorDomain offset");
_Static_assert(offsetof(WKUWPPageEvent, errorCode) == 56, "WKUWPPageEvent.errorCode offset");
_Static_assert(offsetof(WKUWPPageEvent, httpStatus) == 60, "WKUWPPageEvent.httpStatus offset");
_Static_assert(offsetof(WKUWPPageEvent, hasHttpStatus) == 64, "WKUWPPageEvent.hasHttpStatus offset");
_Static_assert(offsetof(WKUWPPageEvent, changeKind) == 68, "WKUWPPageEvent.changeKind offset");
_Static_assert(offsetof(WKUWPPageEvent, canGoBack) == 72, "WKUWPPageEvent.canGoBack offset");
_Static_assert(offsetof(WKUWPPageEvent, canGoForward) == 76, "WKUWPPageEvent.canGoForward offset");
_Static_assert(offsetof(WKUWPPageEvent, isLoading) == 80, "WKUWPPageEvent.isLoading offset");
_Static_assert(offsetof(WKUWPPageEvent, uri) == 84, "WKUWPPageEvent.uri offset");
_Static_assert(offsetof(WKUWPPageEvent, title) == 92, "WKUWPPageEvent.title offset");
_Static_assert(offsetof(WKUWPPageEvent, certificateToken) == 100, "WKUWPPageEvent.certificateToken offset");

/* WKUWPPageCallbacks. */
_Static_assert(sizeof(WKUWPPageCallbacks) == 28, "WKUWPPageCallbacks size");
_Static_assert(offsetof(WKUWPPageCallbacks, header) == 0, "WKUWPPageCallbacks.header offset");
_Static_assert(offsetof(WKUWPPageCallbacks, context) == 8, "WKUWPPageCallbacks.context offset");
_Static_assert(offsetof(WKUWPPageCallbacks, retainContext) == 12, "WKUWPPageCallbacks.retainContext offset");
_Static_assert(offsetof(WKUWPPageCallbacks, releaseContext) == 16, "WKUWPPageCallbacks.releaseContext offset");
_Static_assert(offsetof(WKUWPPageCallbacks, starting) == 20, "WKUWPPageCallbacks.starting offset");
_Static_assert(offsetof(WKUWPPageCallbacks, notify) == 24, "WKUWPPageCallbacks.notify offset");

/* WKUWPInputEvent. */
_Static_assert(sizeof(WKUWPInputEvent) == 52, "WKUWPInputEvent size");
_Static_assert(offsetof(WKUWPInputEvent, header) == 0, "WKUWPInputEvent.header offset");
_Static_assert(offsetof(WKUWPInputEvent, kind) == 8, "WKUWPInputEvent.kind offset");
_Static_assert(offsetof(WKUWPInputEvent, modifiers) == 12, "WKUWPInputEvent.modifiers offset");
_Static_assert(offsetof(WKUWPInputEvent, pointerId) == 16, "WKUWPInputEvent.pointerId offset");
_Static_assert(offsetof(WKUWPInputEvent, virtualKey) == 20, "WKUWPInputEvent.virtualKey offset");
_Static_assert(offsetof(WKUWPInputEvent, unicodeScalar) == 24, "WKUWPInputEvent.unicodeScalar offset");
_Static_assert(offsetof(WKUWPInputEvent, xCssPixels) == 28, "WKUWPInputEvent.xCssPixels offset");
_Static_assert(offsetof(WKUWPInputEvent, yCssPixels) == 36, "WKUWPInputEvent.yCssPixels offset");
_Static_assert(offsetof(WKUWPInputEvent, wheelDeltaX) == 44, "WKUWPInputEvent.wheelDeltaX offset");
_Static_assert(offsetof(WKUWPInputEvent, wheelDeltaY) == 48, "WKUWPInputEvent.wheelDeltaY offset");

/* WKUWPPaintBuffer. */
_Static_assert(sizeof(WKUWPPaintBuffer) == 32, "WKUWPPaintBuffer size");
_Static_assert(offsetof(WKUWPPaintBuffer, header) == 0, "WKUWPPaintBuffer.header offset");
_Static_assert(offsetof(WKUWPPaintBuffer, data) == 8, "WKUWPPaintBuffer.data offset");
_Static_assert(offsetof(WKUWPPaintBuffer, capacityBytes) == 12, "WKUWPPaintBuffer.capacityBytes offset");
_Static_assert(offsetof(WKUWPPaintBuffer, strideBytes) == 20, "WKUWPPaintBuffer.strideBytes offset");
_Static_assert(offsetof(WKUWPPaintBuffer, widthPixels) == 24, "WKUWPPaintBuffer.widthPixels offset");
_Static_assert(offsetof(WKUWPPaintBuffer, heightPixels) == 28, "WKUWPPaintBuffer.heightPixels offset");

/* WKUWPCompositorOptions. */
_Static_assert(sizeof(WKUWPCompositorOptions) == 28, "WKUWPCompositorOptions size");
_Static_assert(offsetof(WKUWPCompositorOptions, header) == 0, "WKUWPCompositorOptions.header offset");
_Static_assert(offsetof(WKUWPCompositorOptions, panelInspectable) == 8, "WKUWPCompositorOptions.panelInspectable offset");
_Static_assert(offsetof(WKUWPCompositorOptions, scaleX) == 12, "WKUWPCompositorOptions.scaleX offset");
_Static_assert(offsetof(WKUWPCompositorOptions, scaleY) == 20, "WKUWPCompositorOptions.scaleY offset");

/*
 * WKUWPEngineAPI.
 *
 * The 24 function slots are asserted individually because a reordered or dropped
 * slot is invisible to a size check and would silently call the wrong function.
 */
_Static_assert(sizeof(WKUWPEngineAPI) == 128, "WKUWPEngineAPI size");
_Static_assert(offsetof(WKUWPEngineAPI, header) == 0, "WKUWPEngineAPI.header offset");
_Static_assert(offsetof(WKUWPEngineAPI, abiMajor) == 8, "WKUWPEngineAPI.abiMajor offset");
_Static_assert(offsetof(WKUWPEngineAPI, abiMinor) == 12, "WKUWPEngineAPI.abiMinor offset");
_Static_assert(offsetof(WKUWPEngineAPI, capabilities) == 16, "WKUWPEngineAPI.capabilities offset");
_Static_assert(offsetof(WKUWPEngineAPI, buildId) == 24, "WKUWPEngineAPI.buildId offset");
_Static_assert(offsetof(WKUWPEngineAPI, initialize) == 32, "WKUWPEngineAPI.initialize offset");
_Static_assert(offsetof(WKUWPEngineAPI, service) == 36, "WKUWPEngineAPI.service offset");
_Static_assert(offsetof(WKUWPEngineAPI, setSuspended) == 40, "WKUWPEngineAPI.setSuspended offset");
_Static_assert(offsetof(WKUWPEngineAPI, createPage) == 44, "WKUWPEngineAPI.createPage offset");
_Static_assert(offsetof(WKUWPEngineAPI, closePage) == 48, "WKUWPEngineAPI.closePage offset");
_Static_assert(offsetof(WKUWPEngineAPI, setCallbacks) == 52, "WKUWPEngineAPI.setCallbacks offset");
_Static_assert(offsetof(WKUWPEngineAPI, detachCallbacks) == 56, "WKUWPEngineAPI.detachCallbacks offset");
_Static_assert(offsetof(WKUWPEngineAPI, navigate) == 60, "WKUWPEngineAPI.navigate offset");
_Static_assert(offsetof(WKUWPEngineAPI, loadHtml) == 64, "WKUWPEngineAPI.loadHtml offset");
_Static_assert(offsetof(WKUWPEngineAPI, goBack) == 68, "WKUWPEngineAPI.goBack offset");
_Static_assert(offsetof(WKUWPEngineAPI, goForward) == 72, "WKUWPEngineAPI.goForward offset");
_Static_assert(offsetof(WKUWPEngineAPI, reload) == 76, "WKUWPEngineAPI.reload offset");
_Static_assert(offsetof(WKUWPEngineAPI, stop) == 80, "WKUWPEngineAPI.stop offset");
_Static_assert(offsetof(WKUWPEngineAPI, getState) == 84, "WKUWPEngineAPI.getState offset");
_Static_assert(offsetof(WKUWPEngineAPI, proceedWithCertificateFailure) == 88, "WKUWPEngineAPI.proceedWithCertificateFailure offset");
_Static_assert(offsetof(WKUWPEngineAPI, sendInput) == 92, "WKUWPEngineAPI.sendInput offset");
_Static_assert(offsetof(WKUWPEngineAPI, setFocus) == 96, "WKUWPEngineAPI.setFocus offset");
_Static_assert(offsetof(WKUWPEngineAPI, resize) == 100, "WKUWPEngineAPI.resize offset");
_Static_assert(offsetof(WKUWPEngineAPI, paint) == 104, "WKUWPEngineAPI.paint offset");
_Static_assert(offsetof(WKUWPEngineAPI, attachCompositor) == 108, "WKUWPEngineAPI.attachCompositor offset");
_Static_assert(offsetof(WKUWPEngineAPI, detachCompositor) == 112, "WKUWPEngineAPI.detachCompositor offset");
_Static_assert(offsetof(WKUWPEngineAPI, renderCompositor) == 116, "WKUWPEngineAPI.renderCompositor offset");
_Static_assert(offsetof(WKUWPEngineAPI, beginShutdown) == 120, "WKUWPEngineAPI.beginShutdown offset");
_Static_assert(offsetof(WKUWPEngineAPI, pollShutdown) == 124, "WKUWPEngineAPI.pollShutdown offset");

/*
 * Calling-convention signature checks. These fail if a compiler ever treats
 * __stdcall differently here, which is the classic source of silent ABI drift
 * between a native module and a C++/CX module.
 */
typedef WKUWPResult(__stdcall* CheckGetEngineAPISignature)(uint32_t, uint32_t, uint64_t, WKUWPEngineAPI*);
_Static_assert(sizeof(&WKUWPGetEngineAPI) == sizeof(CheckGetEngineAPISignature),
    "WKUWPGetEngineAPI calling convention");
_Static_assert(sizeof(((WKUWPEngineAPI*)0)->initialize) == sizeof(CheckGetEngineAPISignature),
    "initialize calling convention");
_Static_assert(sizeof(((WKUWPPageCallbacks*)0)->notify) == sizeof(void(__stdcall*)(void*, const WKUWPPageEvent*)),
    "notify calling convention");
_Static_assert(sizeof(((WKUWPPageCallbacks*)0)->starting) == sizeof(WKUWPStarting),
    "starting calling convention");
_Static_assert(sizeof(((WKUWPPageCallbacks*)0)->retainContext) == sizeof(WKUWPRetainContext),
    "retainContext calling convention");
_Static_assert(sizeof(((WKUWPPlatformServices*)0)->requestClipboard) == sizeof(void(__stdcall*)(void*, WKUWPPageHandle,
        uint64_t, uint32_t, const WKUWPString*, WKUWPClipboardCompletion, void*)),
    "requestClipboard calling convention");