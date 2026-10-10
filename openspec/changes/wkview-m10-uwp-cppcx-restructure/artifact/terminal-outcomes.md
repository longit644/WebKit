# M10 4.4 - Exactly-once terminal outcomes for navigations

Every accepted navigation ID reaches one terminal (commit, veto, stop, replacement, provisional or committed failure); late or repeated signals for settled IDs are dropped and counted, never re-recorded. Proven on the Lumia, including a real attribution bug the suite caught and the fix re-proved.

## Delivered

- `Source/WebCore/platform/uwp/NavigationControllerUWP.h/.cpp` - new `NavigationOutcomeUWP` (None/Committed/Vetoed/Stopped/Replaced/ ProvisionalFailure/CommittedFailure) plus a bounded 32-entry terminal ledger: `beginNavigation` marks an unsettled predecessor Replaced, `cancelNavigation` records synchronous vetoes, commit/failure paths record through it with duplicate-drop counting, and `terminalOutcome` / `duplicateTerminalSignals` expose it for assertions.
- Attribution guards (found necessary by the device run, see below): commit and provisional-failure signals attribute to the provisional owner, not the active ID, because a superseded load's death rattle routinely arrives after its replacement already began; committed failures attribute to the ID that committed the current source, with older-document failures counted stale via `noteStaleSignal`.
- `Source/WebCore/platform/uwp/FrameLoaderClientUWP.cpp` - navigation and response policy vetoes record Vetoed synchronously; provisional failures classify cancellation as Stopped; `dispatchDidFailLoad` records CommittedFailure only for the current source; `dispatchDidFinishLoad` reports (never records) a finish landing on a canceled ID.
- Suite phases 29-38 (numbers appended, never renumbered): same-turn replacement race, host-veto plus reentrant overlap, redirect-loop failure, attachment-download veto, stop-after-commit, and delayed re-verification.
- `Source/WebCore/platform/uwp/PageHostUWP.h` - fixture ID fields for the veto/loop/download/race/replaced probes.

## Verification

```
cmake --build build-arm-uwp-fresh --target WebCore --parallel 8
```

- Fresh-tree `WebCore` links: ARMNT, AppContainer, 25 exports.
- Device run `WebKitProbe.EngineBrowser 1.0.0.7`, log `m10-engine-44-run-v7.log` (132557 bytes), all true:
  - `replaced`, `race-good`, `replaced-delayed`, `replaced-final`.
  - `vetoed`, `veto-stable` (reentrant good commit resurrects nothing).
  - `loop-failed`, `loop-nocommit`, `download-vetoed`, `download-nocommit`.
  - `replacement-committed`, `stop-committed` (Stop cannot rewrite a commit).
  - `terminal-duplicates: 7` stale signals absorbed; no `late-success`, no `timeout`, `suite-completed: 1`.
- The v1.0.0.6 run caught a genuine bug: the race loser's cancelled failure landed on the already-begun winner (`race-good: 0`, confirmed in the log sequence). The provisional-owner attribution fixed it; v1.0.0.7 proves `race-good: 1` with the loser still Replaced at the end.

Final DLL SHA-256:

```text
4.4 WebCore.dll: 18C1E0E1FA6BAE1DB6229F54311E2C2AD7DB1E574F21E4491612D573A1911D69
```

## Not claimed

- Async-veto landing after the next command began would misattribute to the newer ID; the observed veto is synchronous and the suite fails loudly (VetoConfirm gate plus `veto-stable`) if that ever changes. Not papered over: no async-veto path was observed on device.
- `native-fault-code 0xe06d7363` fires as in 4.1-4.3 runs; the suite still completes. Still unexplained, unchanged by 4.4.
- CommittedFailure classification is wired and guarded but no deterministic fixture produces one yet; the teardown-race path is implemented, not device-proven.
