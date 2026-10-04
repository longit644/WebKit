# Handoff to Gemini Flash — UWP WebKit architecture

Date: 2026-10-04  
Repository: `C:/Users/Longi/Workspace/WebKit`

## Immediate instruction

Continue the architecture-first task. **Do not implement or build the XAML
WebKitWebView control yet.** Review the draft decisions below against current
source and reconcile them before coding. Preserve the extensive uncommitted
worktree; do not reset, commit or push.

## Read first

- This handoff
- `Tools/uwp/ARCHITECTURE.md`
- `openspec/changes/wkview-m9-uwp-webview-navigation/control-contract.md`
- `openspec/changes/wkview-m9-uwp-webview-navigation/fixture-extraction-plan.md`
- M9 `design.md`, `design-winrt-component.md`, `public-api.md`, specs and
  `tasks.md`
- `Source/WebCore/platform/uwp/PageUWP.cpp`,
  `Tools/uwp/WinRTComponent/WebKitWebViewComponent.cpp` and IDL

The owner selected GPT-6 Sol for independent architecture review. Its findings
are summarized in `handoff-space-bunny-alpha-muse-spark-1.3.md` and incorporated
as draft gates/decisions in the docs above.

## Draft decisions made in the latest session

- Framework owns generic engine/navigation/history/rendering/input behavior;
  MiniBrowser owns chrome and product UX/policy; NavigationTests owns fixtures,
  device-runner sequencing and assertions; toolchain is a separate platform
  layer.
- Intended authored reusable WinRT/XAML source location is
  `Source/WebKit/UIProcess/API/uwp/WinRT/`; `Tools/uwp/WinRTComponent/` should
  own generation/build/packaging support. Existing prototype files have **not**
  been moved.
- `control-contract.md` is a draft v1 authority: typed instance-scoped native
  event bridge; one terminal event per navigation; multiple controls with
  independent ephemeral sessions; idempotent Close; explicit app TLS-consent
  decision bound to a current certificate failure; opaque-origin
  `about:blank` semantics for `NavigateToString`; several features deferred.
- `fixture-extraction-plan.md` maps current `PageUWP.cpp` assertion families to
  proposed NavigationTests device-harness or internal WebKit coverage. No
  runner code has been moved. Keep the existing test runner until replacements
  pass.
- XAML identity/markup/layout/teardown on Windows 10 Mobile remains an explicit
  feasibility gate before browser integration.

## Files changed in this architecture pass

New:
- `openspec/changes/wkview-m9-uwp-webview-navigation/control-contract.md`
- `openspec/changes/wkview-m9-uwp-webview-navigation/fixture-extraction-plan.md`
- `openspec/changes/wkview-m9-uwp-webview-navigation/handoff-gemini-flash.md`

Updated:
- `Tools/uwp/ARCHITECTURE.md`
- M9 `README.md`, `design.md`, `design-winrt-component.md`, `public-api.md`,
  `tasks.md`, and control/navigation/resource-loading specs.

All these edits are uncommitted. The broader existing worktree contains many
other user/session modifications. Inspect `git status --short` before editing.

## Work still required

1. Independently verify the draft v1 decisions against code and applicable UWP
   APIs. Correct any unsupported assumption before calling the contract
   authoritative. In particular review error domains/status, redirect IDs,
   cancellation/terminal ordering, close reentrancy, UI-thread behavior,
   XAML type identity and TLS-consent token design.
2. Check cross-document consistency. Old `design-winrt-component.md` is marked
   historical/prototype, but its body still contains rejected log-parsing,
   single-instance, TLS scheme-interception and assumed-UserControl guidance;
   keep the warning prominent or revise/remove those sections. Confirm all M9
   SHALLs and task checkboxes agree with v1/deferred scope.
3. Inspect `fixture-extraction-plan.md` against every `FixtureSuitePhase` and
   `probeFixtureMarkers` assertion in `PageUWP.cpp`. Make replacements concrete
   enough to implement without private DOM/font hooks. Fix CSP oracle, large
   HTML endpoint, CSS visual evidence and aggregate pass semantics in the test
   layer when appropriate.
4. Review links/format and run OpenSpec validation if available. This pass was
   documentation-only; no XAML or engine code/build was performed.
5. Report decisions and remaining blockers to the owner before beginning the
   XAML control.

## Known repository/build context

- Worktree is large and uncommitted; user instructed preserve all changes.
- Last recent builds that were explicitly verified: `build-arm-fpuprobe`,
  `build-arm-tlssmoke`, `build-arm-runtime-smoke` passed. A component build had
  passed earlier; recent WebCore/MiniBrowser exit statuses require explicit
  recapture before claiming final green.
- NavigationTests host suite previously passed 8/8. There is no device browser
  runner/result protocol yet.
- Keep ARM32 UWP with Skia Ganesh/ANGLE-D3D11. Device Portal
  `https://192.168.1.52`; fixture host `192.168.1.69` ports 8002/8003;
  certificate thumbprint `18FD65CF5E103CF96F04C2A451DC49C94788A40D`.
- More exact history/runtime/toolchain evidence is in
  `handoff-space-bunny-alpha-muse-spark-1.3.md` and the earlier historical
  handoffs; use them for context, not as the current task priority.

## Suggested opening prompt

“Continue from `handoff-gemini-flash.md`. Review the draft control contract and
fixture extraction plan against the M9 specs and current code. Reconcile
contradictions and identify unsupported assumptions, preserve all uncommitted
work, and do not implement the XAML control until the owner-facing design gates
are resolved.”
