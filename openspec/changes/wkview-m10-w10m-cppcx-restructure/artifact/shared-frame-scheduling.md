# M10 6.2: shared owner service and per-view frame scheduling

## Ownership and service contract

`rendering/FrameSchedulerUWP.h/.cpp` separates live view leases, visibility and presentation epochs from `OwnerThreadServicePumpUWP`'s shared copied negotiated table. Two participants in one SDK RenderingTime epoch invoke provider service once and each eligible frame once. Duplicate/stale epochs cannot multiply work; a failed shared epoch keeps its original result for every view. Recursive service requests are deferred rather than nesting generic RunLoop cycles. Close releases only that view's lease, leaving peers serviceable; recreate resets frame epochs.

`platform/FrameSchedulerDriverUWP` is the private CX projection boundary: one weak-target CompositionTarget token, one 50-ms DispatcherTimer, copied native client dispatch snapshots and coalesced worker RunAsync wakes. Timer/wake service continues while every participant's presentation is disabled. Ordinary last-client cleanup stops the timer and revokes both tokens. Bound service-capable public controllers join this service automatically and retain their lease through Pending Close until native acknowledgement. No SDK callback roots a control target.

Explicit process suspension is distinct from visual unload. The native pump forwards setSuspended once, preserves a rejected originating result and pauses service/presentation only after success. Resume restarts service with fresh epochs. Visual unload changes no service lease or deadline-suspension state.

## Real engine delivery

Configured WebCore builds define `WKUWP_OWNER_SERVICE_FORWARD` for the provider TU. Its `service` and `setSuspended` slots call native EngineUWP forwards after successful actual initialization. Pre-init service returns NotInitialized; foreign-thread service returns WrongThread. Owner initialization/thread state is atomically published. Service rejects recursion and explicit suspension. Loader suspend/resume updates the actual process resource queue/deadline span once.

`WKTickPageUWP` now updates only the selected page's rendering pipeline; it never cycles the process RunLoop. The retained native wrapper consumes the negotiated service slot through FrameSchedulerUWP and the exact SDK RenderingTime epoch. Native page peers reuse the one installed dispatcher wake callback.

WTF's generic RunLoop wakes when a timer is scheduled but does not independently post a new owner wake at its future deadline. The separate CX owner timer is therefore necessary for JavaScript/resource timers while presentation is paused. Transport worker wakes remain independently deliverable on the owner thread.

Capability mask remains **0** because page/session/renderer forwards are not yet delivered through the full table. Default public initialization still fails with E_NOTIMPL. Standalone negotiation builds deliberately retain dependency-free Unsupported service stubs; the configured real engine and phone oracle prove actual service forwarding without inventing capability bits.

## Checks and commands

On 2026-10-10, PowerShell 7.6.6, MSVC 14.44.35207, SDK 10.0.19041.0, native ARM clang-cl 19.1.5:

```powershell
.\Tools\w10m\build\test-frame-scheduler.ps1 `
  -ComponentDirectory "$env:TEMP\opencode\m10-scheduler-62-production-v1" `
  -OutDir "$env:TEMP\opencode\m10-scheduler-62-native-v1"
. "Tools\w10m\arm32-w10m-env.ps1" -VcpkgRoot C:\vcpkg
$env:CCACHE_DISABLE = '1'
cmake --build build-arm-uwp-fresh --target WebCore --parallel 8
.\Tools\w10m\build\build-arm32-cx-component.ps1 `
  -OutDir "$env:TEMP\opencode\m10-scheduler-62-production-v2"
.\Tools\w10m\build\test-cx-component-contract.ps1 `
  -OutDir "$env:TEMP\opencode\m10-scheduler-62-contract-v1"
.\Tools\w10m\build\test-wkw10m-engine-negotiation.ps1 `
  -OutDir "$env:TEMP\opencode\m10-scheduler-62-negotiation-v2"
python -m unittest discover -s Tools/w10m/NavigationTests -p test_server.py
```

- Native scheduling laws: **34/34**, plus both ARM compilers at `/W4 /WX` without `/ZW`; visibility, epochs, error preservation, reentry, worker rejection, suspension, Close-in-service, peer continuation and lease baseline are asserted.
- Full configured ARM WebCore target linked with eight jobs. Earlier compatibility header invalidation required shared dependency rebuilding; timeout attempts were not accepted. The resumed runner records `native-engine-build-exit=0` in `$env:TEMP/opencode/m10-scheduler-62-engine-build.log`.
- New ARM CX/native component full link, exact public metadata/source manifest, ARMNT/AppContainer and all seven diagnostic-export exclusions: pass.
- External WinMD/factory consumer: **20/20**, four forbidden construction/setter compilation modes rejected. Standalone negotiation gate passes both ARM compilers, image/export checks and host behavior with unchanged capability mask.
- Native MiniBrowser/prototype affected targets link with eight jobs.
- Fixture HTTP tests: **9/9**, including MIME/closure for the new DOM oracle.
- Source-contract/documentation-audit unit tests: **4/4** each; targeted new authored declarations: **zero documentation candidates**; whitespace checks pass.

The older negotiation driver incorrectly prequoted native linker arguments under modern PowerShell. Its helper now preserves modern argument boundaries and retains legacy quoting with the linker option colon outside the quoted path.

## Meaningful paused-resource oracle

`Tools/w10m/CXStateTests/SchedulerTests.cpp` uses actual SDK owner timers/dispatcher, actual authored XAML controls as weak frame targets, and **two actual native engine pages**, created through preserved diagnostic legacy entry points. Process service comes from the real frozen negotiated table. The watchdog fails the run if any expected DOM result, callback or report write is missing.

`/scheduler.html?page=A/B` loads delayed external CSS and JavaScript. CSS establishes a computed 101-pixel height; the script waits 350 ms, performs a real JSON fetch, and only then produces distinct DOM heights **3001/3002** if computed CSS and JSON both match. No static PASS text/server response can satisfy those observed native layout values. The oracle queries layout/state only: it never paints, ticks or cycles the process run loop itself. Both frame callbacks remain zero during load.

After those results, one controlled SDK epoch services once and invokes both weak control targets once; repeating it invokes neither again. Close destroys page A and its lease. Unloaded B then executes another real JavaScript timer to produce **4002** pixels. Actual worker calls enqueue two coalesced wakes while foreign presentation mutation/native engine service reject. Last ordinary cleanup revokes owner SDK hooks and returns native leases to zero.

## Lumia evidence

Lumia 950 XL, `mc-001173b46c8b`, `MobileCoreProd`, OS `15254.603.armfre.feature2_rs3svc.200106-1633`, portal `https://192.168.1.52`. Fixture server `http://192.168.1.69:8002`; complete real engine/dependency closure is staged, and the probe manifest declares internetClient.

```powershell
.\Tools\w10m\build\build-cx-source-state-probe.ps1 `
  -OutDir "$env:TEMP\opencode\m10-scheduler-phone-v14" -Version 1.0.0.14 `
  -InitializationTests -EventCallbackTests -CommandTests -LifecycleTests -TransactionTests -SchedulerTests `
  -SchedulerBaseUrl http://192.168.1.69:8002 `
  -EngineDirectory <repository>/build-arm-uwp-fresh/bin `
  -DependencyBaseline <repository>/build-arm-minibrowser/package-skia `
  -SignThumbprint 18FD65CF5E103CF96F04C2A451DC49C94788A40D
```

Package `WebKitProbe.SourceStateHost_1.0.0.14_arm__2932e6rj4m41p`, App ID `WebKitProbe.SourceStateHost_2932e6rj4m41p!App`. Installation completed Code=0/Success=true before launch HTTP 200. One visible 30-second poll was attempted; after phone reconnection the saved reports were fetched without restarting the run. All seven reports match version **1.0.0.14**:

```text
scheduler-result.txt:      result=PASS checks=22 failures=0
transaction-result.txt:    result=PASS checks=35 failures=0
lifecycle-result.txt:      result=PASS checks=24 failures=0
command-result.txt:        result=PASS checks=27 failures=0
event-callback-result.txt: result=PASS checks=37 failures=0
initialization-result.txt: result=PASS checks=33 failures=0
source-state-result.txt:   result=PASS checks=42 failures=0
```

Total **220/220 phone assertions**. V12/V13 correctly failed the paused-resource watchdog: diagnostics showed a running service timer and curl connection error 7. The probe lacked internetClient; v14 fixes the manifest and passes the unchanged DOM oracle. Those failed versions remain failures, not accepted device evidence.

SHA256:

| Payload/report | SHA256 |
|---|---|
| Signed v14 APPX | `D141C118741EEEEC5D9ECFC7DB4004DAF13995BE0075AC85449C4AC092663023` |
| Diagnostic WebKit.dll | `DD6DA53BC92AEA72CBA1A573BBFB546824500C0EE6CF72BF6A57514C9D05C485` |
| Diagnostic WebKit.winmd | `E67FE2727C4DE60211BF313BC4CA477C844D17C13A52E9F4DBEA23381EA9F335` |
| Actual WebCore.dll | `F8B1985E52B48D5B13DDCABB233B073C30A7F208B7C5DFC55E339A60CD537EB2` |
| Scheduler report, 5025 bytes | `417AD633F1E787C4C77F4A03177D40A20C32976187807B3750DFAAC3D794E707` |
| Transaction report | `482CE74DA765C12BD6E1A4507F9789E269D54004D8D4583C1561FB0F8B3DF50B` |
| Lifecycle report | `AA7817EDBAFF4BF5570ED8DF0A57C256EC9020747AD9366587CD70F6D01567BE` |
| Commands report | `2E7DAEAC36925A756FF3094D7F31828555D95D9C103124D357016E99CDDC738C` |
| Callbacks report | `55ECA6A3E89F5694132EA6A481D1B1DB81523B2318541926F57773EE9A28E804` |
| Initialization report | `C38041B7FD743D1551AABE31704AB186452303DE1714C8886AB2E964678066D4` |
| Source/state report | `7A7DE0340CFADBD02B6259D32B4729341DE866EB90B7E963DE10815C9D49A83A` |
| Native 34-check report | `83FFC9568505AD0E6E60A212842E3AA51C3B214E450881FC16E4545F5B5895B9` |
| Production WebKit.dll | `2FC73FCEFA0167AFB4C2C1B6439E2FCAA4AE6B5E59A030E27E9181D98464CED8` |
| Production WebKit.winmd | `8DA32273BF8AA05CA9DFB86E6B3ED73387DE6055DCFFABE813994A0765F5750B` |

Full reports remain under `$env:TEMP/opencode/m10-scheduler-v14-*.txt`; build roots, matching compiler identities/import closure and native engine logs remain there.

## Remaining gates

This accepts ordinary owner scheduling and actual resource/timer progress with presentation disabled. Complete new-control page/renderer forwarding, SDK Loaded/Unloaded/app-suspension subscription integration and failed-enqueue/ dispatcher-loss/quiescence cleanup retain their later gates. Diagnostic pages are actual WebCore, but use preserved native creation/navigation entry points; this does not turn unsupported public initialization into readiness. **Horizontal rendering remains incorrect in 6.3**, and full G4 remains open.
