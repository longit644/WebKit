# M10 4.5 - Transient substitute-data host HTML

App-supplied markup displays as a history-hidden `about:blank` substitute document with opaque origin, its own ledger ID, and kind-driven failure-token semantics: warnings preserve the recorded failure for proceed, generic markup invalidates it. Proven on the Lumia, including two behaviors the first device run corrected.

## Delivered

- `Source/WebCore/platform/uwp/TransientPageUWP.h/.cpp` - `show(kind, html)` loads `about:blank` substitute data with `SessionHistoryVisibility::Hidden` and an EMPTY failing URL, so `urlForHistory()` yields empty: no entry is added and the current item is left alone (a non-empty failing URL would keep a history entry by unreachable-page design). The host veto is consulted synchronously in `show` because substitute loads skip loader policy; refusal records Vetoed and changes nothing else.
- `NavigationControllerUWP` - `setFailurePreserved` arms preservation for the next commit; `noteDocumentCommit` honors and consumes it, so warning commits preserve the failure while every other commit invalidates it.
- `FrameLoaderClientUWP` - commit hook flips Starting to Shown on `about:blank` commits and dismisses to Idle on ordinary ones.
- Suite phases 39-47: fresh loop failure, warning show (origin/kind/commit/ preservation/history/cursor), vetoed generic show (nothing changes), generic show (invalidation), reload, dismiss back to index.
- `PageHostUWP` - owns the transient controller plus the failure/history/ cursor snapshot fields.

## Verification

```
cmake --build build-arm-uwp-fresh --target WebCore --parallel 8
```

- Fresh-tree `WebCore` links: ARMNT, AppContainer, 25 exports.
- Device run `WebKitProbe.EngineBrowser 1.0.0.9`, log `m10-engine-45-run-v9.log` (134496 bytes), all true:
  - `failure-armed`, `blank-origin` (null), `kind` (Warning/Shown), `committed`, `warning-preserved`, `history`, `cursor`.
  - `vetoed`, `veto-stable` (warning text/kind/state/failure/history kept).
  - `generic-origin`, `generic-invalidated` (failure empty), `generic-history`.
  - `reload-history`, `reload-cursor` (re-anchored to blank), `reload-committed`.
  - `reload-body: 0`: reload drops substitute data and yields an empty blank document; logged, not asserted, as the specified reload semantic.
  - No `timeout`, `suite-completed: 1`; every 4.3 and 4.4 marker still true in the same run (23/23 allocations fresh, duplicates absorbed, no late success).
- The v1.0.0.8 run proved the corrections: substitute loads never consult loader policy (no `navigation-policy: about:blank` in the log), and a non-empty failing URL adds a history entry. Both fixed and re-proved here.

Final DLL SHA-256:

```text
4.5 WebCore.dll: 672358EA3EFFB1C10E6010709A63D97B132C78194E98A9C0C4805F2CC1BC6833
```

## Not claimed

- No public export for hosts: the suite drives the controller directly. Host reachability (NavigateToString and friends) is section 5 work.
- `WKLoadHTMLUWP` keeps its frozen V1 writer behavior untouched; transient display is a separate path, not a change to it.
- `native-fault-code 0xe06d7363` fires as in 4.1-4.4 runs; the suite still completes. Still unexplained, unchanged by 4.5.
