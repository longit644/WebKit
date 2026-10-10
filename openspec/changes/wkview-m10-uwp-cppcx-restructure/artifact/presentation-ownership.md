# M10 6.6: presentation, service and input ownership

## Ownership table

All state below is per control unless marked process-shared. Every mutating
path enforces its constructing owner thread first.

| Owner | Owns | Lifetime | Must never own |
|---|---|---|---|
| `SwapChainPresenterUWP` | CPU D3D device/context/upload/swap chain; borrowed panel QI; GPU visual + copied table/page | Explicit detach; Pending blocks replace until acknowledged | Engine ANGLE/Skia objects, caller pixels, a second surface |
| `FrameMetricsUWP` | Bounded pacing/paint windows, millisecond values, microsecond/milli-Hertz reports | Navigation reset clears work and stage totals together | GPU timestamps, display scan-out, refresh discovery |
| `FrameSchedulerUWP` | Per-view lease, visibility, frame epochs | Ordinary close releases only its lease | Peer leases, engine code |
| `OwnerThreadServicePumpUWP` (process) | Copied service table, deduped frame epoch, suspension | Process-pinned; last lease ends demand, never unloads code | Page state, XAML targets |
| `FrameSchedulerDriverUWP` (private CX) | One timer, one Rendering token, weak client ports, coalesced wakes | Last client revokes both hooks | Control lifetimes |
| `RenderSubscriptionUWP` | Exactly one Rendering token across load/unload/suspend/failure/Close | Close revokes; failure disables resubscribe | Page, presenter, service state |
| `InputAdapterUWP` | Gesture classification, tap slop/offset, capture-cancel | Press/release/abort per gesture | Engine calls, capture itself |
| `ScrollControllerUWP` | Kinetic/edge physics, pending deltas, velocity, springs | Navigation reset clears all motion | Document state, presentation |
| `PageControllerUWP` | Copied table, page handle, Pending close | Acknowledged release ends binding | WebCore objects, spans past the call |
| MiniBrowser shell | Toolbar/status UI, curl fetch probe, suspend/resume forwarding | Process lifetime | Engine history, completion oracle |

## No network-completion inference

`Tools/uwp/build/check-presentation-separation.py` gates the owned boundary
(`rendering/`, `input/`, `native/`, `platform/`): no log-scraped completion, no
fixture URLs or suite triggers, no loader/DOM includes, no downloader
references, and terminal payloads only from the real control. It passes on the
real tree; six negative unit tests (`test_presentation_separation.py`) prove
each clause fires.

Behaviorally, the 6.2 phone oracle completes delayed CSS, scripts, timers and
fetches on two pages with **zero** presentation callbacks outstanding, and the
scheduler suite fails closed if any DOM result is missing. Terminal outcomes
exist only as structured-engineering obligations created before dispatch (5.x),
never as rendering observations.

The legacy shell's curl fetch probe (`Tools/MiniBrowser/uwp/MiniBrowserUWP.cpp`,
`curl/curl.h`, worker `FetchRequest`, per-frame `consumeFetchResult`) is
confined to the sample app and stays owned by task 8.4 for removal. No file
under the new control boundary references curl.

## Partial presentation evidence (6.1–6.5)

- 6.1: 49/49 ownership/metrics checks, both ARM compilers; Lumia CPU
  presentation and Skia baseline (`swap-chain-presenter.md`).
- 6.2: 34/34 native plus 22/22 phone scheduler checks with 198/198 regressions;
  paused resources progress with zero frames (`shared-frame-scheduling.md`).
- 6.4: 54/54 input laws plus v42 tap/scroll/fling interaction
  (`input-controllers.md`).
- 6.5: 35/35 subscription laws plus v43 power-button suspend/resume cycles
  (`lifecycle-subscriptions.md`).
- 6.3 resize: EGL revision sync both directions, single exclusive attachment,
  full native capture (`resize-recovery.md`); landscape shell overlay and
  lost-device recovery remain open there.

## Combined G4 stays pending

Per the acceptance matrix, G4 requires all of 6.1–6.6 **and** 7.1–7.5. Section 7
(runtime owner, dispatcher/clipboard services, projection review, capability
reporting, cleanup tickets/quiescence) is entirely unchecked. Nothing here
claims it.
