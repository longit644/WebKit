# PageUWP fixture-suite extraction plan

Status: replacement design; no runner code has been moved and the existing
device runner remains active until replacement coverage passes. Follow
`control-contract.md` for the public API and `Tools/uwp/ARCHITECTURE.md` for
layer ownership.

## Harness shape

`Tools/uwp/NavigationTests/` will contain the device browser harness as a
separate UWP test app, plus fixture server/content and host tests. The harness
will host the public WinRT control, use its typed URI/history/terminal events,
and own scenario timing, actions, retries, assertions, run IDs and result
collection. It will run only when explicitly launched; MiniBrowser normal
startup returns to the app's demo/default URI.

Fixture pages report observable script outcomes to a same-origin result
endpoint keyed by a generated run ID. The server records requests and reports
without logging cookie values or credentials. The harness validates structured
reports and native control events separately, then emits an aggregate pass/fail
summary. `suite-completed` is success only if every required assertion passed.

V1 intentionally has no `ExecuteScriptAsync`; the fixtures can self-report
JavaScript-visible results, and history pages can report their resulting URL and
state from script. Native operations (stop, replace navigation, Back/Forward,
Reload) are driven by the harness through the public control API. If an assertion
cannot be proven through those mechanisms, keep it an internal WebKit test or
defer it explicitly rather than adding a fixture-only WebCore API.

## Assertion replacement matrix

| Current PageUWP assertion/phase | Replacement and owner | Evidence / notes |
|---|---|---|
| `fixture-css-heading` checks that heading text exists | NavigationTests fixture + screenshot pixel/color comparison or a future generic computed-style script API | Text presence does not prove CSS applied. Prefer screenshot comparison for device coverage. |
| External script order (`first,second`) | Fixture JS reports order to same-origin `/report` | Keep document load and ordered script behavior covered by the actual page. |
| Fetch and XHR results | Fixture JS posts success/failure/result to `/report`; server records | Assert both server report and expected resource requests. |
| Cookie pass/fail | Fixture sets/echoes cookies, reports values as boolean only | Never persist/log cookie content; per-instance isolation is an internal/session integration test for v1. |
| Web-font `customPlatformData`, `isInterstitial`, glyph lookup | Internal WebKit font/platform test | These are renderer-private assertions and cannot move unchanged into NavigationTests. Optional device screenshot validates visible fallback/appearance separately. |
| Delayed settle probes at 300/1200/3000 ms | Harness waits for explicit fixture report with a bounded timeout | Remove probe-only load generation state when probes are removed; retain generic navigation IDs/generations needed for correct engine cancellation. |
| Redirect phase and final query-free index URL | Harness observes `SourceChanged` and terminal event for same stable navigation ID | Add redirect marker/target assertion; every redirect hop uses that ID. |
| Navigate index → second; Back/Forward availability and document | Harness drives navigation and Back/Forward; checks Source, typed event sequence and history properties | Do not use shell URL history. |
| Reload request and reloaded page | Harness calls Reload and matches a fresh document completion/report for the same effective URL | Reload must not create a spurious history entry. |
| Same-document fragment, scroll, then Back | Harness activates in-page anchor and invokes Back; fixture reports `location`/`scrollY` on hash/pageshow | Native scroll restoration needs visible/UI or generic observable evidence; do not inspect WebCore internals in app. |
| 404 document, forward-history truncation, Latin-1 decoding | Harness checks typed completion HTTP status, Source/history state; fixture reports decoded `café` marker | 404 HTML is a completed HTTP document, distinct from transport failure. |
| CSP inline-script/image blocked | Test external/report endpoint remains untouched by denied inline script; server request log confirms image was not requested; harness confirms document marker | Update fixture so the PASS/FAIL oracle actually distinguishes execution; don't rely on C++ DOM text inspection. |
| Cross-origin CORS allow/deny | Fixture JS reports allow success and deny rejection to its own origin; second server logs requests but not sensitive headers | Assert response exposure behavior, not just server response generation. |
| Slow-page replacement | Harness waits for the main slow document, starts replacement, then asserts old navigation receives one Cancelled terminal outcome and new document remains current | Resource server records whether delayed response arrived; no stale events may mutate the new page. |
| Stop while slow resource pending | Harness calls Stop after a start marker, checks one Cancelled outcome and unchanged current document; server records delayed request completion | Separate main-navigation cancellation from subresource cancellation. |
| `pushState`/Back/Forward | Fixture runs pushState and reports URL/state; harness traverses using native history APIs and checks SourceChanged/HistoryChanged | Same-document mutations need engine-provided classification; do not infer via URL fragment stripping. |
| `replaceState` does not push a history entry | Fixture performs push then replace, reports `location`; harness Back/Forward confirms the intermediate `?v=1` entry is absent | Exercise actual engine history, not a JavaScript-only expected result. |
| Final suite completion | Device harness computes aggregate result from all assertions and writes one structured summary | Exit/final log is failure if any required check fails or times out. |
| `#next` DOM hit-test traces in ordinary load completion | Remove from framework load path; retain only in a dedicated interaction test if needed | Not general navigation behavior. |

## Phase/marker coverage check

Every `FixtureSuitePhase` value in `PageUWP.cpp` (0–28) and every
`probeFixtureMarkers` field must map to a row above before removal:

| Source | Covered by |
|---|---|
| Phases 0–1 (`IndexProbes`, `Redirect`) and the per-load/settle probes | marker rows for CSS/script/fetch/XHR/cookie; settle-timeout row; redirect row |
| Phases 2–5 (`HistoryBack`, `HistoryForward`, `HistoryReload`, `HistoryBackFallback`) | index→second navigation row; reload row; 404/truncation row (fallback shares the missing-page checks) |
| Phases 6–8 (`Latin1`, `ContentSecurityPolicy`, `CrossOrigin`) | 404/Latin-1 row; CSP row; CORS row |
| Phases 9–11 (`ReplacementPending/Stabilize/Stable`) | slow-page replacement row |
| Phases 12–13 (`StopPending`, `StopStable`) | Stop row |
| Phases 14–17 (`ReloadDocument`, `SameFragment`, `SameBackDocument`, `HttpError`) | reload row; fragment/scroll/Back row; 404/truncation/Latin-1 row |
| Phases 18–20 (`ScrollBefore`, `ScrollBack`, `ScrollRestored`) | fragment/scroll/Back row plus the explicit scroll-restoration note below |
| Phases 21–23 (`PushState`, `PushBack`, `PushForward`) | pushState row |
| Phases 24–27 (`ReplaceSetup`, `ReplaceState`, `ReplaceBack`, `ReplaceForward`) | replaceState row |
| Phase 28 (`Done`) | aggregate completion row |
| `fixture-web-font-custom/interstitial/glyph` | internal WebKit font test row |
| Diagnostic-only counts (`history-back-count`, `branch-forward-count`, `history-*-document`) | public `CanGoBack`/`CanGoForward`/`Source` observation; not privileged assertions |
| `#next` hit-test traces | interaction-test-only row |

Two items need explicit decisions rather than a mechanical mapping:

- **Scroll restoration** has no public observation surface in v1 (no scroll
  offset API and no `ExecuteScriptAsync`). Decide between adding a justified
  generic observable scroll property, driving it through fixture-reported
  `scrollY`, or deferring with an explicit gap statement. Do not keep a
  fixture-only framework probe to preserve it.
- **`fixture-cookie-fail`** is meaningful only alongside `fixture-cookie-pass`;
  both must be reported as booleans so a regression in one is distinguishable.

## Fixture/test corrections before claiming replacement coverage

1. Add a same-origin result endpoint and run-ID isolation; unit-test its
   structured payloads and aggregate failure behavior.
2. Fix CSP fixture oracle: the blocked inline script cannot write a marker. Use
   endpoint non-arrival or a separate allowed harness signal to demonstrate
   non-execution; verify the fixture itself differentiates a regression.
3. Add a large `text/html` main-document route for document-budget tests. Keep
   the existing octet-stream route as a separate unsupported/download policy
   test.
4. Replace static heading-text checks with screenshot evidence or a supported
   generic script/state API when one is added.
5. Keep cookie reports boolean/structural; never include cookie values,
   credentials or sensitive headers in logs/results.
6. Keep private font backend assertions in an internal WebKit test target.

## Safe removal order

1. Implement and host-test result reporting, assertion aggregation and fixture
   oracle corrections.
2. Add the NavigationTests device harness and typed public-event assertions.
3. Run the old and new device suites against the same package/server; compare
   outcomes and investigate discrepancies.
4. Mark each matrix row covered, internal-only or explicitly deferred with
   evidence.
5. Only then remove `fixtureBodyText`, `probeFixtureMarkers`, settle timers,
   `FixtureSuitePhase`/`scheduleFixtureSuite`, suite fields, `uwp-suite=1`
   startup/log gating and fixture-specific completion/hit-test logs from
   `PageUWP.cpp`/MiniBrowser. Leave ordinary navigation, history, rendering and
   failure behavior intact.
