# M10 6.3: size/DPI/lost-device recovery with one rendering-child owner

## What changed

- `SwapChainPresenterUWP` gains `deviceRemovedReason()` (S_OK healthy,
  E_ILLEGAL_METHOD_CALL without a host-owned device) and `recoverDevice()`:
  the dead chain leaves the panel before its replacement is created, so the
  visual never owns two chains; device, context, chain and upload are rebuilt
  on the retained panel, geometry and composition scale; any rebuild failure
  lands Detached with nothing attached. Rebuilds even when healthy — shells
  call it after `DXGI_ERROR_DEVICE_REMOVED`/`DEVICE_RESET` or to reassert a
  known-good device. `deviceRecoveries()` counts rebuilds over the lifetime.
- `WebKitWebViewUWP::repaint`, `advanceFrame` (cached path) and
  `useFallbackPresenter` (resize path) map device-removed/reset failures into
  one `recoverDevice()` plus a single retry, reporting
  `control-presenter-recover`/`control-presenter-recovered`. No other failure
  shape triggers a rebuild.
- `WebKitWebViewUWP::recoverPresenterForDiagnostics()` (following the
  `savePNG` diagnostic precedent): forces CPU fallback, presents, rebuilds
  the real D3D device, presents again, reporting reason-before, each stage,
  the recovery count and reason-after. Leaves the compositor disabled;
  rotation/matrix checks run first. The MiniBrowser shell gains a Recover
  button beside Capture that runs it.
- The `build-arm-minibrowser` tree was reconfigured onto the current toolchain
  (SDK 10.0.19041.0, no `msvcurt.lib`): its cache still referenced the removed
  22621 SDK from before the task 2.2 repin and could no longer link. No
  CMakeLists change was needed; evidence trees under it are untouched.

## Host checks

`test-rendering-contract.ps1` with a fresh component directory:
**67/67** (49 existing + 18 recovery), ARM MSVC + clang-cl separation clean.
Recovery checks cover detached/mode-gated/worker-thread rejection, healthy
reason, on-demand WARP rebuild with geometry/scale retention, exactly one
live attachment (old chain detached first, panel references stable),
presentation resuming on a lazily recreated upload, counted second rebuild,
resize after recovery and detach to baseline. CoreWindow-mode rebuild has no
reattachable panel and stays rejected; real removal events are not triggerable
on WARP or on the phone, so the rebuild path itself is what is proven.

## Device evidence (v49)

Lumia 950 XL, `mc-001173b46c8b`, `MobileCoreProd`, OS
`15254.603.armfre.feature2_rs3svc.200106-1633`, portal `https://192.168.1.52`.
Package `WebKitProbe.EngineBrowser_1.0.0.49_arm__2932e6rj4m41p` (1.0.0.43 was
already installed from the 10/3 era, so the run took the next free version).
Install Code=0/Success=true after reconnect, launch HTTP 200, fixture server
`http://192.168.1.69:8002` serving. The v39 fixes were re-verified on the
current tree before the drill.

Rotation portrait → landscape → portrait → landscape → portrait → landscape →
portrait, pausing without tap/scroll:

```text
control-viewport-width/height: 411x663 -> 731x343 -> 411x663 -> 731x343 -> 411x663 -> 731x343 -> 411x663
gpu-surface-resize revision=2..8 css=731x343/411x663 scale=3.500 target=2559x1201/1439x2321 attempt=1 ready=1
```

Every resize S_OK, every surface sync `ready=1` first attempt, correct CSS,
scale and backing targets both directions. One `control-compositor-initialize`,
one exclusive GPU attachment, no duplicate swap chain, no unresolved surface,
no CPU fallback during rotation, no frame/render failure.

Recovery drill (Recover button, last):

```text
control-presenter-reason-before: 0x8000000e (E_ILLEGAL_METHOD_CALL, no fallback device yet)
control-presenter-fallback-disable: 0x00000000
control-cairo-fallback: 0x00000000
control-presenter-fallback-setup: 0x00000000
control-presenter-before: 0x00000000
control-presenter-recover: 0x00000000
control-presenter-recoveries: 0x00000001
control-presenter-after: 0x00000000
control-presenter-reason-after: 0x00000000
```

Fallback forced, frame presented, real D3D device torn down and recreated,
presentation resumed, device healthy after — exactly one recovery. Rendering
continued after the drill (further resize S_OK). The post-recovery `page.png`
capture shows the full correct layout including the sticky top layer and the
bottom-right fixed layer, proving layout, scale, pinned layers and pixels all
survive a real-driver rebuild.

## Still open (not 6.3)

**Landscape system navigation-bar overlay in the sample shell**, confirmed
still present by the owner on v49: the panel reports full-window width while
the system bar covers content. Engine output is proven correct (rotation
targets, native captures); the chrome belongs to the shell. Proper
`VisibleBoundsChanged` handling needs the typed delegate, which has no native
MIDL specialization for WRL — it stays with the C++/CX app-shell migration
(task 8.1), where the subscription is ordinary CX code.

SHA256:

| Payload/report | SHA256 |
|---|---|
| Signed v49 APPX | `C22FC5BEF12B3F02A71D123D57B554FD1C514FD3787A85C296FCFCB5770F323C` |
| MiniBrowserUWP.exe (staged) | `8BD20B6FF30304EA51233A367B6579ADB3A2688AA95A003C631EAD7ED2756802` |
| Legacy WebKitWebView.dll (staged) | `D396FEC2A8925E85D9BE3A3D925E3A7A673848BA813C4144D408B588579DB024` |
| Actual WebCore.dll (staged) | `95B7AF88486FBBADD380FF38E735DDB10541883BDB1F974206DFE7AB55A783DE` |
| Landscape/recovery log, 13054 bytes | `76AE960352F43344EF46DDAF334128CD2861740A060E1BAF64F789E26968802F` |
| Native page.png, 228046 bytes | `10B50A0967B2076566AE91903BAC951C2C508CE3ED8D0ADF9F2AE66DCD7AE127` |

## Bonus: OVERRIDE page is expected diagnostics

Unchanged from v39: closed-port fixture pages exercise the packaged
blocked template, not product failures.
