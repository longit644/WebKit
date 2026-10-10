# M10 5.7: idempotent Close, weak subscriptions and ordinary lifetime

## Close and notification ownership

`WebKitWebView.Close` immediately marks closing, rejects new mutations, clears queued commands/transient text, invalidates consent and cancels pending initialization observations. Native cleanup is issued once after active property, policy, completion and controller calls unwind. Already-marked terminal transactions remain owed; Close during Starting creates the proposal's one cancellation, while Close during Completed never creates another outcome.

`platform/EventHandlersUWP.h` gives each public event an explicit owner-thread handler registry with standard EventRegistrationToken add/remove projection. Invocation snapshots the delegate list before calling the app. Removal/Close therefore cannot invalidate active machinery or erase owed delivery; throwing subscriber behavior remains the ordinary stop-this-invocation rule. Final Close revokes all delegate roots, breaking captured-object/control cycles. New event registration is a mutation and reports RO_E_CLOSED after closing; owner-thread removal remains harmless after revocation. Public class/default-interface identity and generated event signatures remain unchanged.

`FinishClose` publishes the final committed/nonloading snapshot, then revokes handlers, clears copied/deferred work, releases setup callback ownership, native controller/context/configuration and retained validated assets once. Read-only Source/title/history/loading snapshots remain owner-thread readable.

## Pending acknowledgement and weak context

`native/PageControllerUWP` records its close request before entering the engine, so reentrant release acknowledgement is safe. Pending close retains page binding; repeated requests do not call the engine again. The actual callback attachment's engine-reference release acknowledges cleanup, and `acknowledgeClose` ends that binding only after a requested close. The control cannot declare final cleanup merely because its main navigation already finished.

`platform/PageCallbacksUWP` separately counts setup/native dispatch and provider attachment references. Its target remains weak. Owner release acknowledgement enters the weak control directly; foreign release posts an owner wake while retaining only native/weak/dispatcher state. No release callback performs foreign-thread XAML teardown or invokes public handlers. Late inactive policy fails closed; stale notifications cannot mutate final state or a surviving peer.

## Scope

The real CX/XAML control is tested with the actual frozen table/native consumer and a deterministic per-page provider, including explicit Pending acknowledgement. Worker tests call real public methods and frozen policy/notify entries from the thread pool: wrong-thread checks precede argument/page mutation. Closed owner-thread mutations report RO_E_CLOSED; all worker mutations report RPC_E_WRONG_THREAD with no native operation or registration drift.

These results accept **ordinary explicit Close** and retained/weak event/context ownership. Runtime-owner cleanup tickets, last-release dispatcher loss, failed enqueue quarantine, global service quiescence and OS thread destruction remain task 7.5. Loaded/Unloaded/presentation subscriptions are section-6 scope. Real WebCore negotiated-slot/renderer integration and full G3 remain separate gates.

## Commands and checks

On 2026-10-08, PowerShell 7.6.6, MSVC 14.44.35207, SDK 10.0.19041.0:

```powershell
.\Tools\uwp\build\build-cx-source-state-probe.ps1 `
  -OutDir "$env:TEMP\opencode\m10-lifecycle-phone-v8" -Version 1.0.0.8 `
  -InitializationTests -EventCallbackTests -CommandTests -LifecycleTests `
  -SignThumbprint 18FD65CF5E103CF96F04C2A451DC49C94788A40D
.\Tools\uwp\build\test-cx-component-contract.ps1 `
  -OutDir "$env:TEMP\opencode\m10-lifecycle-57-contract-v1"
.\Tools\uwp\build\build-arm32-cx-component.ps1 `
  -OutDir "$env:TEMP\opencode\m10-lifecycle-57-production-v8"
```

- ARM32 `/W4 /WX` native/CX compile, full DLL/WinMD link, signed package: pass.
- Production ARMNT/AppContainer, exact metadata and five diagnostic-export exclusions: pass.
- Host factory/WinMD-only regression: **20/20**, plus four intended forbidden constructor/setter compile rejections.
- Source documentation audit: **zero candidates**; new methods/functions, types/constants and owned fields are documented.

MSVC disallows assignment to generated CX events (C3918). Explicit add/remove registries provide revocation without depending on compiler-private event fields; the real Binding/event/regression suites verify the replacement projection.

## Lumia evidence

Lumia 950 XL, `MobileCoreProd`, OS `15254.603.armfre.feature2_rs3svc.200106-1633`, portal `https://192.168.1.52`. Package `WebKitProbe.SourceStateHost_1.0.0.8_arm__2932e6rj4m41p`; App ID `WebKitProbe.SourceStateHost_2932e6rj4m41p!App`. Installation completed Code=0/Success=true; launch HTTP 200. The one 30-second poll fetched lifecycle results; after portal reconnection the four existing regression files were fetched without restarting the app. All match version 1.0.0.8:

```text
lifecycle-result.txt:      result=PASS checks=24 failures=0
command-result.txt:        result=PASS checks=27 failures=0
event-callback-result.txt: result=PASS checks=37 failures=0
initialization-result.txt: result=PASS checks=33 failures=0
source-state-result.txt:   result=PASS checks=42 failures=0
```

The 24 lifecycle assertions cover committed loading cancellation, one native close, final readable snapshots, repeated Close, released assets/attachment, late inactive callbacks, every closed mutation and event add, PropertyChanged/ Starting/Completed reentrant Close traces, explicit Pending wait/acknowledgement, peer isolation, captured-subscriber release, control-capturing cycle break, expired weak native target, and a bounded worker checking ten public mutations plus frozen policy/notify affinity without owner-state/native-call drift.

SHA256 identities:

- Signed APPX: `30FA920607782D6134B0270E5F97F8B03A9763E2C41F53F47260F2F84756C8E6`
- Diagnostic `WebKit.dll`: `4A36F1810E221136770B022C370B28BB39CB081E6427D085D639A964482403A7`
- Diagnostic `WebKit.winmd`: `9B108673AFEE01A830FEB2F322BC3A8D780E71CFF973536987A1886501C7F318`
- Lifecycle report (1964 bytes): `220D27A9F8C9A0B870DDA3D23F6AB527C0DBFEDB093ED41469BEB0A0D11B4C49`
- Command report: `3F6EC12D4CC990746CC603858B4EB6E23EAFAE9F4E6811C3F50CD7C467538EF4`
- Callback report: `48161AE5264BD715BA4DED55A8539CF448BBAE66B3973F9CBC8AB203FF84F2DB`
- Initialization report: `FC08F4EB6E9F347AB0E69A0EE8FEB6CD63E2BB53A3194E19ECBBE2BD2362A2AB`
- Source/state report: `15513FF632E851586B9C1B071A9AB9E8CAA043898B94E759FAC69B0AB935E2AB`
- Production `WebKit.dll`: `1DB6C1B188723FA0587C8E575B4C5731031AE47963E8CEB689372F648D2730DC`
- Production `WebKit.winmd`: `F64076AC40D78C1AD9AB45C6AB3A24AF232C31D5EAEE28AC20A983C7B5443B63`

Full reports remain at `$env:TEMP/opencode/m10-lifecycle-v8-result.txt` and the four `m10-lifecycle-v8-{commands,events,initialization,state}.txt` paths. Build output roots retain matching compiler metadata and identity manifests.
