# Tasks - M9 UWP WebKitWebView

> **Superseded:** use [M10 tasks](../wkview-m10-uwp-cppcx-restructure/tasks.md).
> These checkboxes preserve historical progress. Outstanding requirements are
> transferred in M10 artifact/migration-map.md; do not mark them completed by supersession.

Unchecked tasks retain outstanding acceptance conditions; partial implementation and device evidence are noted below. Complete specification artifacts do not establish implemented browser behavior. Run device checks where host compilation cannot prove behavior.

## 1. Contracts and deterministic fixtures

- [ ] 1.1 Inventory the existing curl ResourceHandle, single-process loader/scheduler and UWP client/session dependencies; deliver a source-path/dependency map identifying reused interfaces and required adapters.
- [ ] 1.2 Define native/public navigation IDs, Source updates, title/history/loading state, categorized terminal outcomes and app-supplied HTML origin rules; reconcile against `control-contract.md` and all three capability specs.
- [ ] 1.5 Review `public-api.md` against `control-contract.md`; define v1 IDL/ABI signatures, deferred features, event/lifecycle semantics and source placement. Script-result behavior is deferred from v1.
- [ ] 1.3 Add a deterministic test-server fixture with external CSS, ordered scripts, image, web font, relative link, redirects, MIME/encoding variants, HTTP errors and delayed responses; verify endpoints and expected output with a host-side fixture check.
- [ ] 1.4 Add fixture cases for allowed/blocked fetch/XHR, cookies, origin isolation, cancellation and resource budgets; document launch/setup commands and verify each expected response/policy condition before device testing.

## 2. Engine initialization and sessions

- [ ] 2.1 Extract common engine/thread/font initialization from the snapshot harness and install browser-capable strategies before Page construction; verify ARM build and retained local-demo initialization without swapping the global strategy at runtime.
- [ ] 2.2 Implement an independent ephemeral session per control with a consistent engine/transport cookie context; verify set-cookie requests and two-control isolation using the controlled fixture. Explicit sharing is v2.
- [ ] 2.3 **Deferred v2:** implement application-scoped persistent profiles and clearing for supported cookie/origin storage; verify restart/clear behavior on-device and document supported storage mechanisms and errors.
- [x] 2.4 Configure verified TLS/CA paths in the network context and document dependency/ICU override recipes; verify valid HTTPS succeeds and an invalid certificate fails on the phone without insecure retry.
  - Evidence: `device-tls-2.4.md` (fail-closed: `example.com` loads, `expired.badssl.com` fails terminally with code 60, no commit/retry) and `device-proceed-72.md` (consented override).
  - Amended 2026-10-04 (owner-directed) and implemented: fail-closed default with no silent/automatic bypass; one explicit exception — warning page → Advanced → proceed tap arms a bypass scoped to that host for the current page session only (`history-proceed-exception`, per-request enforcement, cleared on teardown). Verified: proceed tap reaches policy and never loads, exception arms for that host, proceeded document + subresources load, global flag untouched. Cross-host isolation and Back-from-proceeded-page traversal not yet log-confirmed; unknown-issuer variant untested.

## 3. Document navigation and history

- [ ] 3.1 Implement UWP embedding/frame-loader clients and a versioned native page bridge for normal URL navigation; verify requested/final URI, redirects and HTML HTTP-error documents using the fixture.
- [ ] 3.2 Preserve response headers, MIME type, encoding and document base/origin through the normal load path; verify relative resource resolution, non-UTF-8 content and unsupported MIME outcomes.
- [x] 3.3 Connect ordinary links, same-document navigation, Back/Forward/Reload and history availability to engine history; verify the fixture's navigation sequence produces correct entries without shell URL-vector history.
  - Evidence: 1.0.0.64 device run in `device-v64-history.md`. Retained `BackForwardClientUWP`, engine Back/Forward/Reload/Stop bridge, shell URL vector removed. Back/Forward/Reload, anchor same-document + Back, forward truncation on new navigation, and scroll restoration all pass. 1.0.0.66 (`device-v66-pushstate.md`) verifies `pushState` entry + Back/Forward traversal. 1.0.0.68 (`device-v68-replacestate.md`) verifies `replaceState` mutates without pushing (Back lands on the no-query URL) + Forward traversal. History `state`-object identity is the only remaining history gap (no JS evaluation bridge to read it back). Local `about:` loads are not engine-history entries by design.
- [ ] 3.4 Implement navigation state/events and exactly one terminal outcome per navigation ID; verify title/Source updates, Stop and replacement navigation with delayed responses and document event ordering.
- [ ] 3.5 Implement v1 app-supplied HTML through the documented isolated opaque-origin/about:blank substitute-data contract (no base-URI option); verify source, cancellation, history and origin isolation, and verify remote navigation never falls through this API.

## 4. Asynchronous subresources

- [ ] 4.1 Connect the selected scheduler/resource backend to the browser strategy and deliver callbacks on the owning thread independently of rendering; verify concurrent delayed resource requests progress while input remains responsive.
- [x] 4.2 Enable normal external stylesheet/script/image/font loads with response metadata and incremental data delivery; verify visible CSS effects, ordered external script mutations, image rendering and font use on the controlled page.
  - Evidence: 1.0.0.62 owner-confirmed green PNG; successful external CSS/script/image/font transfers and ordered script mutations. 1.0.0.63 renderer font probe confirms downloaded custom font selected, interstitial=0, glyph=1. See `device-v63-acceptance.md` and `handoff-2026-10-03.md`.
- [ ] 4.3 Wire supported script fetch/XHR paths through the same network/session context; verify permitted requests succeed and CORS/CSP/mixed-content fixture violations remain blocked.
  - Device 1.0.0.63: same-origin fetch/XHR, cross-origin allow/deny, CSP inline-script block and CSP image block pass. HTTPS mixed-content remains untested.
- [ ] 4.4 Implement lifetime-safe cancellation and stale-callback rejection for Stop, navigation replacement and page/control teardown; verify delayed resource completions never update a replacement document or released instance.
  - Device 1.0.0.63: Stop and replacement cancel a pending three-second image, with stable document checks after 3500 ms and no delivered delayed-image data/completion. Deferred diagnostic callbacks now expire with page lifetime. Page/control teardown acceptance remains outstanding.
  - Device 1.0.0.64 (`device-v64-history.md`): replacement and Stop cancellation re-verified with the history client active; no delayed-image bytes delivered. The three `navigation-error: 1` teardown reports are attributed in 1.0.0.65: our own `cancelledError` ("Load cancelled") for slow.html, the page being abandoned — correct expected behavior.
- [ ] 4.5 Add bounded concurrency/buffering/redirect/timeout configuration and categorized resource diagnostics; verify budget exhaustion, DNS failure and one failed image are distinct outcomes and document their defaults.
- [ ] 4.6 Build the native wrapper and Skia engine with eight parallel jobs, run controlled navigation/subresource regressions on the phone, and preserve a log summary proving success/failure/cancellation outcomes before XAML packaging migration.

## 5. WinRT and XAML component

- [ ] 5.1 Reconcile IDL with `control-contract.md`, place authored WinRT control sources under `Source/WebKit/UIProcess/API/uwp/WinRT/`, keep build/generation support under `Tools/uwp/WinRTComponent/`, and verify metadata without leaking WebCore types.
- [ ] 5.2 **Gate:** prove XAML runtime identity, inherited interfaces, intended construction/type resolution, layout and teardown on Windows 10 Mobile before browser integration. Activation alone is insufficient.
- [ ] 5.3 Implement XAML metadata/type-provider integration for compiled XAML and the shell's markup-loading path; verify `<web:WebKitWebView />` resolves in both cases using the packaged component.
- [ ] 5.4 Add v1 Source/state properties and typed per-navigation native/dispatcher-bound events; verify stable IDs, redirects, policy cancellation, exactly-one terminal outcome, history changes and no recursive Source navigation.
- [ ] 5.7 Implement lazy initialization/EnsureInitializedAsync, v1 NavigateToString and idempotent Close; verify concurrent initialization, pre-init Source replacement, isolated origin, and close/navigation cancellation. `ExecuteScriptAsync` is deferred.
- [ ] 5.5 Implement multi-instance control disposal/unload/recreation and independent ephemeral session/page ownership; verify two instances navigate independently and one can close during load without affecting the other.
- [ ] 5.6 **Deferred v2:** add new-window/download/permission request and deferral contracts; specify fail-closed unhandled behavior before enabling these features.

## 6. MiniBrowser application migration

- [ ] 6.1 Embed the custom control declaratively and wire toolbar actions/state to its public API; verify address/title/history/loading UI follows engine events and document the minimal application example.
- [ ] 6.2 Remove MiniBrowser's curl HTML-fetch worker, rendering-based completion polling and shell URL-vector history; verify the app loads fixture resources without these paths and no application code downloads/injects page resources.
- [ ] 6.3 Preserve local demo/capture/scroll diagnostics and the required Skia staging guard; verify local-page rendering, pinned layers and intentional non-Skia staging rejection remain correct.

## 7. Lifecycle and device integration acceptance

- [ ] 7.1 Build/package the matching engine/control/app and verify bridge exports, WinMD/runtime activation metadata, ARM dependency closure and CA bundle; bump/sign/install a new package and preserve its version/build identity in evidence.
- [ ] 7.2 Implement and run the NavigationTests device harness against the control contract, including keyboard/focus/tap where required, Stop, redirects, history, security and cancellation; report aggregate pass/fail and do not claim success from main HTML alone.
- [ ] 7.3 Verify resize/DPI and suspend/resume while resources are loading, plus cached/local-demo scrolling; record visual/backend/frame evidence and explicitly classify regressions versus the accepted v33 reference.
- [ ] 7.4 Load `https://en.wikipedia.org/wiki/Main_Page` on-device and check external styling, images, script behavior and article link navigation; record the exact URL/final URI, package, failed resources and owner observations.
- [ ] 7.5 Load `https://github.com/longit644/WebKit` on-device and exercise supported repository navigation and script-driven UI; record exact interactions and unsupported features rather than promising all GitHub functionality.
- [ ] 7.6 Review all capability scenarios against implementation/device evidence, obtain owner acceptance and update task/evidence status; archive or commit/push/tag only when explicitly requested and required checks are complete.
