#pragma once

// Return true to allow the navigation, false to refuse it. Runs synchronously
// during the policy check on the engine thread; keep it short and reentrant.
typedef bool(__stdcall* WKNavigationPolicyCallback)(void* context, const char* utf8Url);

// Legacy first-page snapshot navigation entry point retained for RuntimeSmoke.
extern "C" __declspec(dllexport) unsigned __stdcall WKNavigatePageW10M(void*, const char* utf8, const wchar_t* caPath);
#include <stddef.h>

// Native bridge: all calls for a page belong to its creating engine thread.
// No WebCore/STL ownership crosses this ABI boundary.
typedef void(__stdcall* WKPageLogW10M)(const char*, unsigned long);

// Validation limits enforced on both sides of the ABI; wrapper and engine must agree.
enum W10MValidationLimit {
    // Largest control extent the compositor can present.
    W10MMaxPageDimension = 2048,

    // Largest paint buffer edge in physical pixels.
    W10MMaxPaintDimension = 4096,

    // Largest paint buffer stride in bytes.
    W10MMaxPaintStride = 16384,

    // Largest accepted device scale factor.
    W10MMaxDeviceScale = 8,

    // Bytes per pixel in the BGRA buffers crossing the ABI.
    W10MBytesPerBGRA = 4
};

// Default typography used when only packaged W10M fonts are installed.
constexpr unsigned W10MDefaultFontSize = 18;
constexpr char W10MDefaultFontFamily[] = "DejaVu Sans";

// Result codes returned by every WK*PageW10M entry point below; the wrapper
// maps them to HRESULTs. Zero is success.
enum WKPageResultW10M {
    // Success; the call completed.
    WKPageResultOkW10M = 0,

    // A handle, pointer, size or type argument was null or out of range.
    WKPageResultInvalidArgumentW10M = 1,

    // Page creation could not obtain a local main frame.
    WKPageResultNoMainFrameW10M = 4,

    // HTML loading found no active document loader.
    WKPageResultNoActiveDocumentLoaderW10M = 5,

    // Cairo or Skia could not create a paint surface for the caller buffer.
    WKPageResultNoPaintSurfaceW10M = 6,

    // The document writer could not begin an app-supplied HTML document.
    WKPageResultDocumentWriterBeginFailedW10M = 7,

    // The call arrived on a thread other than the creating engine thread.
    WKPageResultWrongThreadW10M = 8,

    // Requested compositor configuration cannot be rebuilt on this page.
    WKPageResultCompositorConfigurationUnsupportedW10M = 9,

    // Active compositor context could not be made current or rendered.
    WKPageResultCompositorUnavailableW10M = 10
};

// Boolean values written to diagnostic log fields; 0 is false and 1 is true.
enum WKPageLogBooleanW10M {
    // Log field is false or the operation did not succeed.
    WKPageLogFalseW10M = 0,

    // Log field is true or the operation succeeded.
    WKPageLogTrueW10M = 1
};

// Main-frame failure result shared by engine logging and the native host.
enum WKNavigationFailureW10M {
    // The load was stopped or superseded; no blocked page should be shown.
    WKNavigationFailureCancelledW10M = 1
};

// Pointer event kinds for WKPointerPageW10M, in tap-lifecycle order.
enum WKPointerEventTypeW10M {
    // Finger or mouse button went down; starts press tracking and capture.
    WKPointerEventPressW10M = 0,

    // Pointer moved; drives drag detection and kinetic velocity sampling.
    WKPointerEventMoveW10M = 1,

    // Button released; a press without drag synthesizes a tap click.
    WKPointerEventReleaseW10M = 2
};

// Modifier bits for the WKPointerPageW10M modifiers mask.
enum WKPointerModifierW10M {
    // No modifier keys are held during the event.
    WKPointerModifierNoneW10M = 0,

    // Shift key held during the event.
    WKPointerModifierShiftW10M = 1,

    // Control key held during the event.
    WKPointerModifierControlW10M = 2,

    // Alt key held during the event.
    WKPointerModifierAltW10M = 4
};

// Snapshot of viewport, scroll and history state for one page.
struct WKPageStateW10M {
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
enum WKDocumentChangeKindW10M {
    // No same-document change observed since the last commit.
    WKDocumentChangeKindNoneW10M = 0,

    // In-page anchor navigation changed only the fragment.
    WKDocumentChangeKindFragmentW10M = 1,

    // history.pushState() mutated the URL and added a history entry.
    WKDocumentChangeKindPushStateW10M = 2,

    // history.replaceState() mutated the URL without adding a history entry.
    WKDocumentChangeKindReplaceStateW10M = 3,

    // History traversal restored an existing same-document entry.
    WKDocumentChangeKindPopStateW10M = 4
};

// Typed document facts captured from the real load lifecycle. Hosts read these
// instead of parsing diagnostic log lines. Field order is ABI; append only.
struct WKPageDocumentStateW10M {
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

    // Latest same-document change kind; a WKDocumentChangeKindW10M value.
    unsigned documentChange;

    // Server redirect hops observed for the active navigation, including the
    // final hop that delivered the committed document.
    unsigned redirectHops;
};

extern "C" {
#if defined(BUILDING_WebCore)
#define WK_PAGE_W10M_EXPORT __declspec(dllexport)
#else
#define WK_PAGE_W10M_EXPORT
#endif
// Starts the shared engine once per process; fonts is the staged font directory.
WK_PAGE_W10M_EXPORT unsigned __stdcall WKInitializeEngineW10M(const wchar_t* fonts, WKPageLogW10M);

// Creates one page at the placeholder extent; result carries the WKPageResultW10M code.
WK_PAGE_W10M_EXPORT void* __stdcall WKCreatePageW10M(const wchar_t* fonts, unsigned width, unsigned height, WKPageLogW10M, unsigned* result);

// Tears down the page created above; null-safe.
WK_PAGE_W10M_EXPORT unsigned __stdcall WKDestroyPageW10M(void*);

// Loads app-supplied markup; never touches history.
WK_PAGE_W10M_EXPORT unsigned __stdcall WKLoadHTMLW10M(void*, const char* utf8);

// Starts an async http(s) navigation verified against the given CA bundle.
WK_PAGE_W10M_EXPORT unsigned __stdcall WKNavigatePageW10M(void*, const char* utf8, const wchar_t* caPath);

// Steps back in session history.
WK_PAGE_W10M_EXPORT unsigned __stdcall WKGoBackPageW10M(void*);

// Steps forward in session history.
WK_PAGE_W10M_EXPORT unsigned __stdcall WKGoForwardPageW10M(void*);

// Re-requests the current document.
WK_PAGE_W10M_EXPORT unsigned __stdcall WKReloadPageW10M(void*);

// Cancels the in-flight load.
WK_PAGE_W10M_EXPORT unsigned __stdcall WKStopPageW10M(void*);

// Installs the host veto consulted synchronously at policy time.
WK_PAGE_W10M_EXPORT unsigned __stdcall WKSetNavigationPolicyHandlerPageW10M(void*, WKNavigationPolicyCallback, void* context);

// Rebuilds the backing store at the new control size.
WK_PAGE_W10M_EXPORT unsigned __stdcall WKResizePageW10M(void*, unsigned width, unsigned height);

// Scrolls the page by the given delta.
WK_PAGE_W10M_EXPORT unsigned __stdcall WKScrollPageW10M(void*, int dx, int dy);

// Delivers a press/move/release event with a modifier mask.
WK_PAGE_W10M_EXPORT unsigned __stdcall WKPointerPageW10M(void*, unsigned type, double x, double y, unsigned modifiers);

// Runs timers, inertia and network pump for one frame.
WK_PAGE_W10M_EXPORT unsigned __stdcall WKTickPageW10M(void*);

// Paints unscaled page pixels into the caller buffer.
WK_PAGE_W10M_EXPORT unsigned __stdcall WKPaintPageW10M(void*, unsigned char* bgra, size_t capacity, unsigned stride);

// Paints with a DPI scale applied for high-density displays.
WK_PAGE_W10M_EXPORT unsigned __stdcall WKPaintPageScaledW10M(void*, unsigned char* bgra, size_t capacity, unsigned stride, double scaleX, double scaleY);

// Reports whether a repaint is pending.
WK_PAGE_W10M_EXPORT unsigned __stdcall WKPageNeedsPaintW10M(void*, unsigned* needsPaint);

// Snapshots scroll, content and history state.
WK_PAGE_W10M_EXPORT unsigned __stdcall WKGetPageStateW10M(void*, WKPageStateW10M*);

// Copies typed title, HTTP status, same-document change kind and redirect
// count captured from the loader client. On a nonzero result the caller struct
// is untouched and no field may be read.
WK_PAGE_W10M_EXPORT unsigned __stdcall WKGetPageDocumentStateW10M(void*, WKPageDocumentStateW10M*);

// Starts the GPU compositor on the given SwapChainPanel.
WK_PAGE_W10M_EXPORT unsigned __stdcall WKEnableCompositorW10M(void*, void* nativePanel, double scaleX, double scaleY);

// Stops the GPU compositor path.
WK_PAGE_W10M_EXPORT unsigned __stdcall WKDisableCompositorW10M(void*);

// Presents one compositor frame with the given overscroll offset.
WK_PAGE_W10M_EXPORT unsigned __stdcall WKRenderCompositorW10M(void*, double overscrollY);

// Returns an HRESULT; dispatcher is an IInspectable exposing ICoreDispatcher.
WK_PAGE_W10M_EXPORT long __stdcall WKInstallClipboardDispatcherW10M(void*, void* dispatcher);
#undef WK_PAGE_W10M_EXPORT
}

