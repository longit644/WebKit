# Design - M9 UWP WebKitWebView

> **Contract authority:** `control-contract.md` now defines the implementable
> v1 control API, event/lifetime rules and source placement. This document is
> the high-level engine/platform architecture; older statements that conflict
> with that v1 contract are design history pending cleanup.

## Context

See `proposal.md` for motivation. The verified reference is commit `5e0f025405`, MiniBrowser v1.0.0.33 on Lumia 950 XL / Windows 10 Mobile 15254.603. The native wrapper owns a standard XAML UserControl/SwapChainPanel and dynamically binds an opaque page bridge in WebCore. There is no registered WebKitWebView runtime class or WinMD/XAML type provider.

Current architectural debt is explicit: MiniBrowser owns a raw-pointer worker request and polls results from CompositionTarget.Rendering; `WKLoadHTMLUWP` inserts data through DocumentWriter with an empty URL; page construction uses empty clients; engine initialization installs FirstPageLoaderStrategy, which rejects subresources and reports offline. These are snapshot/probe mechanisms, not browser navigation.

The target remains ARM32 clang-cl, exceptions disabled, one engine/UI owning thread, and Skia Ganesh through TextureMapper/ANGLE/D3D11. Network I/O can use backend workers, but WebCore objects, DOM, JavaScript and control events stay on the owning thread. Existing runtime/mutex/ANGLE fixes remain necessary. The current global PlatformStrategies initialization means loader choice cannot be switched independently per control after engine initialization.

## Goals / Non-Goals

**Goals:**
- Give applications one reusable control API; host code implements UI and explicit policy rather than engine/network mechanisms.
- Restore the normal document/resource lifecycle so WebCore supplies parsing, script ordering, URL/origin semantics, security checks, caching and history.
- Deliver a declaratively constructible XAML component using the already verified native surface.
- Make cancellation, object lifetime, sessions and load-state events part of the design before introducing concurrent subresources.

**Non-Goals:**
- Do not implement an HTML rewriter, external-resource scraper or second JavaScript/CSS execution pipeline.
- Do not replace Skia, introduce JIT/multiprocess architecture, or declare M7/M8 complete through M9 documentation.
- Do not implement unrestricted privileged APIs merely to make a particular site appear functional. Unsupported media, service-worker and other APIs remain explicit limitations.

## Decisions

### 1. Separate the public control, host adapter, engine and transport

```text
Application: toolbar/tabs/bookmarks/policy UI
  -> WebKitWebView WinRT/XAML public contract
    -> native UWP control + page/navigation/session adapter
      -> WebCore document/resource loaders + JavaScriptCore
        -> existing curl/OpenSSL transport

WebCore painting -> Skia -> TextureMapper/ANGLE -> SwapChainPanel
```

Keep the native view and authored public WinRT/XAML control under `Source/WebKit/UIProcess/API/uwp/` (the component sources in `WinRT/`). Keep MIDLRT, packaging and build-generation support under `Tools/uwp/WinRTComponent/`; generated MIDL products belong in build output. Platform-specific surface and WebCore hooks stay in existing upstream-aligned locations. Evaluate the existing single-process loader/scheduler in WebKitLegacy as a reference/reuse candidate, without making applications depend on legacy UI types. Avoid pulling an entire unrelated browser port into the target to acquire one scheduler.

Alternative rejected: expand MiniBrowser's downloader with per-resource fetching. That would duplicate browser semantics and force each embedding application to implement engine behavior.

### 2. One browser-capable strategy provider; isolated snapshot tests

Extract common engine initialization (thread setup/fonts/platform setup) from the first-page snapshot harness. Install a browser-capable PlatformStrategies provider once, before Page construction. Route per-page/session policy through clients and network context rather than replacing the global provider. Snapshot diagnostics keep explicit local-only behavior within their own test setup; browser startup must not inherit the snapshot rejecting strategy.

Alternative rejected: swap the global strategy when a page requests a remote URL. Existing objects and other controls could retain inconsistent strategies.

### 3. Real URL navigation with implemented clients

Add a versioned page-bridge entry point for navigation, normal history actions and state/event delivery. Use FrameLoader's normal load path and a UWP embedding client that creates document loaders, makes supported MIME/navigation decisions and forwards title/load/history changes. Use an explicit user-agent string and the final response URL. HTTP error documents can render while exposing their status; an HTTP 404 is distinct from a DNS/TLS failure.

Expose Source as the requested URI when navigation starts and the effective URI when a redirect is committed; internal event/property updates must not trigger another navigation. Engine history owns document entries, same-document navigation and restoration. Back/Forward/Reload delegate to that history, not MiniBrowser's vector of URLs.

Keep app-supplied HTML loading as a distinct method. V1 uses the isolated opaque-origin/about:blank contract in `control-contract.md` and has no base-URI parameter. A future base URI may resolve relative resources but must not grant an arbitrary trusted origin. Remote pages must not be loaded through this API.

### 4. Reuse curl networking through WebCore resource loading

Inspect existing curl ResourceHandle, scheduler, session and networking-context code and adapt the minimum host interfaces required by this revision. Feed responses incrementally through normal WebCore loader callbacks, including final URL, headers, MIME type, encoding, status, errors and metrics. Use WebCore's normal CSS/script/image/font and fetch/XHR paths; retain CSP, CORS, mixed-content and origin enforcement there.

Certificate verification stays enabled for every network request, using a packaged CA path configured by the adapter/session. Never solve missing resources by disabling verification, origin checks or CSP. Preserve response compression/decoding support through the existing backend.

### 5. Owning-thread completion, cancellation and event contract

Use the backend's scheduling and dispatch facilities to complete requests on the engine/UI thread independently of rendering. Each navigation has a stable identifier; late callbacks from superseded/cancelled loads cannot update current state. Stop, navigation replacement and control destruction cancel outstanding work and detach callbacks before releasing page/session state. A worker never owns an unretained application pointer.

Public events are delivered on the owning dispatcher: NavigationStarting (cancellable), SourceChanged, DocumentTitleChanged, HistoryChanged, NavigationCompleted (success/error/status) and requests for new windows/downloads/permissions. Each started main navigation has one terminal outcome, including cancellation. Navigation completion refers to the normal document load event boundary, not network-idle or completion of indefinite fetch/stream activity. Subresource failure does not automatically turn an otherwise successful main navigation into transport failure.

Alternative rejected: polling resource completions on CompositionTarget.Rendering. Hidden/suspended/unchanged visuals must not determine whether resource callbacks make progress.

### 6. Session/profile and limits

V1 gives each control an independent ephemeral session. Persistent profiles, explicit session sharing and profile clearing are deferred to v2. WebCore and transport must share the same cookie context rather than maintaining divergent cookie jars. Storage availability and initialization failures are exposed clearly.

Implement configurable bounded concurrency, buffering/resource budgets, redirects and timeouts through the loader/backend. Stream main documents rather than copying every complete response into a shell string. The 512 KB probe cap is not the production document limit. Limit exhaustion becomes a typed error. Resource limits are measured on-device, especially for image decoding and long-page tile retention.

### 7. Native WinRT/XAML packaging

Use https://learn.microsoft.com/en-us/microsoft-edge/webview2/get-started/winui as the interface/embedding reference selected by the owner. Its WinUI 3/Windows App SDK installation/runtime requirements do not apply to this Windows 10 Mobile UWP implementation. Adopt the familiar control usage, not its Chromium runtime or Microsoft API namespace. The exact M9 public surface and differences are defined in `public-api.md`.

Target public runtime name: `WebKit.WebKitWebView`; application markup uses an alias such as `xmlns:web="using:WebKit"` and `<web:WebKitWebView />`. Define the contract in authored IDL under `Source/WebKit/UIProcess/API/uwp/WinRT/` and package a WinMD plus ARM activation implementation. Use native WRL/ABI compatible with this toolchain. XAML composition/type resolution is a device gate: the current headless component prototype does not prove `UserControl` identity. Do not claim declarative usability until the minimal XAML probe passes.

Keep internal page types out of WinMD. Expose URI/string/value objects, async/event contracts and dependency properties suitable for binding. Package activation metadata and dependencies with the consuming application. Verify two independent control instances and disposal/recreation, not only a single process-lifetime instance.

The v1 member set, including deferred `ExecuteScriptAsync`, event definitions, HTML-origin behavior and lifecycle, is specified by `control-contract.md`. Do not implement older API lists here in conflict with that versioned contract.

Alternative rejected: label a ContentControl containing a native surface as the completed custom control. It does not establish XAML construction, binding or public runtime activation.

### 8. Application policy without duplicated engine mechanisms

The control reports navigation to the application through a synchronous lifetime-safe policy callback; the app may cancel before transfer. Certificate bypass is a separate one-shot, target-bound decision described in `control-contract.md`. New-window/download/permission contracts are deferred to v2; when added, unhandled requests must fail closed and never silently launch applications or write files. Ordinary supported links stay in the current control. Unsupported engine features produce an explicit outcome instead of a success event.

### 9. Skia deployment and reproducibility

Always build/stage the browser engine from `build-arm-uwp-skia/bin` with `USE_SKIA=ON`. Preserve the staging guard, fixed runtime/TLS libraries, CA bundle and refreshed ICU override. Version the page-bridge contract and verify required exports before deployment to avoid the v31 missing-export failure. Rendering diagnostics continue to identify GPU painting, CPU fallback and full frame costs.

## Risks / Trade-offs

- Existing loader code assumes other port-specific clients/session infrastructure -> map dependencies first and adapt narrowly; validate cancellation and headers on controlled cases before live sites.
- ARM runtime fixes are embedded in dependencies -> audit the chosen dependency closure and preserve proven rebuild/override recipes.
- UI/engine thread work can block input -> asynchronous transport and bounded completion batching; measure style/script/layout costs separately from I/O.
- Heavy pages can exceed phone memory -> streaming, finite concurrency/cache budgets and explicit limit errors; test navigation away while images/scripts load.
- WinRT/XAML metadata tooling on ARM32 may differ from host defaults -> compile/activate a minimal custom-control probe before the full component integration, retaining the chosen native architecture.
- Live websites change and use unsupported APIs -> deterministic fixture tests are the correctness gate; capture URLs, final URLs, versions, resource failures and visual observations for live-site evidence.
- Unload/suspend while requests or events are pending -> cancellation generations, ownership guards, dispatcher teardown tests and device recreation/resume checks.
- The renderer baseline is accepted but not uniformly 60 FPS -> compare regressions to v33 without claiming full browser performance acceptance from a successful page load.

## Migration Plan

1. Preserve v33/commit `5e0f025405` as the reference and create deterministic resource/navigation fixtures.
2. Define the native/public contracts, extract initialization, connect real loader/client/session behavior and validate with the native wrapper before metadata packaging.
3. Build the WinRT/XAML component, migrate MiniBrowser markup/actions and remove the HTML-fetch worker/polling and shell history.
4. Build using eight parallel jobs, verify ARM exports/dependency closure, stage Skia with CA/runtime overrides, bump package version, sign and deploy.
5. Run fixture regressions, then Wikipedia and GitHub. Record concrete working/unsupported interactions and owner acceptance; keep implementation tasks open until their checks pass.
6. If a candidate fails, repack the verified v33 sources/artifacts with a newer package version as required by device update rules. Preserve failure logs/dumps. Do not silently deploy Cairo or weaken TLS to recover.
