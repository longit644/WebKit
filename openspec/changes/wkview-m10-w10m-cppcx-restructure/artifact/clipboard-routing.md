# M10 7.2: clipboard routing through retained owner-thread tokens

## Ownership and routing contract

`platform/ClipboardRouteUWP.h/.cpp` is the owner-side clipboard router over the caller-owned neutral 7.1 service. Each copy/cut/paste issues one service operation with a retained token as its completion context. The token lives from acceptance until the single completion returns, on the owner thread only. The token is erased before delivery, so a completion may close the route or issue new operations, but must not destroy the route: a close-driven completion runs inside close()'s cancellation loop. Operation IDs are route-monotonic and never reused. Cut publishes like copy with the completion marked cut; selection deletion belongs to the engine. Engine-driven operations through the same borrowed table keep their own contexts and are unaffected by route Close. Capability mask remains **0**; table wiring stays 7.5.

Close cancels every pending route operation; each completes Closed exactly once, the latch is set before cancellation so reentry returns at once, and later copy/cut/paste fail Closed without completing. Destruction closes first. A transport completion arriving after Close finds no service record and is ignored. With a real dispatcher, the route must be drained to zero pending before destruction, because a queued dispatcher task cannot be recalled here; dispatcher-loss/quiescence cleanup belongs to 7.5. Transports must never invoke a held completion after their owner is gone, which is the transport contract.

## Host checks and commands

On 2026-10-10, PowerShell 7.6.6, MSVC 14.44.35207, SDK 10.0.19041.0, native ARM clang-cl 19.1.5:

```powershell
.\Tools\w10m\build\test-wkw10m-clipboard-route.ps1
```

- ARM32 compile of the route and service with the native engine compiler, toolchain-exact flags at `/W4 /WX`: pass with no warnings. No `/EHsc` is appended, per the task 7.1 LLVM 19.1.5 backend finding these units share.
- ARM32 compile of both with the MSVC ARM32 compiler at `/W4 /WX` without `/ZW`: pass with no warnings, extending the neutrality proof to the router.
- Route header neutrality audit plus no engine header reaching for it: pass.
- ARM32 module link of both objects: `COFF-ARM`, `IMAGE_FILE_MACHINE_ARMNT`, `APPCONTAINER`.
- Host behaviour from x64 copies: **89/89**, covering copy/cut/paste exactness with kind/cut reporting, failed-write origin preservation, validation before any token exists, orphan-service rejection, two-pending Close with exactly-once Closed delivery, idempotent relatch, post-close Closed rejection, late-transport suppression, destruction-during-close delivery, reentrant close, destruction inside an ordinary completion, foreign-thread rejection of all four entry points, and borrowed-table coexistence with an engine-issued write.
- Task 7.1 service gate rerun unchanged: **83/83**. No shared source is modified by this task, so the service proof is a regression by construction.

## Device checks and commands

The diagnostic component compiles the routing layer with its consumer under a new `-ClipboardTests` flavor (`DispatcherUWP.cpp`/`ClipboardPlatformUWP.cpp` with `/ZW`, service plus route without), exports `WKUWPTestClipboard`, and the probe app runs it on the XAML owner thread writing `clipboard-result.txt`. The 7.5 runtime owner promotes these sources to the unconditional production lists. Async WinRT reads complete through `RunAsync` tasks, so the suite pumps the owner dispatcher with `ProcessEvents` in a bounded drain instead of blocking. The run saves the existing clipboard text first and restores it at the end; only the version-tagged round-trip strings are asserted.

```powershell
.\Tools\w10m\build\build-cx-source-state-probe.ps1 `
  -OutDir "$env:TEMP\opencode\m10-clipboard-phone-v44" -Version 1.0.0.44 `
  -InitializationTests -EventCallbackTests -CommandTests -LifecycleTests -TransactionTests `
  -SchedulerTests -ClipboardTests `
  -SchedulerBaseUrl http://192.168.1.69:8002 `
  -EngineDirectory C:/Users/Longi/Workspace/WebKit/build-arm-uwp-fresh/bin `
  -DependencyBaseline C:/Users/Longi/Workspace/WebKit/build-arm-minibrowser/package-skia `
  -SignThumbprint 18FD65CF5E103CF96F04C2A451DC49C94788A40D
```

Lumia 950 XL, `mc-001173b46c8b`, `MobileCoreProd`, OS `15254.603.armfre.feature2_rs3svc.200106-1633`, portal `https://192.168.1.52`. Fixture server `http://192.168.1.69:8002` served the scheduler oracle during the run. Package `WebKitProbe.SourceStateHost_1.0.0.44_arm__2932e6rj4m41p`, App ID `WebKitProbe.SourceStateHost_2932e6rj4m41p!App`. Installation completed Code=0/Success=true; launch returned HTTP 200. One visible 30-second poll was run before fetching. All eight reports match version **1.0.0.44**:

```text
clipboard-result.txt:      result=PASS checks=27 failures=0
scheduler-result.txt:      result=PASS checks=22 failures=0
transaction-result.txt:    result=PASS checks=35 failures=0
lifecycle-result.txt:      result=PASS checks=24 failures=0
command-result.txt:        result=PASS checks=27 failures=0
event-callback-result.txt: result=PASS checks=37 failures=0
initialization-result.txt: result=PASS checks=33 failures=0
source-state-result.txt:   result=PASS checks=42 failures=0
```

Total **247/247 phone assertions**. The 27 clipboard assertions cover real copy/paste and cut/paste round trips with exact bytes, worker-thread copy/paste/close rejection, a close-while-in-flight race completing exactly once as OK or Closed, post-close Closed rejection, engine-table interop and clipboard save/restore without fault.

SHA256:

| Payload/report | SHA256 |
|---|---|
| Signed v44 APPX | `AD315DD881DC0D1A4B1CE441E2C4B2CB336A50585E0F102184B16C034D88BC34` |
| Diagnostic WebKit.dll | `2D8DBA6DEF9025BEFF9220D8823B65076ED415DA5EC3B020FC235EBFE95E24F1` |
| Diagnostic WebKit.winmd | `BAAF9D245144874C520811E19888FAB02A915AB2E05040C8779C4565D4D14B62` |
| Actual WebCore.dll | `9213C1BBE442AD02493796C945FCB3805B2A8082E6D4A12018021E423C18B5CF` |
| Clipboard report, 1176 bytes | `6682AFE2954E330799395E55A38A387244EE79CB3C357E190823EA36C95557B8` |
| Scheduler report, 5025 bytes | `DF1AB47CB2458D0AFD2FDF93DEFD46DDD133AB3438B1CA5BBBAB8DE84F64E674` |
| Transaction report | `15F559217A63B517B092E6B6AAE688D596C2397C0EC0207EAAFBB6AC014207F2` |
| Lifecycle report | `BDD639A25F7C9CC547DF8EE659D9476B117140AE0663787C81CC575F75915D8D` |
| Commands report | `B18716B398E9CCA88521286AE9267FDE6A3B9F25CF7DDF6C30A6D45EAAE3F6C8` |
| Callbacks report | `7D3024E911BDF596BCBB18C2A30A4334B07E45A13BAB87FF02251FB611393A63` |
| Initialization report | `31447C1E9C293B666F580E39EB167F1AC3AB5BC7BF8DFA39C8E374AD7AE2DE4D` |
| Source/state report | `B76DD2F845BA1519012C4A9F35B7D38679DA5A3FBD156B74FCD1DFD8CE10E332` |
| Native 89-check report | `ADFA78EC972184368F42DBFD2537DEF2F207A4BBB111FB64E2C90A4F350978FE` |
| Native 83-check report | `322CE6E12B2C236257F06E0D195DBB61EE8952768E76B89C38D74D06B36F4902` |

Full reports remain under `$env:TEMP/opencode/v44-*.txt`; the package root remains under `$env:TEMP/opencode/m10-clipboard-phone-v44`.

## Defects found during the run

The device suite first passed the route completion (six parameters) where the borrowed engine table requires the engine completion (four parameters); the ARM compiler rejected it and the suite now records through a dedicated engine-side completion. An unused version-token local tripped `/WX` and was removed. Both were caught by the diagnostic component build before packaging.

## Remaining gates

This accepts routed copy/cut/paste, cross-thread rejection and delayed completion after Close on the host and the phone. Runtime-owner wiring, `postWake`, cleanup-ticket accounting, dispatcher-loss/quiescence and shutdown hooks retain the 7.5 gate. Full G4 stays pending on 6.3 and 7.3-7.5.
