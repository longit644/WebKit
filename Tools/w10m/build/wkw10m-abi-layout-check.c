/*
 * wkw10m-abi-layout-check.c - proves the frozen V1 ABI layout is identical for the
 * native engine compiler and the MSVC C++/CX compiler on ARM32.
 *
 * Every assertion here restates a row of the structure table in
 * openspec/changes/wkview-m10-w10m-cppcx-restructure/artifact/native-abi.md. If a
 * compiler disagrees about any size, offset or enumeration width, this translation
 * unit fails to compile and the build stops before any producer or consumer is
 * written against the wrong layout.
 *
 * The file is compiled for ARM32 by Tools/w10m/build/check-wkw10m-abi-layout.ps1
 * with both the native clang-cl driver and the MSVC C compiler used for
 * C++/CX. It is a compile-only conformance check; it is never linked into the
 * engine, the component or the app.
 */

#include "WKW10MEngine.h"
#include "WKW10MPage.h"
#include "WKW10MNavigation.h"
#include "WKW10MInput.h"
#include "WKW10MRendering.h"

#include <stddef.h>

/*
 * MSVC accepts _Static_assert only as C, while the framework consumer compiles
 * these headers as C++, where the spelling is static_assert. Both spellings must
 * carry the same assertions, because a divergence that only shows up in one
 * language is exactly how a frozen structure ends up with two meanings.
 */
#if defined(__cplusplus)
#define WKW10M_STATIC_ASSERT static_assert
#else
#define WKW10M_STATIC_ASSERT _Static_assert
#endif

/* ARM32 is the only supported boundary architecture. */
WKW10M_STATIC_ASSERT(sizeof(void*) == 4, "WKW10M ABI is defined for little-endian ARM32 pointers only");

/* Primitives. */
WKW10M_STATIC_ASSERT(sizeof(uint8_t) == 1, "uint8_t width");
WKW10M_STATIC_ASSERT(sizeof(uint32_t) == 4, "uint32_t width");
WKW10M_STATIC_ASSERT(sizeof(int32_t) == 4, "int32_t width");
WKW10M_STATIC_ASSERT(sizeof(uint64_t) == 8, "uint64_t width");
WKW10M_STATIC_ASSERT(sizeof(double) == 8, "double width");

/* Enumerations are specified as 32-bit values, never compiler-chosen widths. */
WKW10M_STATIC_ASSERT(sizeof(WKW10MResult) == 4, "WKW10MResult width");
WKW10M_STATIC_ASSERT(sizeof(WKW10MCapability) == 4, "WKW10MCapability width");
WKW10M_STATIC_ASSERT(sizeof(WKW10MClipboardKind) == 4, "WKW10MClipboardKind width");
WKW10M_STATIC_ASSERT(sizeof(WKW10MContentKind) == 4, "WKW10MContentKind width");
WKW10M_STATIC_ASSERT(sizeof(WKW10MOutcome) == 4, "WKW10MOutcome width");
WKW10M_STATIC_ASSERT(sizeof(WKW10MErrorDomain) == 4, "WKW10MErrorDomain width");
WKW10M_STATIC_ASSERT(sizeof(WKW10MChangeKind) == 4, "WKW10MChangeKind width");
WKW10M_STATIC_ASSERT(sizeof(WKW10MEventKind) == 4, "WKW10MEventKind width");
WKW10M_STATIC_ASSERT(sizeof(WKW10MInputKind) == 4, "WKW10MInputKind width");
WKW10M_STATIC_ASSERT(sizeof(WKW10MModifier) == 4, "WKW10MModifier width");

/* Enumeration values are frozen; renumbering them is an ABI break. */
WKW10M_STATIC_ASSERT(WKW10MResultOK == 0, "WKW10MResultOK value");
WKW10M_STATIC_ASSERT(WKW10MResultInvalidArgument == 1, "WKW10MResultInvalidArgument value");
WKW10M_STATIC_ASSERT(WKW10MResultNoMainFrame == 4, "WKW10MResultNoMainFrame value");
WKW10M_STATIC_ASSERT(WKW10MResultNoActiveDocumentLoader == 5, "WKW10MResultNoActiveDocumentLoader value");
WKW10M_STATIC_ASSERT(WKW10MResultNoPaintSurface == 6, "WKW10MResultNoPaintSurface value");
WKW10M_STATIC_ASSERT(WKW10MResultDocumentWriterBeginFailed == 7, "WKW10MResultDocumentWriterBeginFailed value");
WKW10M_STATIC_ASSERT(WKW10MResultWrongThread == 8, "WKW10MResultWrongThread value");
WKW10M_STATIC_ASSERT(WKW10MResultCompositorConfigurationUnsupported == 9, "WKW10MResultCompositorConfigurationUnsupported value");
WKW10M_STATIC_ASSERT(WKW10MResultCompositorUnavailable == 10, "WKW10MResultCompositorUnavailable value");
WKW10M_STATIC_ASSERT(WKW10MResultClosed == 16, "WKW10MResultClosed value");
WKW10M_STATIC_ASSERT(WKW10MResultAbiMismatch == 17, "WKW10MResultAbiMismatch value");
WKW10M_STATIC_ASSERT(WKW10MResultMissingCapability == 18, "WKW10MResultMissingCapability value");
WKW10M_STATIC_ASSERT(WKW10MResultInvalidState == 19, "WKW10MResultInvalidState value");
WKW10M_STATIC_ASSERT(WKW10MResultPending == 20, "WKW10MResultPending value");
WKW10M_STATIC_ASSERT(WKW10MResultBufferTooSmall == 21, "WKW10MResultBufferTooSmall value");
WKW10M_STATIC_ASSERT(WKW10MResultUnsupported == 22, "WKW10MResultUnsupported value");
WKW10M_STATIC_ASSERT(WKW10MResultBudgetExceeded == 23, "WKW10MResultBudgetExceeded value");
WKW10M_STATIC_ASSERT(WKW10MResultShuttingDown == 24, "WKW10MResultShuttingDown value");
WKW10M_STATIC_ASSERT(WKW10MResultConfigurationConflict == 25, "WKW10MResultConfigurationConflict value");
WKW10M_STATIC_ASSERT(WKW10MResultInvalidCertificateToken == 26, "WKW10MResultInvalidCertificateToken value");
WKW10M_STATIC_ASSERT(WKW10MResultNotInitialized == 27, "WKW10MResultNotInitialized value");
WKW10M_STATIC_ASSERT(WKW10MResultDispatcherUnavailable == 28, "WKW10MResultDispatcherUnavailable value");

WKW10M_STATIC_ASSERT(WKW10MCapabilityStructuredNotifications == 0x001, "StructuredNotifications bit");
WKW10M_STATIC_ASSERT(WKW10MCapabilityHostHtml == 0x002, "HostHtml bit");
WKW10M_STATIC_ASSERT(WKW10MCapabilityIndependentSessions == 0x004, "IndependentSessions bit");
WKW10M_STATIC_ASSERT(WKW10MCapabilityCPUPaint == 0x008, "CPUPaint bit");
WKW10M_STATIC_ASSERT(WKW10MCapabilityGPUCompositor == 0x010, "GPUCompositor bit");
WKW10M_STATIC_ASSERT(WKW10MCapabilityClipboardService == 0x020, "ClipboardService bit");
WKW10M_STATIC_ASSERT(WKW10MCapabilityCertificateDecisions == 0x040, "CertificateDecisions bit");
WKW10M_STATIC_ASSERT(WKW10MCapabilityInput == 0x080, "Input bit");
WKW10M_STATIC_ASSERT(WKW10MCapabilityImmutableTrustPolicy == 0x100, "ImmutableTrustPolicy bit");
WKW10M_STATIC_ASSERT(WKW10MCapabilityGlobalFontConfiguration == 0x200, "GlobalFontConfiguration bit");

WKW10M_STATIC_ASSERT(WKW10MClipboardRead == 0, "ClipboardRead value");
WKW10M_STATIC_ASSERT(WKW10MClipboardWrite == 1, "ClipboardWrite value");
WKW10M_STATIC_ASSERT(WKW10MContentRemote == 0, "ContentRemote value");
WKW10M_STATIC_ASSERT(WKW10MContentHostHtml == 1, "ContentHostHtml value");
WKW10M_STATIC_ASSERT(WKW10MOutcomeCompleted == 0, "OutcomeCompleted value");
WKW10M_STATIC_ASSERT(WKW10MOutcomeFailed == 1, "OutcomeFailed value");
WKW10M_STATIC_ASSERT(WKW10MOutcomeCancelled == 2, "OutcomeCancelled value");
WKW10M_STATIC_ASSERT(WKW10MErrorDomainNone == 0, "ErrorDomainNone value");
WKW10M_STATIC_ASSERT(WKW10MErrorDomainPlatform == 4, "ErrorDomainPlatform value");
WKW10M_STATIC_ASSERT(WKW10MChangeNewDocument == 0, "ChangeNewDocument value");
WKW10M_STATIC_ASSERT(WKW10MChangeHistoryTraversal == 4, "ChangeHistoryTraversal value");
WKW10M_STATIC_ASSERT(WKW10MEventStarting == 0, "EventStarting value");
WKW10M_STATIC_ASSERT(WKW10MEventRepaintRequested == 5, "EventRepaintRequested value");
WKW10M_STATIC_ASSERT(WKW10MInputKeyDown == 0, "InputKeyDown value");
WKW10M_STATIC_ASSERT(WKW10MInputWheel == 5, "InputWheel value");
WKW10M_STATIC_ASSERT(WKW10MModifierAlt == 0x0001, "ModifierAlt value");
WKW10M_STATIC_ASSERT(WKW10MModifierWindows == 0x0008, "ModifierWindows value");

/* Scalars. */
WKW10M_STATIC_ASSERT(sizeof(WKW10MPageHandle) == 8, "WKW10MPageHandle width");
WKW10M_STATIC_ASSERT(WKW10M_ABI_MAJOR_VERSION == 1, "ABI major version");
WKW10M_STATIC_ASSERT(WKW10M_ABI_MINOR_VERSION == 0, "ABI minor version");
WKW10M_STATIC_ASSERT(WKW10M_STRUCTURE_VERSION == 1, "structure version");
WKW10M_STATIC_ASSERT(WKW10M_ENGINE_API_STRUCTURE_SIZE == 128, "engine API structure size");

/* WKW10MHeader. */
WKW10M_STATIC_ASSERT(sizeof(WKW10MHeader) == 8, "WKW10MHeader size");
WKW10M_STATIC_ASSERT(offsetof(WKW10MHeader, structureSize) == 0, "WKW10MHeader.structureSize offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MHeader, structureVersion) == 4, "WKW10MHeader.structureVersion offset");

/* WKW10MString. */
WKW10M_STATIC_ASSERT(sizeof(WKW10MString) == 8, "WKW10MString size");
WKW10M_STATIC_ASSERT(offsetof(WKW10MString, data) == 0, "WKW10MString.data offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MString, length) == 4, "WKW10MString.length offset");

/* WKW10MPath. */
WKW10M_STATIC_ASSERT(sizeof(WKW10MPath) == 8, "WKW10MPath size");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPath, data) == 0, "WKW10MPath.data offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPath, length) == 4, "WKW10MPath.length offset");

/* WKW10MCertificateToken. */
WKW10M_STATIC_ASSERT(sizeof(WKW10MCertificateToken) == 16, "WKW10MCertificateToken size");
WKW10M_STATIC_ASSERT(offsetof(WKW10MCertificateToken, issuer) == 0, "WKW10MCertificateToken.issuer offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MCertificateToken, decision) == 8, "WKW10MCertificateToken.decision offset");

/* WKW10MPlatformServices. */
WKW10M_STATIC_ASSERT(sizeof(WKW10MPlatformServices) == 32, "WKW10MPlatformServices size");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPlatformServices, header) == 0, "WKW10MPlatformServices.header offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPlatformServices, context) == 8, "WKW10MPlatformServices.context offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPlatformServices, retainContext) == 12, "WKW10MPlatformServices.retainContext offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPlatformServices, releaseContext) == 16, "WKW10MPlatformServices.releaseContext offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPlatformServices, requestClipboard) == 20, "WKW10MPlatformServices.requestClipboard offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPlatformServices, cancelClipboard) == 24, "WKW10MPlatformServices.cancelClipboard offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPlatformServices, postWake) == 28, "WKW10MPlatformServices.postWake offset");

/* WKW10MEngineOptions. */
WKW10M_STATIC_ASSERT(sizeof(WKW10MEngineOptions) == 28, "WKW10MEngineOptions size");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineOptions, header) == 0, "WKW10MEngineOptions.header offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineOptions, fontDirectory) == 8, "WKW10MEngineOptions.fontDirectory offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineOptions, fontFingerprint) == 16, "WKW10MEngineOptions.fontFingerprint offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineOptions, platformServices) == 24, "WKW10MEngineOptions.platformServices offset");

/* WKW10MPageOptions. */
WKW10M_STATIC_ASSERT(sizeof(WKW10MPageOptions) == 56, "WKW10MPageOptions size");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageOptions, header) == 0, "WKW10MPageOptions.header offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageOptions, caBundle) == 8, "WKW10MPageOptions.caBundle offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageOptions, trustFingerprint) == 16, "WKW10MPageOptions.trustFingerprint offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageOptions, maximumConcurrentRequests) == 24, "WKW10MPageOptions.maximumConcurrentRequests offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageOptions, maximumResourceBytes) == 28, "WKW10MPageOptions.maximumResourceBytes offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageOptions, maximumBufferedBytes) == 36, "WKW10MPageOptions.maximumBufferedBytes offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageOptions, requestTimeoutMilliseconds) == 44, "WKW10MPageOptions.requestTimeoutMilliseconds offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageOptions, maximumQueuedRequests) == 48, "WKW10MPageOptions.maximumQueuedRequests offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageOptions, maximumRedirects) == 52, "WKW10MPageOptions.maximumRedirects offset");

/* WKW10MPageState. */
WKW10M_STATIC_ASSERT(sizeof(WKW10MPageState) == 92, "WKW10MPageState size");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageState, header) == 0, "WKW10MPageState.header offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageState, pageId) == 8, "WKW10MPageState.pageId offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageState, documentGeneration) == 16, "WKW10MPageState.documentGeneration offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageState, activeNavigationId) == 24, "WKW10MPageState.activeNavigationId offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageState, canGoBack) == 32, "WKW10MPageState.canGoBack offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageState, canGoForward) == 36, "WKW10MPageState.canGoForward offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageState, isLoading) == 40, "WKW10MPageState.isLoading offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageState, widthCssPixels) == 44, "WKW10MPageState.widthCssPixels offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageState, heightCssPixels) == 48, "WKW10MPageState.heightCssPixels offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageState, scrollX) == 52, "WKW10MPageState.scrollX offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageState, scrollY) == 56, "WKW10MPageState.scrollY offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageState, contentWidth) == 60, "WKW10MPageState.contentWidth offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageState, contentHeight) == 64, "WKW10MPageState.contentHeight offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageState, uri) == 68, "WKW10MPageState.uri offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageState, title) == 76, "WKW10MPageState.title offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageState, hasHttpStatus) == 84, "WKW10MPageState.hasHttpStatus offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageState, httpStatus) == 88, "WKW10MPageState.httpStatus offset");

/* WKW10MNavigationRequest. */
WKW10M_STATIC_ASSERT(sizeof(WKW10MNavigationRequest) == 44, "WKW10MNavigationRequest size");
WKW10M_STATIC_ASSERT(offsetof(WKW10MNavigationRequest, header) == 0, "WKW10MNavigationRequest.header offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MNavigationRequest, contentKind) == 8, "WKW10MNavigationRequest.contentKind offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MNavigationRequest, uri) == 12, "WKW10MNavigationRequest.uri offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MNavigationRequest, html) == 20, "WKW10MNavigationRequest.html offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MNavigationRequest, warningToken) == 28, "WKW10MNavigationRequest.warningToken offset");

/* WKW10MPageEvent. */
WKW10M_STATIC_ASSERT(sizeof(WKW10MPageEvent) == 116, "WKW10MPageEvent size");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageEvent, header) == 0, "WKW10MPageEvent.header offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageEvent, pageId) == 8, "WKW10MPageEvent.pageId offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageEvent, callbackSequence) == 16, "WKW10MPageEvent.callbackSequence offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageEvent, navigationId) == 24, "WKW10MPageEvent.navigationId offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageEvent, documentGeneration) == 32, "WKW10MPageEvent.documentGeneration offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageEvent, eventKind) == 40, "WKW10MPageEvent.eventKind offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageEvent, contentKind) == 44, "WKW10MPageEvent.contentKind offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageEvent, outcome) == 48, "WKW10MPageEvent.outcome offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageEvent, errorDomain) == 52, "WKW10MPageEvent.errorDomain offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageEvent, errorCode) == 56, "WKW10MPageEvent.errorCode offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageEvent, httpStatus) == 60, "WKW10MPageEvent.httpStatus offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageEvent, hasHttpStatus) == 64, "WKW10MPageEvent.hasHttpStatus offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageEvent, changeKind) == 68, "WKW10MPageEvent.changeKind offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageEvent, canGoBack) == 72, "WKW10MPageEvent.canGoBack offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageEvent, canGoForward) == 76, "WKW10MPageEvent.canGoForward offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageEvent, isLoading) == 80, "WKW10MPageEvent.isLoading offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageEvent, uri) == 84, "WKW10MPageEvent.uri offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageEvent, title) == 92, "WKW10MPageEvent.title offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageEvent, certificateToken) == 100, "WKW10MPageEvent.certificateToken offset");

/* WKW10MPageCallbacks. */
WKW10M_STATIC_ASSERT(sizeof(WKW10MPageCallbacks) == 28, "WKW10MPageCallbacks size");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageCallbacks, header) == 0, "WKW10MPageCallbacks.header offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageCallbacks, context) == 8, "WKW10MPageCallbacks.context offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageCallbacks, retainContext) == 12, "WKW10MPageCallbacks.retainContext offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageCallbacks, releaseContext) == 16, "WKW10MPageCallbacks.releaseContext offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageCallbacks, starting) == 20, "WKW10MPageCallbacks.starting offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPageCallbacks, notify) == 24, "WKW10MPageCallbacks.notify offset");

/* WKW10MInputEvent. */
WKW10M_STATIC_ASSERT(sizeof(WKW10MInputEvent) == 52, "WKW10MInputEvent size");
WKW10M_STATIC_ASSERT(offsetof(WKW10MInputEvent, header) == 0, "WKW10MInputEvent.header offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MInputEvent, kind) == 8, "WKW10MInputEvent.kind offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MInputEvent, modifiers) == 12, "WKW10MInputEvent.modifiers offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MInputEvent, pointerId) == 16, "WKW10MInputEvent.pointerId offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MInputEvent, virtualKey) == 20, "WKW10MInputEvent.virtualKey offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MInputEvent, unicodeScalar) == 24, "WKW10MInputEvent.unicodeScalar offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MInputEvent, xCssPixels) == 28, "WKW10MInputEvent.xCssPixels offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MInputEvent, yCssPixels) == 36, "WKW10MInputEvent.yCssPixels offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MInputEvent, wheelDeltaX) == 44, "WKW10MInputEvent.wheelDeltaX offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MInputEvent, wheelDeltaY) == 48, "WKW10MInputEvent.wheelDeltaY offset");

/* WKW10MPaintBuffer. */
WKW10M_STATIC_ASSERT(sizeof(WKW10MPaintBuffer) == 32, "WKW10MPaintBuffer size");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPaintBuffer, header) == 0, "WKW10MPaintBuffer.header offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPaintBuffer, data) == 8, "WKW10MPaintBuffer.data offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPaintBuffer, capacityBytes) == 12, "WKW10MPaintBuffer.capacityBytes offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPaintBuffer, strideBytes) == 20, "WKW10MPaintBuffer.strideBytes offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPaintBuffer, widthPixels) == 24, "WKW10MPaintBuffer.widthPixels offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MPaintBuffer, heightPixels) == 28, "WKW10MPaintBuffer.heightPixels offset");

/* WKW10MCompositorOptions. */
WKW10M_STATIC_ASSERT(sizeof(WKW10MCompositorOptions) == 28, "WKW10MCompositorOptions size");
WKW10M_STATIC_ASSERT(offsetof(WKW10MCompositorOptions, header) == 0, "WKW10MCompositorOptions.header offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MCompositorOptions, panelInspectable) == 8, "WKW10MCompositorOptions.panelInspectable offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MCompositorOptions, scaleX) == 12, "WKW10MCompositorOptions.scaleX offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MCompositorOptions, scaleY) == 20, "WKW10MCompositorOptions.scaleY offset");

/*
 * WKW10MEngineAPI.
 *
 * The 24 function slots are asserted individually because a reordered or dropped
 * slot is invisible to a size check and would silently call the wrong function.
 */
WKW10M_STATIC_ASSERT(sizeof(WKW10MEngineAPI) == 128, "WKW10MEngineAPI size");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, header) == 0, "WKW10MEngineAPI.header offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, abiMajor) == 8, "WKW10MEngineAPI.abiMajor offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, abiMinor) == 12, "WKW10MEngineAPI.abiMinor offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, capabilities) == 16, "WKW10MEngineAPI.capabilities offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, buildId) == 24, "WKW10MEngineAPI.buildId offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, initialize) == 32, "WKW10MEngineAPI.initialize offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, service) == 36, "WKW10MEngineAPI.service offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, setSuspended) == 40, "WKW10MEngineAPI.setSuspended offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, createPage) == 44, "WKW10MEngineAPI.createPage offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, closePage) == 48, "WKW10MEngineAPI.closePage offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, setCallbacks) == 52, "WKW10MEngineAPI.setCallbacks offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, detachCallbacks) == 56, "WKW10MEngineAPI.detachCallbacks offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, navigate) == 60, "WKW10MEngineAPI.navigate offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, loadHtml) == 64, "WKW10MEngineAPI.loadHtml offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, goBack) == 68, "WKW10MEngineAPI.goBack offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, goForward) == 72, "WKW10MEngineAPI.goForward offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, reload) == 76, "WKW10MEngineAPI.reload offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, stop) == 80, "WKW10MEngineAPI.stop offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, getState) == 84, "WKW10MEngineAPI.getState offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, proceedWithCertificateFailure) == 88, "WKW10MEngineAPI.proceedWithCertificateFailure offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, sendInput) == 92, "WKW10MEngineAPI.sendInput offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, setFocus) == 96, "WKW10MEngineAPI.setFocus offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, resize) == 100, "WKW10MEngineAPI.resize offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, paint) == 104, "WKW10MEngineAPI.paint offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, attachCompositor) == 108, "WKW10MEngineAPI.attachCompositor offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, detachCompositor) == 112, "WKW10MEngineAPI.detachCompositor offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, renderCompositor) == 116, "WKW10MEngineAPI.renderCompositor offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, beginShutdown) == 120, "WKW10MEngineAPI.beginShutdown offset");
WKW10M_STATIC_ASSERT(offsetof(WKW10MEngineAPI, pollShutdown) == 124, "WKW10MEngineAPI.pollShutdown offset");

/*
 * Calling-convention signature checks. These fail if a compiler ever treats
 * __stdcall differently here, which is the classic source of silent ABI drift
 * between a native module and a C++/CX module.
 */
typedef WKW10MResult(__stdcall* CheckGetEngineAPISignature)(uint32_t, uint32_t, uint64_t, WKW10MEngineAPI*);
WKW10M_STATIC_ASSERT(sizeof(&WKW10MGetEngineAPI) == sizeof(CheckGetEngineAPISignature),
    "WKW10MGetEngineAPI calling convention");
WKW10M_STATIC_ASSERT(sizeof(((WKW10MEngineAPI*)0)->initialize) == sizeof(CheckGetEngineAPISignature),
    "initialize calling convention");
WKW10M_STATIC_ASSERT(sizeof(((WKW10MPageCallbacks*)0)->notify) == sizeof(void(__stdcall*)(void*, const WKW10MPageEvent*)),
    "notify calling convention");
WKW10M_STATIC_ASSERT(sizeof(((WKW10MPageCallbacks*)0)->starting) == sizeof(WKW10MStarting),
    "starting calling convention");
WKW10M_STATIC_ASSERT(sizeof(((WKW10MPageCallbacks*)0)->retainContext) == sizeof(WKW10MRetainContext),
    "retainContext calling convention");
WKW10M_STATIC_ASSERT(sizeof(((WKW10MPlatformServices*)0)->requestClipboard) == sizeof(void(__stdcall*)(void*, WKW10MPageHandle,
        uint64_t, uint32_t, const WKW10MString*, WKW10MClipboardCompletion, void*)),
    "requestClipboard calling convention");
