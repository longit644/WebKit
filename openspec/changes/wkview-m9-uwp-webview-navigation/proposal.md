# Proposal - M9 reusable UWP WebKitWebView and normal navigation

## Why

MiniBrowser v1.0.0.33 verifies Skia rendering and HTTPS on Lumia, but remote pages are downloaded by the application and inserted as HTML with an empty document URL. Wikipedia and GitHub consequently render without their external resources; applications cannot yet declare a reusable `<web:WebKitWebView />` control and delegate web-engine behavior to it.

## What Changes

- Define the reusable control contract: Source/Navigate, Back/Forward/Reload/Stop, loading state, document title, navigation events, and explicit session/profile configuration.
- Use Microsoft's WebView2 WinUI embedding tutorial as the public-interface reference: declarative Source/layout, cancellable navigation events, observable load/history changes and asynchronous script execution, adapted to UWP XAML with the WebKit runtime rather than claiming WebView2 compatibility.
- Implement normal WebCore navigation, loader clients, asynchronous subresource loading and history using the existing curl/OpenSSL backend where compatible.
- Preserve URLs, origins, redirects, response metadata and WebCore security enforcement so CSS, JavaScript, images, web fonts and script-originated requests load through engine APIs.
- Provide WinRT activation/metadata and XAML type integration for `<web:WebKitWebView />`, backed by the existing native UWP control and Skia surface.
- Move networking, page state and resource lifecycle out of MiniBrowser; the application supplies UX/UI and explicit policy decisions for permissions, new windows and downloads.
- Preserve app-supplied HTML loading as a separate API with a defined base-URL/origin contract; remove the remote curl-to-loadHTML shortcut and shell-maintained history.
- Validate controlled resource-loading cases, then Wikipedia and GitHub, on the device. Keep v33 as a regression reference and require Skia in every MiniBrowser deployment.

## Capabilities

### New Capabilities

- `uwp-webview-control`: reusable WinRT/XAML control, public contract, ownership, events and application/engine boundary.
- `uwp-web-navigation`: document navigation, response fidelity, history, load state and cancellation.
- `uwp-web-resource-loading`: asynchronous network integration, subresource loading, sessions, security, limits and diagnostics.

### Modified Capabilities

None. `openspec list --specs` currently reports no archived capability specifications. M7 font/clipboard and M8 rendering requirements remain separately tracked.

## Impact

- `Source/WebKit/UIProcess/API/uwp/`: reusable control, navigation adapter, WinRT runtime class/metadata and host events.
- `Source/WebCore/platform/uwp/`: page/client wiring and separation of snapshot-only initialization from browser platform strategies.
- Existing WebCore curl networking and in-tree single-process loader implementations; prefer reuse over a parallel HTTP/resource loader.
- `Tools/MiniBrowser/uwp/`: declarative control usage and UI-only navigation actions.
- ARM-UWP CMake/toolchain, component packaging, certificate bundle, session storage and device tests.
- Existing application-local dependencies and the tested Skia renderer remain the starting point. Any additional link/runtime requirements need ARM/device verification.

## Non-goals

This change does not promise every feature of Wikipedia/GitHub, universal website compatibility, JavaScript JIT, multiprocess isolation, complete media/WebRTC/service-worker support, unrestricted downloads, or uniformly steady 60 FPS on arbitrary pages. Existing unsupported features must be reported explicitly, not bypassed or represented as working.
