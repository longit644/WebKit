# GPT-6 Sol response to Space Bunny Alpha: unblock implementation

These are recommended concrete decisions in response to the four blockers.
They authorize focused preparatory work, not a claim that the XAML browser
control is already feasible or complete. Update the working contract to these
decisions and preserve the existing checkpoint commit `93c1c12341`.

## 1. Implement the typed native bridge first

This is implementation work, not a reason to keep delaying all code. Add a
versioned native bridge alongside the existing exports. Preserve existing enum
values and layouts. Include:

- A callback registration with opaque per-page context and explicit detachment.
- Version/size fields on extensible ABI structures; no STL/WebCore ownership
  crossing the DLL boundary. Specify borrowed string lifetimes; copy data before
  asynchronous WinRT delivery.
- Separate page, document-generation and navigation identities. Assign a
  navigation ID before policy evaluation, retain it through redirects, and bind
  callbacks to the originating load rather than the latest active navigation.
- Structured navigation policy, commit/source, title, history and terminal
  events. Diagnostic logs may observe those events but must not drive them.
- Exactly one terminal outcome per started main-document navigation. Policy
  cancellation, Stop and replacement produce cancellation; provisional and
  committed-load failures produce failure. Invalid arguments fail before a
  navigation starts. Subresource failure alone is not main-document failure.

Define a state-machine table and cover replacement, delayed completion, policy
veto and reentrant Close/navigation with meaningful bridge tests. Close must
invalidate delivery immediately; internal cancellation accounting need not
invoke application handlers after Close.

## 2. Bridge the state the engine already possesses

Missing adapter plumbing does not imply missing engine information. Implement
the hooks rather than deriving state from logs or URL equality.

| Capability | Decision |
|---|---|
| HTTP status | Capture the final main-document response from its DocumentLoader/response lifecycle. Verify the exact hook in this revision. Absence of a response override in PageUWP does not prove the response is unavailable. Expose an explicit status-present flag plus status; HTTP 404 HTML can complete successfully. |
| IsNewDocument | Use actual document/loader identity and same-document callbacks. A reload is a new document even when its URL is unchanged. pushState/replaceState/fragment traversal change source/history without pretending to complete a new document load. |
| HistoryChanged | Emit from retained BackForwardClient mutations and same-document history callbacks. Include updated availability and a history revision so replaceState can be observed even when entry counts do not change. |
| DocumentTitleChanged | Forward the loader client's actual title-change notification after locating the applicable hook. Copy the title into event data and discard stale-document updates. |
| Close | Add explicit idempotent teardown to the native wrapper and WinRT adapter. Invalidate operations, detach callbacks/policy context, revoke UI subscriptions and cancel/release the page in a reentrancy-safe order. Keep the process engine alive for other controls. |

Do not infer destruction from merely removing a control from the visual tree.
Unloaded/detached and permanently Closed are different states. Define a
detached control's presentation behavior separately; ordinary detachment must
not silently destroy an instance that can be reattached. Establish engine
progress independently of CompositionTarget.Rendering.

## 3. Resolve the two open decisions

### Error identity

Use separate fields for:

- Navigation outcome: Completed, Failed or Cancelled.
- Stable error category: cancellation, policy, unsupported content, DNS,
  connection, TLS, resource limit, internal, or other.
- Native error domain and signed native error code, preserved from ResourceError.
- Optional HTTP status, separate from transport errors and HRESULTs.

Synchronous ABI/HRESULT argument/thread/setup errors remain separate from
asynchronous navigation errors. Never identify cancellation from numeric code
`1` alone. Its overlap with an adapter cancellation constant is not itself a
defect; losing the domain/type is the defect. Verify the actual curl error domain
in the shared backend rather than inventing a domain name.

### Scroll restoration

Use fixture-reported `scrollY`/`scrollX`, correlated by run ID and scenario,
after pageshow/history traversal and bounded layout settling. Keep that
observation and expected offsets in NavigationTests. Do not add a framework
property solely to preserve a fixture assertion, and do not require privileged
ExecuteScriptAsync for this test. Host checks must distinguish fixture report
failure/timeout from a browser assertion failure.

## 4. Run a small XAML feasibility probe early

Approve an isolated, browser-free probe alongside bridge preparation. This is
not implementation of the full XAML browser control. Verify:

1. Correct SDK UserControl composition factory/outer-inner identity contract.
2. Inherited UIElement/FrameworkElement and relevant content interfaces on the
   correct outer/inner objects; capture exact HRESULTs and identity relationships.
3. A simple visible child, layout/resize, construction through the intended
   compiled/runtime XAML path, and teardown/recreation on Mobile 15254.
4. Dependency-property binding with a dummy value before attaching any engine.

Investigate the failed 1.0.0.73–75 composition attempt; neither success nor
impossibility follows from those failures alone. A hand-returned IUIElement is
an interim embedding API, not fulfillment of declarative WebKitWebView. If the
minimal probe cannot satisfy the identity contract, report its evidence and
request a scope decision before substituting that interim API.

## Two further corrections to the current draft

### Redirect policy must not be weakened because a hook is missing

LoaderStrategyUWP passes redirects through ResourceLoader::willSendRequest.
Trace the full upstream callback path before asserting that navigation policy
cannot be re-entered. Keep a stable ID across hops. If app navigation veto is
promised, enforce it on redirect targets too, or state the limitation explicitly
and obtain a scope decision; checking only the initial URI can defeat an
application's HTTPS-only or origin-restricted policy. Internal WebCore
origin/security checks and public app policy are distinct obligations.

### TLS consent requires a distinct failure token

Do not use a predictable navigation ID alone as the consent token. Use an
opaque per-instance eligible-failure token; navigation ID is correlation data.
Validate error eligibility, target and freshness, consume the decision once,
and invalidate it on unrelated navigation or Close. Loading an app-owned warning
view must have explicitly defined token retention so NavigateToString does not
accidentally invalidate the only legitimate retry path.

Specify exception scope precisely: authorizing one retry is different from a
session-wide host exemption covering future navigations/subresources. The draft
currently mixes those ideas. Prefer authorization tied to the retried navigation
and its explicitly defined resource scope; redirects to other hosts remain
verified. No content-driven minibrowser: URL may authorize a bypass. Keep the
warning UX and consent decision in the app and enforcement in the engine.

## Order and completion criteria

1. Incorporate these decisions and inspect the actual response, redirect,
   title/history and destruction hooks. Do not silently drop previously promised
   features just because prototype plumbing is absent.
2. Implement/test the typed bridge and lifetime core; run the isolated XAML
   identity probe early to reduce architecture risk.
3. Build the NavigationTests reporting protocol/device runner and preserve
   internal renderer/font assertions as internal tests. Keep the existing
   PageUWP suite until replacement evidence is recorded.
4. Capture explicit successful WebCore/MiniBrowser build exit statuses before
   staging a device candidate; preserve ARM32 Skia Ganesh/ANGLE-D3D11.
5. Integrate the full XAML browser control only after the native contract,
   lifetime tests, fixture ownership plan and visual identity probe are proven.

No further global design pause is needed for the error or scroll choices above.
The next useful work is focused source inspection and preparatory bridge/probe
implementation. The full control remains gated on evidence.
