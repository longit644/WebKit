# M10 5.4: independent async observations over shared initialization

## Implementation and ownership

`Source/WebKit/UIProcess/API/uwp/platform/InitializationActionUWP.h/.cpp` implements
the actual SDK `IAsyncAction` projection in C++/CX, including inherited
`IAsyncInfo`. Each call returns a new object, a nonzero process-unique async ID,
its own single completion-handler slot, status, original HRESULT, Cancel state
and Close state. No historical WRL async/prototype header is used.

Caller cancellation terminates only that action as Canceled/E_ABORT. Caller
IAsyncInfo.Close suppresses that observation's delivery and makes later reads/
GetResults report RO_E_CLOSED; repeated Close is harmless. Neither operation
cancels the shared attempt. Weak owner-dispatched detachment removes ended
observations from producer ownership. A mutex protects action state, delegates
are retained with the SDK Agile wrapper, and callbacks run outside locks on the
owner dispatcher with a retained action lifetime. Handler errors do not change
an already-recorded action result or cause duplicate delivery.

`WebKitWebView` now gives Source-driven and explicit callers the same
generation-bound initialization attempt. Configuration is validated/frozen before
controller/page work; failed validation is returned through a failed action.
Controller work is issued once per generation. Readiness/failure carries that
generation so late results cannot settle a later retry. Canceled/closed/failed
observations never become successful on a later attempt.

On readiness, the control installs the ready state and marks every live joiner
terminal before property or completion handlers run. Native/notification drain
guards keep reentrant setters/Close from starting navigation in that interval.
Only after all live completion invocations does it enqueue consumption of the
latest initial Source. Success therefore means page/controller readiness, not
first navigation completion. Post-ready calls return fresh completed actions.
Failure settles live callers with the same original HRESULT; retry reuses the
private immutable options and retained validated CA holder. Control Close marks
all pending caller actions canceled before its first completion callback and
invalidates the shared attempt; late readiness is ignored.

## Verified scope

The device host uses the real XAML control and real SDK async action objects.
Its diagnostic-only per-control command port supplies deterministic page
readiness and frozen V1 navigation signals. Package CA/font defaults are actually
validated, including invalid-range/missing-asset failure cases. The ordinary
production page slots remain Unsupported; their fallback produces an explicit
failed action (E_NOTIMPL), never false readiness. Production engine forwarding,
full service/final-release cleanup, worker/apartment-loss matrices and the
remaining public command/event integration retain their later gates. G3 is not
accepted in full by this observation-layer evidence.

## Commands and results

On 2026-10-08, PowerShell 7.6.6, MSVC 14.44.35207 / SDK 10.0.19041.0:

```powershell
.\Tools\uwp\build\build-cx-source-state-probe.ps1 `
  -OutDir "$env:TEMP\opencode\m10-initialization-phone-v4" `
  -Version 1.0.0.4 -InitializationTests `
  -SignThumbprint 18FD65CF5E103CF96F04C2A451DC49C94788A40D
.\Tools\uwp\build\test-cx-component-contract.ps1 `
  -OutDir "$env:TEMP\opencode\m10-initialization-contract-v4"
.\Tools\uwp\build\build-arm32-cx-component.ps1 `
  -OutDir "$env:TEMP\opencode\m10-initialization-54-production-v4"
```

- ARM32 native/CX `/W4 /WX` compile, full Store DLL/WinMD link and package sign: pass.
- Exact public metadata, ARMNT/AppContainer and production-private-export checks: pass.
- Factory/WinMD-only consumer regression: **20 checks, zero failures**; four
  forbidden constructor/setter modes still reject the intended operation.
- Targeted documentation audit: **zero candidates**. All new methods/functions,
  fields, constants/enums/types have comments; no new public interface identity
  or frozen native ABI layout was changed.

## Lumia run

Lumia 950 XL, `MobileCoreProd`, OS
`15254.603.armfre.feature2_rs3svc.200106-1633`, portal `https://192.168.1.52`.
Package `WebKitProbe.SourceStateHost_1.0.0.4_arm__2932e6rj4m41p`;
App ID `WebKitProbe.SourceStateHost_2932e6rj4m41p!App`.
Deployment was accepted, completion state Code=0/Success=true, launch HTTP 200.
One 30-second result poll fetched both version-matched files with HTTP 200:

```text
initialization-result.txt: version=1.0.0.4 result=PASS checks=33 failures=0
source-state-result.txt:   version=1.0.0.4 result=PASS checks=42 failures=0
```

The 33 async assertions verify distinct identities/handlers, pending GetResults,
group readiness before either callback, late handler registration, single-handler
assignment, independent/repeated Cancel, pending action Close, all-observers-ended
Source use, latest/cleared Source, readiness before first navigation, control Close
and delayed signals, exact failure HRESULT, immutable retry holder/options, stale
attempt rejection, prior failures remaining failed, pre-page validation errors,
and Source/Close reentrancy from readiness handlers. The 42-check 5.3 suite passes
with the same new component and real package validation.

Payload/report SHA256:

- Signed APPX: `166C6A15CD363C24921EC268E378B7F315B8B75B781D2D6155AB64ECDD5E468B`
- Diagnostic `WebKit.dll`: `2106DC3A8343DBDE91FEF2386BC6289379F0CE6B3AFFD8AD5AD6D192D0E17425`
- Diagnostic `WebKit.winmd`: `8A3FE6885ED0566918EDF2B29DA9D160764907903E6A44D789FD0256D9AB6F88`
- Initialization result (2362 bytes): `E2BCBB85AB6D16D55AE97271644093C6A901514A6B29E89C8418E7DCFF984711`
- State result (2597 bytes): `9BA01B98B85FB250CB2809351C3D8F2C6CE9BCEE57F575FB28FA96D094288644`
- Production `WebKit.dll`: `3B5AE341653E9B53608A000DB6C3111E39933847476AFA86BBB7F8E0F3E7195B`
- Production `WebKit.winmd`: `4FFCBF4EC732F27222761319415061D942F6B4900A481731548C8BEED14890D2`

Full reports remain at `$env:TEMP/opencode/m10-initialization-v4-result.txt` and
`m10-initialization-v4-state-result.txt`; output directories retain matching
compiler metadata and build identity manifests.
