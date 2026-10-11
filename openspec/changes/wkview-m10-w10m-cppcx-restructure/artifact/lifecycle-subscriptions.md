# M10 6.5: lifecycle subscriptions (Loaded/Unloaded, suspend/resume)

## Implemented ownership

`rendering/RenderSubscriptionUWP.h/.cpp` owns one control's Rendering subscription across four independent axes: visual attachment (Loaded/Unloaded), presentation clocks (suspend/resume), terminal frame failure, and instance Close. Exactly one token is ever outstanding: subscribe paths first check, and every revocation clears. Temporary unload pauses callbacks while the page, presenter and service lease stay alive; only Close ends the instance, and a frame-failure disable is terminal without ending it. Hook failures preserve exact HRESULTs: a failed subscribe leaves no token for a later retry, while a failed unsubscribe retains the token. Terminal `close()` revokes best-effort and then ignores every later transition. All mutations enforce the constructing owner thread; the report sink from the input slice stays non-throwing here by construction (no sink is installed: the wrapper reports stages itself).

The native wrapper binds its CompositionTarget subscription through a narrow `RenderPortUWP`, subscribes once at initialize (the inherited Loaded behavior), and adds `Loaded`/`Unloaded` handlers on the root control that only pause and resume callbacks — page state is never torn down and no second token is ever created. `setSuspended(bool)` pauses/resumes presentation clocks for OS suspension without closing; after Close it reports `RO_E_CLOSED`. A failed frame keeps its inherited terminal semantics via `disableAfterFailure()`.

The MiniBrowser shell subscribes once each to application `Suspending` and `Resuming` and forwards to the wrapper with `minibrowser-suspending/resumed` markers. Suspension requests no deferral: it only pauses callbacks, so nothing can be left half-completed across the freeze.

## Checks and commands

On 2026-10-11, PowerShell 7.6.6, MSVC 14.44.35207, SDK 10.0.19041.0, native ARM clang-cl 19.1.5:

```powershell
.\Tools\w10m\build\test-rendering-subscriptions.ps1 `
  -ComponentDirectory "$env:TEMP\opencode\m10-scheduler-62-production-v2" `
  -OutDir "$env:TEMP\opencode\m10-subscriptions-65-tests-v3"
. "Tools\w10m\arm32-w10m-env.ps1" -VcpkgRoot C:\vcpkg
$env:CCACHE_DISABLE = '1'
cmake --build "$env:TEMP\opencode\m10-rendering-61-mini" --target WebKitWebView WebKitMiniBrowser --parallel 8
```

- Native subscription laws: **35/35** — single-token attach, load/unload invariance, three reparent cycles balanced 5/4, suspend/resume idempotence and unload interplay, close/failure terminality and post-terminal inertness, failure preservation and retry, two-instance isolation, worker rejection.
- Both ARM compilers at `/W4 /WX` without `/ZW`.
- Fresh native MiniBrowser/prototype targets link with eight jobs.
- Documentation audit zero candidates; whitespace checks pass.

## Lumia evidence (v43)

Package `WebKitProbe.EngineBrowser_1.0.0.43_arm__2932e6rj4m41p`, App ID `WebKitProbe.EngineBrowser_2932e6rj4m41p!App`. Install Code=0/Success=true, launch HTTP 200. Owner locked/unlocked the phone with the power button (real OS suspend/resume) while `about:demo` was visible:

```text
control-suspended: 0x00000000
minibrowser-suspending: 0x00000000
control-resumed: 0x00000000
minibrowser-resumed: 0x00000000
```

Three suspend/resume cycles, each paired wrapper/shell success, with `gpu-frame-profile` and `frame-profile` batches continuing after every resume. `control-loaded: 0` fired once at startup; no duplicate subscription was ever observed. Prior v42 input interaction (tap/scroll/fling stages) still passes through the same build lineage.

SHA256:

| Payload/report | SHA256 |
|---|---|
| Signed v43 APPX | `7AA7284C4EC9511855091ECB9379C40B5AD690A2DB80EC422C10EE31ED549E94` |
| Suspend log, 228448 bytes | `01BFE2D4DA1E9B2713E694DFFB00DC36B4AC42678B54B8415AAEABC091160645` |
| Native 35-check report | `20A37835FF8BA0DCCCD6ADF06B4F560C8135CF6A4BF66157900EA1ADC5CD2FF0` |
| Historical native-shell MiniBrowserUWP.exe (ARM; pre-C# rename) | `2B573FBC7D94D9DCB58B79A51B11251D49EE30912B6FB454B6ABB550FD65E8E2` |

## Remaining gates

Reparenting through actual XAML unload/load has no shell affordance, so the unload half is proven by the host state machine plus the thin two-lambda wiring, not by a device reparent run. CX-control visuals, dispatcher-loss and global quiescence stay with their runtime-owner gates. Full G4 remains open.
