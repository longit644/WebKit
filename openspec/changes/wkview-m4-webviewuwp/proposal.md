# wkview-m4-webviewuwp — WebKitWebViewUWP mirroring upstream structure

## Why

Owner decision: no invented structure. Our UWP view mirrors upstream's
`Source/WebKit/UIProcess/API/gtk/` layout and `WebKitWebView` API shape,
adapted to WinRT/XAML. Proven GTK trio to mirror:
`WebKitWebViewGtk.cpp` (public control), `WebKitWebViewBase.*` (page
lifecycle + compositing), `PageClientImpl.*` (input/scroll/popups).

## What

1. New `Source/WebKit/UIProcess/API/uwp/` with v0 trio:
   - `WebKitWebViewUWP.h/.cpp` — navigation (LoadUri/GoBack/Forward/Reload/
     Stop), Title/Uri/IsLoading properties, LoadChanged/NavigationStarting
     events. Same method ORDER and naming as Gtk twin, WinRT types.
   - `WebKitWebViewBaseUWP.h/.cpp` — Page create/destroy, compositing hookup
     (Cairo bitmap v0, SwapChainPanel later), settings passthrough.
   - `PageClientImplUWP.h/.cpp` — touch/keyboard/pinch → WebCore events,
     popup-menu + dialog stubs (deferred to v0.2).
2. `Tools/UWP/UWP-MAPPING.md` — GObject→WinRT, signals→events,
   HWND→CoreWindow/SwapChainPanel, glib-loop→generic RunLoop (already done).
3. Rules: new files are 100% ours (no guards inside); shared-file edits keep
   `WK_WEBKITVIEW` guards; `uwp/` is NOT referenced by any build list until
   the engine links (default build unaffected — verified by host reconfigure).
4. Control shell compiles for ARM + deploys to 950 XL BEFORE engine wiring
   (proves appx/Device Portal chain early); engine behind the same interface.

## Non-goals

No dialogs/print/inspector in v0. No changes to gtk/wpe/cocoa ports.
No engine wiring (that's M5).

## Acceptance

Trio + mapping doc committed; host build unaffected (WebKitBuild still
configures+builds jsc); owner ack → tag `wkview-m4-webviewuwp`.
Device deploy of the shell is M4b (needs VS2017 XAML chain — separate change).
