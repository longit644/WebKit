# M10 5.9: notification transactions and revalidated FIFO commands

## Transaction and rejection rules

`WebKitWebView` installs the copied candidate state and creates the terminal payload before PropertyChanged. Property/typed-event invocation keeps getters and the real Source DP on that snapshot. Native signals own copied spans when deferred; commands execute only after owed event delivery, native-call unwind and any outstanding refused-policy terminal acknowledgement.

Direct synchronous admission rejection preserves its original HRESULT and restores the prior requested Source if no new Starting ID was admitted. Deferred command rejection increments private diagnostics and continues the FIFO, including stale warning/retry capabilities. It never invents a main ID or terminal event. A command waiting for an asynchronous policy terminal is deferred even when submitted outside the original callback. Close preserves already-owed terminals before revocation; Starting Cancel/replacement/Stop/Close takes precedence over a simultaneous throw.

Readiness scheduling is deferred work. Initial admission rejection restores the committed Source, leaves all successfully resolved initialization observations complete, and keeps the ready page usable. An independent document-intent epoch invalidates obsolete unstarted HTML continuations when Source/HTML/Stop/null supersedes them. Latest HTML removes an unstarted initial Source without a synthetic terminal. Internal Initialize/InitialSource markers cannot veto a real proposal.

Private counters/helpers do not appear in public WinMD. Public class/interface, enum values and the frozen V1 declarations are unchanged.

## Exact reference-trace oracle

`Tools/w10m/CXStateTests/TransactionTests.cpp` creates 20 independent real authored controls and binds each through `native/PageControllerW10M` to a deterministic V1 provider with retained actual callbacks. It evaluates **35 assertions**, including **13 complete filtered actual/reference vector comparisons**. A mismatch, unexpected exception or report-write failure fails the suite; later passing cases cannot hide it.

Notation: `N#` = provider admission; `P1/P0` = IsLoading PropertyChanged; `S#` = Starting; `C#/X#/F#` = Completed/Cancelled/Failed terminal; `Z` = native cleanup. IDs are local to each independent page.

| Case | Required complete trace |
|---|---|
| Terminal PropertyChanged navigates B | `P0 C1 N2 P1 S2` |
| Terminal PropertyChanged closes | `P0 C1 Z` |
| Starting replacement plus throw | `N1 P1 S1 P0 X1 N2 P1 S2` |
| Starting explicit Cancel plus throw | `N1 P1 S1 P0 X1` |
| Starting unopposed throw | `N1 P1 S1 P0 F1` |
| Completed navigates B | `P0 C1 N2 P1 S2` |
| Completed closes | `P0 C1 Z` |
| Stale generation during terminal PropertyChanged | `P0 C1 N2 P1 S2` |
| Rejected deferred admission followed by valid B | `P0 C1 N2 P1 S2` |
| Stop invalidates queued warning before valid B | `N1 P1 S1 P0 F1 N2 P1 S2` |
| Async veto acknowledgement, rejected then valid command | `N1 P1 S1 P0 X1 N2 P1 S2` |
| Starting Close plus throw | `N1 P1 S1 P0 X1 Z` |
| Starting Stop | `N1 P1 S1 P0 X1` |

Additional state assertions check frozen getters, terminal counts per ID, exact Platform HRESULT, no rejected HTML/retry provider call, direct Source rollback, Stop retaining initialization joiners, latest HTML copy, Source superseding pending HTML, HTML superseding pending Source, and recovery after initial admission failure.

## Build and host checks

On 2026-10-09, PowerShell 7.6.6, MSVC 14.44.35207, SDK 10.0.19041.0:

```powershell
.\Tools\w10m\build\build-cx-source-state-probe.ps1 `
  -OutDir "$env:TEMP\opencode\m10-transactions-phone-v10" -Version 1.0.0.10 `
  -InitializationTests -EventCallbackTests -CommandTests -LifecycleTests -TransactionTests `
  -SignThumbprint 18FD65CF5E103CF96F04C2A451DC49C94788A40D
.\Tools\w10m\build\test-cx-component-contract.ps1 `
  -OutDir "$env:TEMP\opencode\m10-transactions-contract-v1"
.\Tools\w10m\build\test-navigation-state.ps1 `
  -ComponentDirectory "$env:TEMP\opencode\m10-transactions-contract-v1" `
  -OutDir "$env:TEMP\opencode\m10-transactions-state-v1"
.\Tools\w10m\build\build-arm32-cx-component.ps1 `
  -OutDir "$env:TEMP\opencode\m10-public-59-production-v1"
python -m unittest discover -s Tools/w10m/build -p test_public_control_contract.py
python -m unittest discover -s Tools/w10m -p test_audit_source_docs.py
```

- ARM32 native/CX `/W4 /WX`, full same-object DLL/WinMD link, signed APPX: pass.
- Production ARMNT/AppContainer, exact compiler-derived metadata, actual compiler input/source contract and all six diagnostic-export exclusions: pass.
- Host factory/WinMD-only consumer: **20/20**, four forbidden construction/setter modes rejected for the expected compiler errors.
- Native Source-state regression: **30/30**.
- Source-contract checker regressions: **4/4**; documentation-audit regressions: **4/4**; targeted authored C/C++ documentation audit: **zero candidates**.
- Tracked and untracked source whitespace checks: pass.

New methods/functions, constants/types/enums and owned fields are documented. Build manifests retain the actual source set and matching payload hashes. The v9 intermediate package was compile-only; accepted runtime evidence uses v10 below.

## Lumia evidence

Lumia 950 XL, `MobileCoreProd`, computer `mc-001173b46c8b`, OS `15254.603.armfre.feature2_rs3svc.200106-1633`, portal `https://192.168.1.52`. Package `WebKitProbe.SourceStateHost_1.0.0.10_arm__2932e6rj4m41p`; App ID `WebKitProbe.SourceStateHost_2932e6rj4m41p!App`.

After owner reconnection, install returned HTTP 202, then package-manager state confirmed Code=0/Success=true. Launch returned HTTP 200. One visible 30-second poll retrieved the transaction result, followed by all five regression reports. Every file matches version **1.0.0.10**:

```text
transaction-result.txt:    result=PASS checks=35 failures=0
lifecycle-result.txt:      result=PASS checks=24 failures=0
command-result.txt:        result=PASS checks=27 failures=0
event-callback-result.txt: result=PASS checks=37 failures=0
initialization-result.txt: result=PASS checks=33 failures=0
source-state-result.txt:   result=PASS checks=42 failures=0
```

Total: **198/198 device assertions**, with all 13 transaction actual vectors equal to their reference vectors. No screenshot or new visual-rendering acceptance is claimed from these text reports.

SHA256 identities:

| Payload/report | SHA256 |
|---|---|
| Signed APPX | `5ED6031A8EA61A9216152895DB18E418A40570DE5E3060AA687286011EE25F7B` |
| Diagnostic WebKit.dll | `EF2E2B34B77ADB3E4211B253BA4F9DA7F9AC6753710705CC572C7A44D8BD55C1` |
| Diagnostic WebKit.winmd | `44EB88C470B586020DC400C7A4AA64D30731F023D9CE171F05D8C1BACEA979B5` |
| Transaction report, 3636 bytes | `25FE0586EA4AF1DF10845C7A8B06C64E3D97078DD244127F15E33EA3EAEABEA4` |
| Lifecycle report, 1965 bytes | `A9409AB8E97B753C77630404D0AB2CC13A6F5129A9424C55DFF74DAB91175D2B` |
| Command report, 2131 bytes | `D2777534613F914C0FF296809D06E8820CDFBC4D0CF50F6F4971709584D63AC6` |
| Callback report, 3050 bytes | `8D49B402A83CCB3F0BCF5F28F2EFBEEFD80D3831212012A7BBA7CFED5CC1EE5F` |
| Initialization report, 2363 bytes | `34ACFB144D461FAB6206929054F968F50AFBFA56118E3993380977F7C40F59E6` |
| Source-state report, 2598 bytes | `6EEB7D81D2919D97960D8CE6DF62664555FB649775844A6608E75C72FB41CC9C` |
| Production WebKit.dll | `7FB3484F44FA5B020C3325359EB1A13BE7ED7106BBE51C5252D853E9F6DFB8C0` |
| Production WebKit.winmd | `60D20DFF11B8CFF9A3BE712DD4A9EF493919BB9087B8CBEC3F579DEE15CFC2EB` |

Full reports remain at `$env:TEMP/opencode/m10-transactions-v10-result.txt` and `m10-transactions-v10-{lifecycle,commands,events,initialization,state}.txt`. Matching diagnostic and production roots retain compiler metadata/identity manifests.

## Acceptance scope

This accepts the real public control's notification/FIFO/readiness semantics using controlled frozen-table admission and completion responses. Production WebCore negotiated-slot, TLS/origin, renderer and platform-service integration remains separate; the engine still advertises capability mask zero and default initialization reports E_NOTIMPL. Full G3/G4 and final-release/dispatcher-loss cleanup are not inferred from this consumer matrix. Rendering/input/scheduling work starts at 6.1; runtime-owner cleanup remains 7.5.
