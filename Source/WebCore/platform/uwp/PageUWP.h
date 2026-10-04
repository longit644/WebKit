#pragma once

// Return true to allow the navigation, false to refuse it. Runs synchronously
// during the policy check on the engine thread; keep it short and reentrant.
typedef bool(__stdcall* WKNavigationPolicyCallback)(void* context, const char* utf8Url);

// Legacy first-page snapshot navigation entry point retained for RuntimeSmoke.
extern "C" __declspec(dllexport) unsigned __stdcall WKNavigatePageUWP(void*, const char* utf8, const wchar_t* caPath);
#include <stddef.h>

// Native bridge: all calls for a page belong to its creating engine thread.
// No WebCore/STL ownership crosses this ABI boundary.
typedef void(__stdcall* WKPageLogUWP)(const char*, unsigned long);

// Validation limits enforced on both sides of the ABI; wrapper and engine must agree.
enum UWPValidationLimit {
    // Largest control extent the compositor can present.
    UWPMaxPageDimension = 2048,

    // Largest paint buffer edge in physical pixels.
    UWPMaxPaintDimension = 4096,

    // Largest paint buffer stride in bytes.
    UWPMaxPaintStride = 16384,

    // Largest accepted device scale factor.
    UWPMaxDeviceScale = 8,

    // Bytes per pixel in the BGRA buffers crossing the ABI.
    UWPBytesPerBGRA = 4
};

// Default typography used when only packaged UWP fonts are installed.
constexpr unsigned UWPDefaultFontSize = 18;
constexpr char UWPDefaultFontFamily[] = "DejaVu Sans";

// Result codes returned by every WK*PageUWP entry point below; the wrapper
// maps them to HRESULTs. Zero is success.
enum WKPageResultUWP {
    // Success; the call completed.
    WKPageResultOkUWP = 0,

    // A handle, pointer, size or type argument was null or out of range.
    WKPageResultInvalidArgumentUWP = 1,

    // Page creation could not obtain a local main frame.
    WKPageResultNoMainFrameUWP = 4,

    // HTML loading found no active document loader.
    WKPageResultNoActiveDocumentLoaderUWP = 5,

    // Cairo or Skia could not create a paint surface for the caller buffer.
    WKPageResultNoPaintSurfaceUWP = 6,

    // The document writer could not begin an app-supplied HTML document.
    WKPageResultDocumentWriterBeginFailedUWP = 7,

    // The call arrived on a thread other than the creating engine thread.
    WKPageResultWrongThreadUWP = 8,

    // Requested compositor configuration cannot be rebuilt on this page.
    WKPageResultCompositorConfigurationUnsupportedUWP = 9,

    // Active compositor context could not be made current or rendered.
    WKPageResultCompositorUnavailableUWP = 10
};

// Boolean values written to diagnostic log fields; 0 is false and 1 is true.
enum WKPageLogBooleanUWP {
    // Log field is false or the operation did not succeed.
    WKPageLogFalseUWP = 0,

    // Log field is true or the operation succeeded.
    WKPageLogTrueUWP = 1
};

// Main-frame failure result shared by engine logging and the native host.
enum WKNavigationFailureUWP {
    // The load was stopped or superseded; no blocked page should be shown.
    WKNavigationFailureCancelledUWP = 1
};

// Pointer event kinds for WKPointerPageUWP, in tap-lifecycle order.
enum WKPointerEventTypeUWP {
    // Finger or mouse button went down; starts press tracking and capture.
    WKPointerEventPressUWP = 0,

    // Pointer moved; drives drag detection and kinetic velocity sampling.
    WKPointerEventMoveUWP = 1,

    // Button released; a press without drag synthesizes a tap click.
    WKPointerEventReleaseUWP = 2
};

// Modifier bits for the WKPointerPageUWP modifiers mask.
enum WKPointerModifierUWP {
    // No modifier keys are held during the event.
    WKPointerModifierNoneUWP = 0,

    // Shift key held during the event.
    WKPointerModifierShiftUWP = 1,

    // Control key held during the event.
    WKPointerModifierControlUWP = 2,

    // Alt key held during the event.
    WKPointerModifierAltUWP = 4
};

// Snapshot of viewport, scroll and history state for one page.
struct WKPageStateUWP {
    // Viewport size in CSS pixels driving layout.
    unsigned width, height;

    // Current scroll offset in CSS pixels.
    int scrollX, scrollY;

    // Full document size in CSS pixels; larger than the viewport when scrollable.
    int contentWidth, contentHeight;

    // Nonzero when session history has an entry behind the current one.
    unsigned canGoBack;

    // Nonzero when session history has an entry ahead of the current one.
    unsigned canGoForward;
};

// How the committed document URL changed without a new document load. Reported
// so the host distinguishes a real new document from a same-document change
// instead of inferring it by comparing URL strings.
enum WKDocumentChangeKindUWP {
    // No same-document change observed since the last commit.
    WKDocumentChangeKindNoneUWP = 0,

    // In-page anchor navigation changed only the fragment.
    WKDocumentChangeKindFragmentUWP = 1,

    // history.pushState() mutated the URL and added a history entry.
    WKDocumentChangeKindPushStateUWP = 2,

    // history.replaceState() mutated the URL without adding a history entry.
    WKDocumentChangeKindReplaceStateUWP = 3,

    // History traversal restored an existing same-document entry.
    WKDocumentChangeKindPopStateUWP = 4
};

// Typed document facts captured from the real load lifecycle. Hosts read these
// instead of parsing diagnostic log lines. Field order is ABI; append only.
struct WKPageDocumentStateUWP {
    // Committed document title; empty when the document reported none. The
    // storage is owned by the page and stays valid until the next commit on
    // the same engine thread, so copy it before returning to other code.
    const char* title;

    // HTTP status of the committed main-document response. Meaningful only
    // when hasHttpStatus is nonzero; values follow WebKit's HTTP status
    // constants. A status of 404 with an HTML body is a completed document,
    // not a transport failure.
    unsigned httpStatus;

    // Nonzero once a main-document response supplied a status.
    unsigned hasHttpStatus;

    // Latest same-document change kind; a WKDocumentChangeKindUWP value.
    unsigned documentChange;

    // Server redirect hops observed for the active navigation, including the
    // final hop that delivered the committed document.
    unsigned redirectHops;
};

extern "C" {
#if defined(BUILDING_WebCore)
#define WK_PAGE_UWP_EXPORT __declspec(dllexport)
#else
#define WK_PAGE_UWP_EXPORT
#endif
// Starts the shared engine once per process; fonts is the staged font directory.
WK_PAGE_UWP_EXPORT unsigned __stdcall WKInitializeEngineUWP(const wchar_t* fonts, WKPageLogUWP);

// Creates one page at the placeholder extent; result carries the WKPageResultUWP code.
WK_PAGE_UWP_EXPORT void* __stdcall WKCreatePageUWP(const wchar_t* fonts, unsigned width, unsigned height, WKPageLogUWP, unsigned* result);

// Tears down the page created above; null-safe.
WK_PAGE_UWP_EXPORT unsigned __stdcall WKDestroyPageUWP(void*);

// Loads app-supplied markup; never touches history.
WK_PAGE_UWP_EXPORT unsigned __stdcall WKLoadHTMLUWP(void*, const char* utf8);

// Starts an async http(s) navigation verified against the given CA bundle.
WK_PAGE_UWP_EXPORT unsigned __stdcall WKNavigatePageUWP(void*, const char* utf8, const wchar_t* caPath);

// Steps back in session history.
WK_PAGE_UWP_EXPORT unsigned __stdcall WKGoBackPageUWP(void*);

// Steps forward in session history.
WK_PAGE_UWP_EXPORT unsigned __stdcall WKGoForwardPageUWP(void*);

// Re-requests the current document.
WK_PAGE_UWP_EXPORT unsigned __stdcall WKReloadPageUWP(void*);

// Cancels the in-flight load.
WK_PAGE_UWP_EXPORT unsigned __stdcall WKStopPageUWP(void*);

// Installs the host veto consulted synchronously at policy time.
WK_PAGE_UWP_EXPORT unsigned __stdcall WKSetNavigationPolicyHandlerPageUWP(void*, WKNavigationPolicyCallback, void* context);

// Rebuilds the backing store at the new control size.
WK_PAGE_UWP_EXPORT unsigned __stdcall WKResizePageUWP(void*, unsigned width, unsigned height);

// Scrolls the page by the given delta.
WK_PAGE_UWP_EXPORT unsigned __stdcall WKScrollPageUWP(void*, int dx, int dy);

// Delivers a press/move/release event with a modifier mask.
WK_PAGE_UWP_EXPORT unsigned __stdcall WKPointerPageUWP(void*, unsigned type, double x, double y, unsigned modifiers);

// Runs timers, inertia and network pump for one frame.
WK_PAGE_UWP_EXPORT unsigned __stdcall WKTickPageUWP(void*);

// Paints unscaled page pixels into the caller buffer.
WK_PAGE_UWP_EXPORT unsigned __stdcall WKPaintPageUWP(void*, unsigned char* bgra, size_t capacity, unsigned stride);

// Paints with a DPI scale applied for high-density displays.
WK_PAGE_UWP_EXPORT unsigned __stdcall WKPaintPageScaledUWP(void*, unsigned char* bgra, size_t capacity, unsigned stride, double scaleX, double scaleY);

// Reports whether a repaint is pending.
WK_PAGE_UWP_EXPORT unsigned __stdcall WKPageNeedsPaintUWP(void*, unsigned* needsPaint);

// Snapshots scroll, content and history state.
WK_PAGE_UWP_EXPORT unsigned __stdcall WKGetPageStateUWP(void*, WKPageStateUWP*);

// Copies typed title, HTTP status, same-document change kind and redirect
// count captured from the loader client. On a nonzero result the caller struct
// is untouched and no field may be read.
WK_PAGE_UWP_EXPORT unsigned __stdcall WKGetPageDocumentStateUWP(void*, WKPageDocumentStateUWP*);

// Starts the GPU compositor on the given SwapChainPanel.
WK_PAGE_UWP_EXPORT unsigned __stdcall WKEnableCompositorUWP(void*, void* nativePanel, double scaleX, double scaleY);

// Stops the GPU compositor path.
WK_PAGE_UWP_EXPORT unsigned __stdcall WKDisableCompositorUWP(void*);

// Presents one compositor frame with the given overscroll offset.
WK_PAGE_UWP_EXPORT unsigned __stdcall WKRenderCompositorUWP(void*, double overscrollY);

// Returns an HRESULT; dispatcher is an IInspectable exposing ICoreDispatcher.
WK_PAGE_UWP_EXPORT long __stdcall WKInstallClipboardDispatcherUWP(void*, void* dispatcher);
#undef WK_PAGE_UWP_EXPORT
}
