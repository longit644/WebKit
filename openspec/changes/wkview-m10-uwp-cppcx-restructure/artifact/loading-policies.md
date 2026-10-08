# M10 4.9 - Bounded loading policies with distinguishable failures

Concurrency, decoded-byte, transient-buffer and deadline policies enforce the
configured ranges with session scope; queue overflow, budget exhaustion and
redirect failure stay distinguishable by code, and explicit suspension pauses
deadlines while unload never does. Proven on the Lumia, including two
build-system and timing findings the device runs forced.

## Delivered

- `Source/WebCore/platform/uwp/ResourcePolicyUWP.h/.cpp` - contract
  defaults (8 concurrent, 32 MiB per resource, 16 MiB session transient,
  30 s deadline, 128 queued, 10 redirects), range validation, per-session
  overrides plus a fixture-only unvalidated hook, and the transient
  in-flight account with exact charge/release balance.
- `Source/WebCore/platform/uwp/ResourceLoadUWP.h/.cpp` - policy-driven
  redirect budget with hop logging, pre-copy resource/transient checks
  (codes 11/13), enqueue-based deadline checks plus a re-arming timer
  (code 28, the curl timeout value), and provisional-owner attribution for
  commit/failure signals.
- `Source/WebCore/platform/uwp/LoaderStrategyUWP.h/.cpp` - per-session
  concurrency FIFO with queue-overflow code 14, suspend/resume nesting with
  deadline exclusion, and slot-aware restart of queued loads.
- `NavigationControllerUWP` - typed `transportError` per attempt for
  distinguishable main-frame failures, reset per provisional.
- New adapter codes 13 (transient overflow) and 14 (queue overflow) beside
  the existing 11 (resource budget) and 8 (redirects).
- Suite phases 67-77: defaults, range validation, redirect/resource/
  transient/timeout codes, suspend survival, restore health.

## Verification

```
cmake --build build-arm-uwp-fresh --target WebCore --parallel 8
```

- Fresh-tree `WebCore` links: ARMNT, AppContainer, 25 exports.
- Device run `WebKitProbe.EngineBrowser 1.0.0.27`, log
  `m10-engine-49-run-v27.log` (179754 bytes), all true:
  - `budget-defaults`, `budget-ranges` (out-of-range rejected, policy kept).
  - `budget-redirect-code` (loop dies with code 8; hop count in the
    transport log beside it).
  - `budget-resource-code` (976-byte doc fails a 512-byte fixture cap
    with code 11; restore commit proves health).
  - `budget-transient-code` (same doc fails a 512-byte fixture buffer
    with code 13).
  - `budget-timeout-code` (2 s delayed doc fails a 1500 ms fixture
    deadline with code 28).
  - `budget-suspend-survived` with outcome 1: the delayed transfer
    committed despite 2500 ms suspended under a 1500 ms deadline, with
    loop, active and commit IDs all agreeing (`0x28`).
  - No `timeout`, `suite-completed: 1`.
- Findings on the way, both fixed and re-proved here:
  - v20/v21 startup crash: incremental objects missed UWP header growth
    (proven via `ninja -t deps` for EngineUWP). `PlatformWin.cmake` now
    rebuilds every UWP translation unit on any UWP header change.
  - Suspend-timer race: the deadline timer could fire mid-suspend before
    resume extended it. Request age now subtracts recorded suspension
    spans overlapping the transfer's own lifetime, exact under all
    interleavings; the timer reschedules while the clock is held.
  - Missing `fixtureLoopId` capture made three runs assert the previous
    navigation's ID; ID-value markers now accompany every verdict marker.

Final DLL SHA-256:

```text
4.9 WebCore.dll: 73DE5406C1A4D181096BD90CA02211CA15B9A2DE9FD09277B0A137CF399C0771
```

## Not claimed

- Queue-overflow code 14 is implemented and range-checked but unreachable
  on device: WebCore and connection limits serialize long before 129
  concurrent session transfers.
- Retry-execution success after consume still needs a TLS fixture (4.8).
- `native-fault-code 0xe06d7363` fires as in 4.1-4.8 runs; the suite still
  completes. Still unexplained, unchanged by 4.9.
