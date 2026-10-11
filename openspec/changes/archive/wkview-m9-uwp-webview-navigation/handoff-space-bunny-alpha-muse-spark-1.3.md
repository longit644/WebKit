# UWP WebKit handoff — Space Bunny Alpha / Muse Spark 1.3

Date: 2026-10-04  
Repository: `C:/Users/Longi/Workspace/WebKit`

## Read this first

The immediate task is **architecture and ownership design before implementing
the XAML WebKitWebView control**. The owner explicitly said to first decide what
belongs in the framework versus the application layer, and agreed the fixture
suite is not framework behavior.

`Tools/uwp/ARCHITECTURE.md` is the current working architecture note. GPT-6 Luna
wrote the initial boundary draft. The owner selected GPT-6 Sol for an
independent subagent review; Sol's findings have been incorporated into the
architecture note as unresolved gates and an implementation order. Do not
start building the XAML control until those design gates are resolved.

This handoff is the current continuation note for Space Bunny Alpha or Muse
Spark 1.3. Older notes (especially `handoff-2026-10-03.md` and
`handoff-muse-spark-1.3.md`) contain valuable historical runtime/history/debug
evidence, but their immediate next-step priorities and some device/build state
are stale. Keep their historical evidence; do not follow their old todo list in
preference to this architecture-first task.

## Owner requirements

- Keep the entire UWP implementation in scope, including `Tools/uwp/toolchain`.
- First decide and document framework / app / test-harness responsibilities;
  then extract fixture-only behavior; only after that design gate proceed to
  XAML control implementation.
- Preserve ABI values and distinguish framework errors, CURL errors, HRESULTs,
  navigation outcomes and diagnostic booleans.
- Do not discard or overwrite unfamiliar changes. The worktree has extensive
  uncommitted changes from prior sessions.
- Do not commit, amend, push or create a PR unless requested.

## Current architectural decision

The intended split is:

| Layer | Owns |
|---|---|
| `Source/WebCore/platform/uwp/` | Generic engine integration: loading, security enforcement, actual history, layout/paint, generic platform behavior. No fixture URLs, test phase machine, expected fixture text or test result protocol. |
| `Source/WebKit/UIProcess/API/uwp/` | Reusable native view/engine bridge, generic state/events, rendering/input/lifecycle plumbing. |
| WinRT/XAML control | Public reusable control identity, visual containment, layout/DPI/focus/lifecycle, navigation API and typed event projection. No MiniBrowser chrome or product policy. |
| `Tools/MiniBrowser/uwp/` | Address bar/toolbar, app configuration, error/interstitial presentation/template, user-facing application policy and consent UI. |
| `Tools/uwp/NavigationTests/` | Fixture server/content, device runner, test sequencing, assertions, expected outcomes and result collection. |
| `Tools/uwp/RuntimeSmoke`, `FpuProbe`, `TlsSmoke` | Standalone diagnostic applications and their probe orchestration. |
| `Tools/uwp/toolchain/` and build/package support | Shared platform/build infrastructure, separate from browser features and app UI. |

Architecture note: `Tools/uwp/ARCHITECTURE.md`.

## GPT-6 Sol review — findings to act on

The report approved the split in principle, but said the XAML control design is
not ready. The following are design gates, not optional cleanup:

1. **Reconcile public contract documents.** `design.md`,
   `design-winrt-component.md`, `public-api.md`, M9 specs, IDL and implementation
   conflict on source placement, `Source` dependency property semantics,
   supported API, redirects, terminal events, script execution, Close and
   sessions/multiple instances. Publish one supported/deferred contract and
   choose where reusable WinRT component source lives. Current code/IDL are
   under `Tools/uwp/WinRTComponent`; M9 `design.md` assigns public API/WinRT
   ownership to `Source/WebKit/UIProcess/API/uwp/`.
2. **Replace log-parsed navigation events with a typed native bridge.** The
   component currently infers events from diagnostic strings and a current
   active ID. Specify stable per-navigation IDs, typed URL/error/status data,
   policy cancellation, exactly-once terminal outcomes (including committed
   failures), source/history changes and engine-reported same-document
   classification. Diagnostics must observe events, not define the API.
3. **Define ownership/lifetime.** Specify idempotent Close, callback/policy
   context detachment, cancellation, teardown reentrancy, UI-thread contract,
   multiple controls and close/recreate. Remove the process-global raw instance
   routing before claiming general reusable control behavior.
4. **Move consent interpretation to the app.** The app owns gesture meaning and
   consent UI. The engine enforces only a narrow decision tied to the current
   eligible certificate failure/navigation. A content page must not trigger
   bypass by navigating to an app-reserved scheme. Current `PageUWP.cpp`
   intercepts `minibrowser:` before the policy callback; inspect and redesign
   this path, do not copy it as the final component contract.
5. **Prove XAML runtime identity before browser integration.** The IDL declares
   `WebKitWebView : UserControl`, but the current component does not compose the
   XAML base. Activation/QI for `IWebKitWebView` alone does not prove it can be
   created as a XAML element. First test inherited XAML interfaces, intended
   XAML/markup construction, visual layout, source binding and teardown on the
   target phone. If only a hosted visual is possible, name/document it honestly
   as an interim API.
6. **Specify app-supplied HTML semantics.** Define origin/base URL, effective
   Source, history, cancellation and event behavior for `NavigateToString`;
   current active-loader writer behavior is not a substitute-data contract.

See GPT-6 Sol's full independent report in conversation history. These items
are now in `Tools/uwp/ARCHITECTURE.md` under “Decisions required before
implementing the XAML control.”

## Fixture-suite extraction

The fixture runner currently embedded in WebCore is clearly test-only and must
move, but do not delete it before replacement coverage exists.

Primary code to extract/remove from `Source/WebCore/platform/uwp/PageUWP.cpp`:

- `PageHostUWP` fixture-specific phase/base/reload/scroll fields and
  probe-only load-generation state.
- `fixtureBodyText`, `probeFixtureMarkers`, settle retries/timers,
  `FixtureSuitePhase`, `scheduleFixtureSuite` and all fixture-specific DOM,
  cookie, font, redirect/history/CORS/CSP/stop/replacement/scroll assertions.
- `uwp-suite=1` trigger and fixture completion/timeout log protocol in
  `dispatchDidFinishLoad`.
- Per-load fixture marker and `#next` hit-test traces. Retain generic navigation
  lifecycle/failure events only.

`Tools/MiniBrowser/uwp/MiniBrowserUWP.cpp` also currently contains temporary
fixture gates, a hard-coded device fixture startup URL, component diagnostics and
blocked-template sequencing. Those belong in the test harness, not ordinary
MiniBrowser startup. The app itself still owns its normal toolbar and
blocked-page behavior.

Important constraint: the fixture suite reads private WebCore DOM and renderer
font internals. Those assertions cannot be moved unchanged into an app. Keep
renderer-private font/platform-data checks in internal WebKit tests. For
application-visible behavior use fixture self-reporting/server result endpoints
or observable public control events. The current
`Tools/uwp/NavigationTests/` has a deterministic HTTP server and host unit tests,
but **does not yet have a device browser runner/result protocol**. Make an
assertion-to-replacement table and establish replacement coverage before
removing the old suite.

Sol identified fixture-document gaps to correct as part of runner design:

- CSP inline-script check must really report whether the script executed.
- `/large` is currently `application/octet-stream`; it does not prove the
  main-document HTML budget unless adapted.
- Static heading text is not evidence that CSS color was applied; use actual
  visual/computed-style evidence.
- `fixture-suite-completed=1` must mean all assertions passed, not merely the
  state machine reached its end.

## Worktree and recent verification

There are many uncommitted modifications across WebCore, WebKit, MiniBrowser,
the ARM toolchain and OpenSpec. `git status --short` was inspected on 2026-10-04;
the worktree contains both modified and untracked files, including
`Tools/uwp/ARCHITECTURE.md`, the UWP skill, NavigationTests,
`Tools/uwp/WinRTComponent/`, `LoaderStrategyUWP.{h,cpp}`, generated MIDL files,
and several build directories. Preserve them. Nothing was committed.

Newly verified after fixing compile issues:

- `cmake --build build-arm-fpuprobe` — passed.
- `cmake --build build-arm-tlssmoke` — passed after ordering `winsock2.h`,
  `windows.h`, then `bcrypt.h` in `Tools/uwp/TlsSmoke/TlsSmoke.cpp`.
- `cmake --build build-arm-runtime-smoke` — passed after adding the WebCore UWP
  include directory to `Tools/uwp/RuntimeSmoke/CMakeLists.txt` (needed for
  `PageUWP.h`).
- Earlier source audit `python Tools/uwp/audit-source-docs.py` passed without
  diagnostics; NavigationTests host suite passed 8/8; Python syntax checks and
  `git diff --check` passed (only line-ending conversion warnings).

Build status needing explicit recheck before claiming final green:

- WinRT component target was reported green earlier in the prior session.
- The last `WebCore` build output was truncated without an explicit final exit
  status. Re-run/capture if code builds resume.
- MiniBrowser after the latest broad source edits also needs an explicit
  successful build confirmation. Previous build logs in the old conversation
  were truncated; don't infer a pass.
- Architecture docs do not affect code builds, but current full build status
  should be established before device packaging.

## Device/build environment

- Device Portal: `https://192.168.1.52` (use `--insecure`); phone Wi-Fi is
  intermittent.
- Fixture PC: `192.168.1.69`, ports `8002` and `8003` for same-origin/CORS
  tests. Do not use disconnected adapter `192.168.1.45`.
- Signing certificate thumbprint:
  `18FD65CF5E103CF96F04C2A451DC49C94788A40D`.
- Prior session had MiniBrowser 1.0.0.77 on-device, where packaged and
  LocalState blocked-template selection was logged. Confirm installed identity
  and rerun on the eventual new package rather than assuming it is current.
- Source environment before ARM builds: `. .\Tools\uwp\arm32-uwp-env.ps1`.
- Build only affected targets; default all-target Skia build has unrelated
  `TestWebKitAPI` gtest SEH failures. Keep Skia Ganesh/ANGLE-D3D11.
- Read `Tools/uwp/SKIA-PORT.md`, `Tools/uwp/WinRTComponent/README.md`, and older
  handoffs for toolchain/regeneration details.

## Recommended next actions

1. Review `Tools/uwp/ARCHITECTURE.md` and reconcile the M9 normative docs into a
   clear decision record: supported API now vs deferred, native bridge/event
   semantics, Close/multinstance/session ownership, consent decision flow, HTML
   loading semantics, and authoritative source placement. Do not implement the
   XAML control yet.
2. Create a per-assertion replacement plan for the current PageUWP suite and
   define the NavigationTests device runner/result protocol. Identify which
   checks remain internal WebKit tests. Keep the current runner until the
   replacements are verified.
3. Make the small fixture correctness fixes (CSP report, large HTML endpoint,
   CSS evidence and aggregate pass semantics) in the test layer as appropriate.
4. After those design decisions, probe XAML identity/composition/layout/close on
   device with the smallest control possible. Only then connect the browser and
   migrate MiniBrowser to host the reusable control.
5. Before device packaging, rerun explicit build targets and capture exit
   statuses; do not rely on truncated logs.

## Immediate model prompt

“Continue from `handoff-space-bunny-alpha-muse-spark-1.3.md`. First reconcile the
architecture/API design based on GPT-6 Sol's review and current source; do not
build the XAML browser control yet. Preserve the large uncommitted worktree.
Produce a specific framework/app/test responsibility matrix, resolve design
document contradictions, and map each in-framework fixture assertion to its
replacement before removing test code.”
