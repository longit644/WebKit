# UWP architecture boundaries

This note defines ownership before the XAML `WebKitWebView` control is completed.
The framework provides reusable browser behavior; applications provide chrome and
product policy; test harnesses own fixtures, sequencing and assertions.

## Layers

### Browser framework

`Source/WebCore/platform/uwp/` and
`Source/WebKit/UIProcess/API/uwp/` contain reusable engine and native-view
behavior. Keep page creation/destruction, resource loading, navigation/history,
input translation, layout/paint, compositor integration, clipboard integration,
and generic navigation/policy notifications here. Framework APIs describe
browser operations and outcomes without knowing a fixture server, test URL,
expected page text, test phase, or test result protocol.

`Source/WebKit/UIProcess/API/uwp/WinRT/` is the intended authored home for the
reusable WinRT/XAML adapter, alongside the native view it projects. It owns the
public navigation contract, the control visual and its contained rendering
surface, and typed browser event projection. It must not own MiniBrowser
address-bar/status-bar UI, blocked-page product copy, test URLs, fixture
sequencing, or app-specific consent policy. `Tools/uwp/WinRTComponent/` owns
MIDLRT/build/packaging support only; generated products belong in build output.
The current prototype remains in its existing location until deliberately
replaced.

### Applications

`Tools/MiniBrowser/uwp/` owns the sample browser experience: address bar,
toolbar, app navigation policy, LocalState/package configuration, blocked-page
template and copy, and the presentation of failures. It consumes the reusable
control and decides what the user sees. No fixture-suite gate or diagnostic
navigation sequence belongs in the shipping/sample browser startup path.

`Tools/uwp/RuntimeSmoke/`, `FpuProbe/`, and `TlsSmoke/` are diagnostic
applications. Their startup, logging, probe order and expected values stay with
those apps rather than becoming framework behavior. If `FirstPageUWP.cpp` is
only used by RuntimeSmoke, its one-frame demo page/export belongs with that
smoke app; generic page creation and rendering remain framework operations.

### Test harness and platform tooling

`Tools/uwp/NavigationTests/` owns deterministic HTTP fixture content and
servers, device test orchestration, test endpoints, expected results and test
documentation. Device scenarios exercise the public control and ordinary
browser actions. Fixture documents may report JavaScript-visible outcomes to
the test server; the framework must not read their DOM to decide whether a test
passed.

`Tools/uwp/toolchain/`, CMake presets/toolchains and packaging scripts form a
separate platform/build-support layer. They support every UWP app and are
neither browser features nor MiniBrowser application behavior.

## Decisions required before implementing the XAML control

The layer boundary above is approved in principle, but it is not yet a complete
control contract. Resolve these design gates first:

1. **One authoritative API specification.** `openspec/changes/wkview-m9-uwp-webview-navigation/control-contract.md`
   now defines v1 and deferred capabilities, and supersedes conflicting public
   API/IDL prose. Reconcile the remaining M9 specs to it before implementation.
2. **Typed native bridge.** Do not derive the public contract by parsing log
   strings. Specify context-bearing callbacks/state with stable navigation IDs,
   policy cancellation, exactly-once terminal outcomes, committed-load
   failures, HTTP/error domains, engine-reported same-document classification,
   source changes and history changes. Logs remain observational diagnostics.
3. **Lifetime and instance ownership.** Define idempotent Close, callback and
   policy-context detachment, pending-operation cancellation, teardown
   reentrancy, UI-thread requirements, multiple instances and close/recreate.
   Replace process-global raw instance routing before claiming a reusable
   control.
4. **Explicit security decision flow.** The application owns the meaning of a
   user gesture and consent UI. The engine enforces a narrowly scoped decision
   associated with the current eligible certificate failure/navigation. A web
   document must not obtain certificate bypass by navigating to an
   application-reserved URL or scheme.
5. **Proven XAML identity.** Resolve whether the public object is actually
   constructible as a XAML `UserControl` (including inherited XAML interfaces
   and markup/type resolution), or define and name an interim visual-hosting
   API honestly. Prove visual composition and teardown on device before
   connecting the browser. Ensure there is one owner for the rendering child,
   input/size subscriptions and native page.
6. **Defined HTML-content semantics.** Specify origin/base URL, Source,
   history, cancellation and navigation events for app-supplied HTML. Writing
   into whichever document loader happens to be active is not a substitute-data
   contract.

The minimum visual feasibility probe should activate the proposed type, query
its inherited XAML interfaces, construct it through the intended XAML path,
measure/layout it, bind its source and close it. Browser navigation integration
follows only after that passes. A child `SwapChainPanel` inside a native wrapper
proves that the wrapper can own a visual; it does not by itself prove that the
outer WinRT object is a XAML element.

## Current cleanup boundary

The following are test-only and must leave `PageUWP.cpp`:

- `PageHostUWP` fixture suite state (`fixtureSuiteStarted`, fixture base URL,
  phase/reload/scroll state) and probe-only load generation state.
- `fixtureBodyText`, `probeFixtureMarkers`, delayed settle probes, the
  `FixtureSuitePhase` enum, `scheduleFixtureSuite`, and all DOM, cookie, font,
  redirect, history, CORS, CSP, stop/replacement and scroll assertions.
- The `uwp-suite=1` URL query trigger in `dispatchDidFinishLoad`, including
  suite-only completion/timeout log protocol.
- Fixture-specific marker logging from ordinary load completion. Generic
  navigation lifecycle and failure notifications remain.

The fixture server, pages, runner and result protocol move under
`Tools/uwp/NavigationTests/`. MiniBrowser should launch a test scenario only
when explicitly requested by the test harness, never on normal startup. Keep
the framework's public navigation/history/render APIs generic and use them from
the harness. Any assertion requiring private WebCore DOM/font internals should
become a proper WebKit test or an observable fixture result, not a permanent
framework test hook.

`NavigationTests` currently has deterministic server fixtures and host tests,
but not yet a device browser runner or result-reporting protocol. Plan
replacement coverage before removing the current device suite. Use public
navigation/events and fixture-side result reporting for application-visible
behavior; expose generic script/state APIs only when justified as reusable
browser capabilities. Keep renderer-private font/platform-data checks as
internal WebKit tests rather than moving those assertions into an app.

The assertion-by-assertion replacement and safe removal sequence are recorded in
`openspec/changes/wkview-m9-uwp-webview-navigation/fixture-extraction-plan.md`.

Before accepting fixture coverage, correct known test/document mismatches: the
CSP fixture must report whether the inline script executed; the large-response
route must exercise the intended HTML/main-document budget if that is the
claim; visual CSS assertions need visual/computed-style evidence; and suite
completion must mean aggregate assertion success, not merely runner termination.

MiniBrowser's temporary component/event diagnostics and blocked-template test
sequencing are also harness concerns. The reusable component keeps ordinary
events and navigation behavior; test event subscriptions and scenarios move to
the device harness when the XAML control is exercised there.

## XAML control contract

The intended control is a reusable browser view, not a complete browser
application. It owns one visual child for rendering and routes its measured
size, DPI, pointer, keyboard/focus and lifecycle changes to its native view. Its
public API exposes navigation, history, source and navigation events. It does
not create its own address bar, status UI, fixture runner or product-specific
interstitial. The current implementation has not yet proven that its WinRT
runtime identity is a constructible XAML control.

The MiniBrowser app will host that control inside its own layout and provide
chrome around it. The device test app will host the same control and drive it
through the same public surface, with tests and fixtures owned by
`NavigationTests`.

## Implementation order

1. Reconcile the normative API/design documents and choose framework source
   placement.
2. Specify and implement the typed native bridge, lifetime rules and explicit
   application-consent flow.
3. Map every fixture assertion to a replacement, create the device runner and
   result protocol, then remove fixture-only WebCore/MiniBrowser startup logic.
4. Prove minimal XAML identity, composition, layout and teardown on device.
5. Connect browser behavior to the control and migrate MiniBrowser to host it.
6. Add another application shell only after the control contract is stable.
