# W10M architecture boundaries

**M10 authority:** [DESIGN.md](DESIGN.md) specifies the complete C++/CX design,
Windows 10 Mobile naming, target source tree and build/lifecycle contracts. Its
[OpenSpec change](../../openspec/changes/wkview-m10-w10m-cppcx-restructure/README.md)
supersedes M9's unfinished architecture. This note summarizes implemented
ownership and the current cleanup boundary.

This note defines ownership for the implemented XAML `WebKitWebView` control and
its remaining engine-integration gates. The framework provides reusable browser
behavior; applications provide chrome and product policy; test harnesses own
fixtures, sequencing and assertions.

## Layers

### Browser framework

`Source/WebCore/platform/w10m/` and
`Source/WebKit/UIProcess/API/w10m/` contain reusable engine and native-view
behavior. Keep page creation/destruction, resource loading, navigation/history,
input translation, layout/paint, compositor integration, clipboard integration,
and generic navigation/policy notifications here. Framework APIs describe
browser operations and outcomes without knowing a fixture server, test URL,
expected page text, test phase, or test result protocol.

`Source/WebKit/UIProcess/API/w10m/WebKitWebView.h/.cpp` is the authored home for the
reusable C++/CX WinRT/XAML control, with native/input/rendering/platform services
in sibling subdirectories. It owns the
public navigation contract, the control visual and its contained rendering
surface, and typed browser event projection. It must not own MiniBrowser
address-bar/status-bar UI, blocked-page product copy, test URLs, fixture
sequencing, or app-specific consent policy. `Tools/w10m/CXComponent/` owns
MIDLRT/build/packaging support only; generated products belong in build output.
The public authored control is in the API directory; `Tools/w10m/CXComponent/`
retains build support and explicitly identified prototype sources.

The directory is named for the projection it actually uses. It was
`WinRTComponent/` while the prototype was C++/WinRT; M10 adopts C++/CX because
the SDK's C++/WinRT base header rejects 32-bit ARM, so the old name misdescribed
the contents. Nothing is authored under `Source/` from this directory: the public
control's authored home is `Source/WebKit/UIProcess/API/w10m/`, and this directory
holds only the prototype being replaced, the browser-free gate probe, and build
support.

### Applications

`Tools/MiniBrowser/w10m/csharp/` is the Windows 10 Mobile C# application host over the reusable
component WinMD: address bar, toolbar, app navigation policy,
LocalState/package configuration, blocked-page template and copy, and the
presentation of failures. The former WRL shell files were removed during the
application-host relocation. No fixture-suite gate or diagnostic navigation
sequence belongs in the C# sample startup path.

`Tools/w10m/diagnostics/RuntimeSmoke/`, `FpuProbe/`, and `TlsSmoke/` are C#
diagnostic hosts over native payloads. RuntimeSmoke's shared `stage-app.py`
remains at `Tools/w10m/RuntimeSmoke/`. Their startup, logging, probe order and
expected values stay with those apps rather than becoming framework behavior.
If `FirstPageW10M.cpp` is
only used by RuntimeSmoke, its one-frame demo page/export belongs with that
smoke app; generic page creation and rendering remain framework operations.

### Test harness and platform tooling

`Tools/w10m/NavigationTests/` owns deterministic HTTP fixture content and
servers, device test orchestration, test endpoints, expected results and test
documentation. Device scenarios exercise the public control and ordinary
browser actions. Fixture documents may report JavaScript-visible outcomes to
the test server; the framework must not read their DOM to decide whether a test
passed.

`Tools/w10m/toolchain/`, CMake presets/toolchains and packaging scripts form a
separate platform/build-support layer. They support every W10M app and are
neither browser features nor MiniBrowser application behavior.

## Current implementation and remaining gates

M10 specifies the target contract. The C++/CX control, compiled metadata/XAML
identity, neutral ABI, native services and C# MiniBrowser host are implemented.
The production engine page forward still fails closed as unsupported; complete
remaining integration in the dependency order recorded by M10:

1. **One authoritative API specification.** M10 `public-api.md` defines the
   current target and deferred capabilities. CX declarations generate component
   metadata; M9 contracts/IDL remain historical references, not competing authority.
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

The compiled-XAML phone probe has established activation, inherited XAML
interfaces, declarative construction, binding/layout and ordinary Close. Keep
that proof distinct from production browser readiness: a child `SwapChainPanel`
proves visual ownership, not a functioning engine page/navigation path.

## Current cleanup boundary

The following are test-only and must leave `PageW10M.cpp`:

- `PageHostW10M` fixture suite state (`fixtureSuiteStarted`, fixture base URL,
  phase/reload/scroll state) and probe-only load generation state.
- `fixtureBodyText`, `probeFixtureMarkers`, delayed settle probes, the
  `FixtureSuitePhase` enum, `scheduleFixtureSuite`, and all DOM, cookie, font,
  redirect, history, CORS, CSP, stop/replacement and scroll assertions.
- The `uwp-suite=1` URL query trigger in `dispatchDidFinishLoad`, including
  suite-only completion/timeout log protocol.
- Fixture-specific marker logging from ordinary load completion. Generic
  navigation lifecycle and failure notifications remain.

The fixture server, pages, runner and result protocol move under
`Tools/w10m/NavigationTests/`. MiniBrowser should launch a test scenario only
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
the historical M9 `fixture-extraction-plan.md` and M10 `migration-map.md` /
`acceptance-matrix.md`. New runner evidence is required before extraction.

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

The control is a reusable browser view, not a complete browser application. It
owns one visual child for rendering and routes its measured size, DPI,
pointer, keyboard/focus and lifecycle changes to its native view. Its public API
exposes navigation, history, source and navigation events. It does not create its
own address bar, status UI, fixture runner or product-specific interstitial.
Compiled-XAML device evidence proves its WinRT runtime identity is a constructible
XAML control; production engine page creation remains a separate open gate.

The MiniBrowser app will host that control inside its own layout and provide
chrome around it. The device test app will host the same control and drive it
through the same public surface, with tests and fixtures owned by
`NavigationTests`.

## Implementation order

Follow M10 `tasks.md`: baseline and exhaustive naming map; complete C++/CX
feasibility; neutral ABI and engine extraction; public control/rendering/input/
platform services; MiniBrowser and diagnostics; verified fixture replacement;
packaging/cleanup and final device acceptance. Do not remove old coverage or
reuse historical device logs as new-package proof.
