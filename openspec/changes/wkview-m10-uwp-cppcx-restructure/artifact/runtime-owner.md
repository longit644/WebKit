# M10 7.5: runtime owner with tickets, quiescence and shutdown

## Ownership and shutdown contract

`platform/RuntimeOwnerUWP.h/.cpp` is the process runtime owner over the bound or unbound engine module, the clipboard route and the dispatcher port. Each accepted clipboard operation opens a ticket; its single completion (normal or Closed-on-cancel) acknowledges and closes the ticket. Hooks open tickets that close on revocation. Quiescence means no open tickets, no pending route operations and no registered hooks.

`beginShutdown` latches, then cancels every ticket (each acknowledged Closed), revokes every hook, loses the dispatcher and forwards engine shutdown when bound; host steps run before the forward, so acknowledgment never depends on the engine answer. After the latch, new clipboard work, hooks, recreations and engine table requests fail fail-closed while engine cancels still pass through as cleanup. Dispatcher loss fails wakes with `DispatcherUnavailable` while synchronous cancellation keeps working, so the last release with a lost dispatcher drops nothing silently. Destruction shuts down first, so pending work is acknowledged while fully alive; completions and hook callbacks must not destroy the owner. Page creation stays with the control (section 8 integration); the owner binds, pins and shuts down the module.

The owner builds the engine-facing platform table with its own context: request/cancel forward to the current service, retain/release count table references the engine owes past quiescence, and `postWake` enqueues one wake through the port, fulfilling the 7.1 notes. The engine module pins its image for process lifetime; destroying the owner never unloads it.

## Host checks and commands

On 2026-10-10, PowerShell 7.6.6, MSVC 14.44.35207, SDK 10.0.19041.0, native ARM clang-cl 19.1.5:

```powershell
.\Tools\uwp\build\test-wkuwp-runtime-owner.ps1
```

- ARM32 compile of the owner, service, route and module with the native engine compiler, toolchain-exact flags at `/W4 /WX`: pass with no warnings.
- ARM32 compile of all four with the MSVC ARM32 compiler at `/W4 /WX` without `/ZW`: pass with no warnings, extending the neutrality proof to the owner and the module consumer.
- Owner header neutrality audit plus no engine header reaching for it: pass.
- ARM32 module link of the four objects: `COFF-ARM`, `IMAGE_FILE_MACHINE_ARMNT`, `APPCONTAINER`.
- Host behaviour from x64 copies against the test-double provider, reached only through the loader: **137/137**, covering bring-up baselines, ticketed copy/cut/paste with acknowledged completion, engine table with working postWake, hook registration and revocation, close/recreate generations with carried totals, shutdown with acknowledged tickets and revoked hooks, fail-closed post-shutdown behavior, dispatcher-loss drops with synchronous close release and last-release destruction, foreign-thread rejection of all eight entry points, missing-engine rejection, bind/pin idempotence, ticket acknowledgment despite an engine shutdown failure with the failure preserved and recorded, shutdown polling, post-shutdown bind refusal and pin survival past the owner (refcount-neutral handle observation after the owner dies).
- Task 7.2 routing regression (with its 7.1 service regression): **89/89** and **83/83**. Only additive owner files are new; no shared source changed.

## Device checks and commands

The diagnostic component compiles the owner stack with a new `-OwnerTests` flavor exporting `WKUWPTestOwner`; the probe app runs it on the XAML owner thread writing `owner-result.txt`. Engine pinning stays a host proof: binding the real engine inside the probe would start ungraded engine work. Async reads pump the dispatcher in a bounded drain; the run saves and restores the device clipboard text with only version-tagged strings asserted.

```powershell
.\Tools\uwp\build\build-cx-source-state-probe.ps1 `
  -OutDir "$env:TEMP\opencode\m10-owner-phone-v45" -Version 1.0.0.45 `
  -InitializationTests -EventCallbackTests -CommandTests -LifecycleTests -TransactionTests `
  -SchedulerTests -ClipboardTests -OwnerTests `
  -SchedulerBaseUrl http://192.168.1.69:8002 `
  -EngineDirectory C:/Users/Longi/Workspace/WebKit/build-arm-uwp-fresh/bin `
  -DependencyBaseline C:/Users/Longi/Workspace/WebKit/build-arm-minibrowser/package-skia `
  -SignThumbprint 18FD65CF5E103CF96F04C2A451DC49C94788A40D
```

Lumia 950 XL, `mc-001173b46c8b`, `MobileCoreProd`, OS `15254.603.armfre.feature2_rs3svc.200106-1633`, portal `https://192.168.1.52`. The first install attempt timed out with the phone off-network; after the owner reconnected it, installation completed Code=0/Success=true and launch returned HTTP 200. Fixture server `http://192.168.1.69:8002` served the scheduler oracle. One visible 30-second poll was run before fetching. All nine reports match version **1.0.0.45**:

```text
owner-result.txt:          result=PASS checks=37 failures=0
clipboard-result.txt:      result=PASS checks=27 failures=0
scheduler-result.txt:      result=PASS checks=22 failures=0
transaction-result.txt:    result=PASS checks=35 failures=0
lifecycle-result.txt:      result=PASS checks=24 failures=0
command-result.txt:        result=PASS checks=27 failures=0
event-callback-result.txt: result=PASS checks=37 failures=0
initialization-result.txt: result=PASS checks=33 failures=0
source-state-result.txt:   result=PASS checks=42 failures=0
```

Total **284/284 phone assertions**. The 37 owner assertions cover ticketed copy/paste/cut round trips, worker rejection, close/recreate generations with carried totals, hook registration and revocation, shutdown with acknowledged tickets and revoked hooks, fail-closed post-shutdown behavior, engine-table ShuttingDown refusal and clipboard save/restore.

SHA256:

| Payload/report | SHA256 |
|---|---|
| Signed v45 APPX | `6FA431CDE983295F8C09D69AC1CD9B3C78E2E92C1FB79CD0D6DE1D0825748CAF` |
| Diagnostic WebKit.dll | `43CE938C219CF8C8AB8F97FE93F10A3101F867CBF25D72F91AE7EE5111C9BC42` |
| Diagnostic WebKit.winmd | `3C2063D5C05FECC2A861B1C0453A8621FC15FB6E33D7F80FBF2CF32957AB673B` |
| Actual WebCore.dll | `95B7AF88486FBBADD380FF38E735DDB10541883BDB1F974206DFE7AB55A783DE` |
| Owner report, 1648 bytes | `7A28C721DCE53C5F05CA51D5BD5F0489AE56E261D275DE4307DD600F1D3AB03E` |
| Clipboard report | `C076E458C5E4FAC7AFB9E93E0DCA9BBD2131413CFEC55E52B29D5A3003EE3B4B` |
| Scheduler report | `928284814E1857F95465B49E36D84522675FCA9FE79E74EE290F42318CA7685C` |
| Transaction report | `5A65288C4D511B2F34CAF0AD1665A17C7A2527D59E9F0D113BECD4E32BD17533` |
| Lifecycle report | `2E838B1378896160A980F71E957AA13C0F5878E04A2AB38A2980AF58AD303630` |
| Commands report | `E11889E171CF4049F49048F95DE38BFEA469326BA15CAC7123F73854D2F0B754` |
| Callbacks report | `7127ADF39AA7647E689390CA8764A0C5BCA06138538563209690E4E0AA4FD212` |
| Initialization report | `B85FA022BC1FF39279FC59F73B3E4F8B6E33D7AE2DE4D01018B04BCE9F4DDEC` |
| Source/state report | `C4805E1A3C88DBB393BAF10255BAEC3B1231A5A520B39D01018B04BCE9F4DDEC` |
| Native 137-check report | `9AE7E5EF9C5E72D49C8FC04F4AD784F0FCDE6569F412501BC87CFA1BCB9313E4` |

Full reports remain under `$env:TEMP/opencode/v45-*.txt`; the package root remains under `$env:TEMP/opencode/m10-owner-phone-v45`. The staged engine differs from v44 because the 7.4 ledger edit rebuilt it after v44 staging.

## Defects found during the run

The host suite first expected one shutdown revocation while two hooks were open; the owner correctly revoked both, and the suite now revokes the second hook explicitly for deterministic single-hook shutdown accounting. The diagnostic component link first missed `EngineModuleUWP.cpp`, which the owner flavor now compiles. A C++14 `data()` and a brace-eating edit were caught before the passing runs. The v45 phone run also exposed a stale OutDir guard discipline (rebuilds require fresh directories) with no source impact.

## Remaining gates

This accepts the 7.5 runtime owner on host and phone. Production control adoption belongs to section 8. **G4 is not complete**: task 6.3 resize/recovery stays open and is now the only open G4 item.
