# M10 6.4: input adapter and scroll controller extraction

## Implemented ownership

`input/InputAdapterUWP.h/.cpp` owns per-control gesture classification: press
tracking, tap-slop drag detection (10 CSS pixels), tap synthesis at release
coordinates offset above held overscroll, and pointer-capture-cancel abort.
It drives the caller's `ScrollControllerUWP` for deltas/velocity and the
caller's `TapEngineUWP` for press/release pairs. Capture itself stays with the
XAML owner; no XAML event types cross the boundary, so host tests drive plain
coordinates and timestamps.

`input/ScrollControllerUWP.h/.cpp` owns per-control kinetic/edge physics with
the inherited constants and formulas: 0.2 s velocity window, 5000 px/s sample
clamp, 0.035 s blend, 60 px/s fling floor with 0.1 s recency, 1.12 boost clamped
at 5500, 4.2 friction with 20 px/s stop, 256/32 edge spring on 1/120 s substeps,
0.4/(1+|over|/36) static and 0.2 kinetic resistance with a 56 px clamp, and a
0.2 kinetic spring transfer. Wheel deltas queue plain motion without elastic
arming. Visual unload never pauses physics; explicit suspension is diagnosed by
the stall reset. Timestamps are caller-supplied monotonic seconds, so tests own
time exactly. The report sink must not throw (native no-exception domain).

Both classes capture their constructing owner thread and reject workers with
`RPC_E_WRONG_THREAD` before any state or engine access. Neither allocates per
frame; both are non-copyable with exactly one release owner.

`native/PageControllerUWP` gains `sendInput(const WKUWPInputEvent&)` and
`setFocused(bool)`. Validation follows the frozen header: table prefix, kind
range (six frozen kinds; the header has no Cancel/Character kinds), the four
defined modifier bits, finite pointer/wheel coordinates, nonzero virtual key
for key events, and scalar range excluding surrogates when nonzero. Focus and
input delivery require the `Input` capability; the real engine delivers neither,
so those paths return explicit `E_NOTIMPL` instead of a silent no-op. Missing
slots report revision mismatch; originating negative HRESULTs pass through.

The native wrapper delegates press/move/release/cancel/wheel and per-frame
motion to the two classes through narrow engine ports, keeping every diagnostic
stage byte-identical (`control-pointer-pressed/released`, `ptr-*`,
`control-inertia-start/stop`, `control-edge-hit/returned`, `control-scroll-y`).

## Checks and commands

On 2026-10-10/11, PowerShell 7.6.6, MSVC 14.44.35207, SDK 10.0.19041.0,
native ARM clang-cl 19.1.5:

```powershell
.\Tools\uwp\build\test-input-controller.ps1 `
  -ComponentDirectory "$env:TEMP\opencode\m10-scheduler-62-production-v2" `
  -OutDir "$env:TEMP\opencode\m10-input-64-tests-v7"
.\Tools\uwp\build\build-arm32-cx-component.ps1 `
  -OutDir "$env:TEMP\opencode\m10-input-64-production-v1"
.\Tools\uwp\build\test-cx-component-contract.ps1 `
  -OutDir "$env:TEMP\opencode\m10-input-64-contract-v1"
.\Tools\uwp\build\check-wkuwp-legacy-surface.ps1 `
  -OutDir "$env:TEMP\opencode\m10-input-64-legacy-v1"
. "Tools\uwp\arm32-uwp-env.ps1" -VcpkgRoot C:\vcpkg
$env:CCACHE_DISABLE = '1'
cmake --build "$env:TEMP\opencode\m10-rendering-61-mini" --target MiniBrowserUWP --parallel 8
```

- Native input laws: **54/54** — tap at release coordinates, overscroll-offset
  taps, drag classification/slop, single engine delivery per frame, fling arm/
  coast/stop, stale-release refusal, cancel abort, wheel mapping and edge
  clamping without overscroll, edge stretch/settle stages, invalid rejection,
  two-control isolation, worker rejection, navigation reset, unbound/validation/
  capability/slot/mapping coverage for V1 input and focus.
- Both ARM compilers at `/W4 /WX` without `/ZW` (native no-exception ABI).
- Authored ARM DLL/WinMD link with unchanged public metadata/source manifest.
- External WinMD/factory consumer **20/20** plus four negative modes.
- Frozen legacy signatures/exports and remaining diagnostic sources pass.
- Fresh native MiniBrowser/prototype targets link with eight jobs.
- Documentation audit zero candidates; whitespace checks pass.

Two test bugs found during authoring were fixed on the test side: scroll
direction (finger-up scrolls down) and tap coordinates (release point, per the
inherited behavior). One implementation bug was caught the same way:
`armRelease` computed but never stored the armed state.

## Phone interaction (v42)

Package `WebKitProbe.EngineBrowser_1.0.0.42_arm__2932e6rj4m41p`, App ID
`WebKitProbe.EngineBrowser_2932e6rj4m41p!App`. Install Code=0/Success=true.
The first launch exposed a real pre-existing race: portal launch raises both
`OnLaunched` and `OnActivated`, and both threads ran first-time creation, so
the loser tripped the process owner gate (`control-initialize: RPC_E_WRONG_THREAD`).
v42 serializes first-time creation with an `SRWLOCK` double-checked guard; the
loser then activates the created window. Launch HTTP 200, `control-initialize: 0`.

Owner tapped the demo button and swiped the long document on `about:demo`:

```text
control-pointer-pressed / ptr-press-x / ptr-press-y ...
control-pointer-released / ptr-down=1 / ptr-dragging=0 + engine tap stages, scroll-y unchanged
control-pointer-released / ptr-dragging=1 + control-inertia-start, control-scroll-y advancing
```

Taps dispatch press/release pairs with no document motion; drags scroll and
fling through the extracted classes with identical stages. Counter increments
and scrolling repaint were owner-confirmed.

SHA256:

| Payload/report | SHA256 |
|---|---|
| Signed v42 APPX | `5E1F8029B872E120679B03E7C2042C85E541704D8867A2AF988584C7A9B4223F` |
| Interaction log, 232141 bytes | `E16758BC5ABF6EBAFFDF772D25041E7D2474290A6E5CE777D1C6E95571795C19` |
| Native 54-check report | `B7B509F06BFEEFBEF40F22E811F9C85E8BE2D8D0F87544BAD64AE687B7FE3F5A` |
| Production WebKit.dll | `B84C3588CA07FA29ECEC77F760B1F3A9035AFE1BD27E3F94FD6E4E62493276DA` |
| Production WebKit.winmd | `5A78902FA6D98214E30055FCDEEB8681A37635F4CEF16D84F15F876CE0EF061F` |
| MiniBrowserUWP.exe (ARM) | `417456D2F2A813DF0164D09DDD75BB151D21DEAF251B30EB13312EFF9E3750C4` |
| Engine WebCore.dll (unchanged) | `9213C1BBE442AD02493796C945FCB3805B2A8082E6D4A12018021E423C18B5CF` |

## Scope notes

- Keyboard/focus verified as explicit `E_NOTIMPL` against the real engine, with
  full validation/threading/slot coverage; real key delivery needs engine slots
  plus public-control handlers and stays a later gate.
- The header/ABI prose disagrees on input kind ordinals and modifier bits; code
  follows the frozen headers (the dual-compiler-checked authority).
- One deliberate micro-fix versus the legacy wrapper: capture-cancel also clears
  the dragging flag (a press resets it either way, so no observable path changes).
- Full G4 remains open; 6.3 overlay/lost-device items are untouched by this slice.
