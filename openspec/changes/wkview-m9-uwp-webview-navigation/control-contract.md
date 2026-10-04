# UWP WebKitWebView — authoritative control contract

Status: design decision for the next implementation phase; not yet implemented.
This document supersedes conflicting API/placement/event/lifecycle statements in
`public-api.md` and `design-winrt-component.md`. The architecture boundary is
documented in `Tools/uwp/ARCHITECTURE.md`; device fixture replacement is mapped
in `fixture-extraction-plan.md`.

## Ownership and source placement

The reusable browser control is framework code:

- `Source/WebCore/platform/uwp/`: engine integration and platform backend.
- `Source/WebKit/UIProcess/API/uwp/`: native view/page bridge.
- `Source/WebKit/UIProcess/API/uwp/WinRT/`: authored WinRT IDL, WRL projection,
  XAML control source and public metadata contract.
- `Tools/uwp/WinRTComponent/`: build/generation/packaging support only. MIDLRT
  outputs and build products belong in build directories, not as authored source
  in the framework implementation directory.
- `Tools/MiniBrowser/uwp/`: app chrome, app configuration, blocked-page UX and
  explicit product policy.
- `Tools/uwp/NavigationTests/`: fixtures, device runner, test protocol and
  assertion results.

The current files under `Tools/uwp/WinRTComponent/` are a prototype and remain
where they are until the replacement is designed and moved deliberately. Do not
move files or implement the XAML control as part of this design-only step.

## XAML identity and rendering ownership

The target public type is `WebKit.WebKitWebView`, constructible as a regular
UWP XAML element and deriving from the supported XAML control base. Applications
place it in normal layout without attaching a renderer manually. The control
owns exactly one native view/page and one rendering visual; it routes measured
size, DPI, pointer, keyboard/focus and load/unload/close lifetime to that view.
It does not own browser chrome.

This target is conditional on a device feasibility probe: activation, inherited
XAML interface identity, XAML construction/type resolution, layout, source
binding and teardown on Windows 10 Mobile 15254. A `SwapChainPanel` inside a
native WRL object is not proof that the outer object is a XAML element. If the
platform only supports hosting a returned visual, explicitly rescope/name that
as an interim hosting API before implementation; do not label it the completed
XAML control.

## Supported v1 public API

### Properties

- `Source`: URI dependency property. Setting a valid absolute HTTP(S) URI
  requests navigation (lazy initialization is allowed). Reading it returns the
  effective committed main-frame URI; redirects update it only on commit.
  Internal updates must not recursively start navigation.
- `CanGoBack`, `CanGoForward`, `IsLoading`, `DocumentTitle`: read-only observable
  state sourced from the owning page.
- Standard XAML layout/visibility properties.

The API does not also expose a redundant `Navigate(uri)` in v1; assigning
`Source` is the navigation operation. `EnsureInitializedAsync`, Back, Forward,
Reload and Stop are explicit methods. Invalid arguments return an explicit
HRESULT/WinRT argument error.

### Events

- `NavigationStarting`: synchronous cancellable policy event; URI, stable
  navigation ID and user-initiation classification where the engine can
  determine it. **Redirect hops are not reported in v1.** The transport follows
  301/302/303/307/308 internally through the resource-loader
  `willSendRequest` path (`LoaderStrategyUWP.cpp`), so the frame navigation
  policy callback is not re-entered per hop. V1 therefore reports one
  `NavigationStarting` for the requested URI and exposes the final committed URI
  through `SourceChanged`. Per-hop redirect events require surfacing the
  loader-level redirect decision to the page bridge and are deferred; a
  redirect flag must not be claimed until that exists.
- `SourceChanged`: effective URI plus `IsNewDocument`, supplied by the engine.
  This requires a new same-document signal from the loader/chrome client; the
  current implementation infers it by comparing URL strings with the fragment
  stripped (`WebKitWebViewComponent.cpp`), which also misclassifies a same-URL
  reload and cannot see `pushState`/`replaceState`.
- `HistoryChanged`: changed back/forward availability, sourced from engine
  history mutations rather than post-call polling. No history-mutation hook is
  currently bridged, so this is new bridge work, not an existing behavior.
- `DocumentTitleChanged`: current title on actual title changes. No title
  plumbing exists today; this is new bridge work.
- `NavigationCompleted`: exactly one terminal event per started main-frame
  navigation, with `Outcome` = Completed, Failed or Cancelled, stable ID, URL
  and a typed error domain plus code. A valid HTTP 404 document completes
  successfully; transport failure is not an HTTP status. Superseded/stopped work
  cannot later complete successfully under the same ID. **HTTP status is not
  yet available to the page client** — the frame loader client does not receive
  the response — so either expose the main-document response status through the
  bridge or omit status from v1 rather than promising an unsourced field.

There is no separate `NavigationFailed` terminal event in v1: failure and
cancellation are terminal outcomes on `NavigationCompleted`. `ContentLoading`,
new-window, download and permission events are deferred until their engine
sources and deferral semantics are designed. The control may add a
`DOMContentLoaded` event later without changing the v1 terminal contract.

Public events are delivered on the owning UI dispatcher. Event args hold copied
values, not raw WebCore pointers. Reentrant navigation from an event handler is
defined and tested. Native diagnostic logs are not the source of API events.

### HTML supplied by the host

`NavigateToString(html)` is included for host-owned interstitial/error content.
It cancels/supersedes the active main navigation and loads an isolated
app-supplied document with an opaque origin; it does not inherit the previous
remote document's privileges and does not create a normal remote history entry.
The effective source is `about:blank`. V1 has no base URI option and relative
resources do not resolve to an implicit remote origin. If a future base URI is
added, it controls URL resolution only and never grants origin trust. This
contract must be implemented through the supported substitute-data loading
path, not by writing into whichever loader happens to be active.

### TLS certificate decision

There is no navigation to a `minibrowser:` or other app-reserved URI to trigger
certificate bypass. On an eligible certificate-verification failure, the
terminal event identifies a pending, one-shot failure token/navigation ID. An
explicit app call to `ProceedWithCertificateFailure(id)` may request a retry
for that exact failed HTTPS target. The engine validates token freshness,
target and certificate-error eligibility, limits the exception to the current
control session, and consumes it once. No API disables verification globally.
The application alone decides whether/when user consent was given and presents
the warning UX. Ordinary content cannot invoke the native method.

### Lifetime, instances and session v1

- Multiple control instances are supported; page state, event context, pending
  callbacks, rendering and close operations are instance-scoped.
- Each instance receives an independent ephemeral session by default.
- Explicit session sharing, persistent profiles, profile clearing and storage
  configuration are deferred to v2. V1 must not silently share cookie state
  across separate controls.
- `Close()` is idempotent. It prevents new operations, cancels pending work,
  detaches native/event/policy callbacks before page release and is safe during
  an event callback. Releasing the XAML element invokes equivalent cleanup.
  Calls after close fail explicitly. Close does not tear down process-global
  engine state still used by another control.
  This requires new work: `WebKitWebViewUWP` exposes no public close/detach
  entry point and its engine log sink has no clear operation, and the component
  has no destructor that clears its routed sink.
- Engine operations remain owning-thread-affine; callers on another thread
  receive a documented wrong-thread HRESULT. UI callbacks never retain an
  unowned app pointer.

## Native bridge requirements

The native page bridge must deliver structured callback data with per-page
context. It must not ask the WinRT adapter to parse diagnostic stage strings or
attribute an event to a mutable `m_activeId`. The contract needs stable IDs
across replacement loads, source/history changes, policy cancellation,
committed-load failures, HTTP status, typed error domain/code and exactly-once
terminal outcomes. Preserve existing numeric ABI values; version new
structs/callbacks rather than reinterpreting existing result codes.

### Error identity must be explicit

`ResourceError::errorCode()` alone is ambiguous today and the current log
protocol drops `ResourceError::domain()` entirely. The bridge must deliver both
parts:

- Transport failures carry a `CURLcode`; the app-facing blocked page already
  keys on names such as `CURLE_PEER_FAILED_VERIFICATION`,
  `CURLE_COULDNT_RESOLVE_HOST`, `CURLE_COULDNT_CONNECT` and
  `CURLE_OPERATION_TIMEDOUT`.
- Adapter refusals carry `UWPResourceErrorCode` values in domain `UWPNetwork`
  (cancelled, blocked, unsupported URL/MIME, redirect failure, HTTPS-only,
  byte budget, synchronous unsupported).
- `WKNavigationFailureCancelledUWP` is numerically `1`, the same value as
  `UWPResourceErrorCancelled`. The numeric agreement is currently implicit, so a
  domain-carrying contract must state that cancellation is identified by
  domain plus code rather than by the number alone.

Keep these families distinct in the public contract; do not flatten them into one
integer "error code" field.

The bridge must support multiple page handles and callback detachment on Close.
Use an opaque callback context retained by the page/client and explicitly
cleared before destruction. If new ABI exports are required, add versioned
exports rather than silently changing old ABI layouts.

## Deliberately deferred

Deferred from v1: host JavaScript evaluation (`ExecuteScriptAsync`), persistent
profiles/shared-session configuration, content-loading/DOMContentLoaded event,
download/new-window/permission deferrals, arbitrary URL schemes, and long-lived
storage APIs. Fixture JavaScript reports its own observable outcomes to the
NavigationTests server; the app test runner does not need privileged script
inspection for v1 coverage. Any later script API is a generic host capability,
with document-generation cancellation and JSON result semantics, not a
fixture-only hook.

## Implementation gates

1. Update dependent M9 docs/specs to this decision and retain deferred features
   as explicitly unimplemented follow-ups.
2. Complete the per-assertion fixture replacement design in
   `fixture-extraction-plan.md`; keep the current suite until replacement
   evidence exists.
3. Implement/validate the versioned typed bridge, TLS decision token and
   multi-instance lifecycle without the XAML control.
4. Pass the minimal XAML identity/composition probe on device.
5. Only then implement the control and migrate MiniBrowser to host it.
