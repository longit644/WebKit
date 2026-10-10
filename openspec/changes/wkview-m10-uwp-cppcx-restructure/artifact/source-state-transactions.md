# M10 5.3: Source DP and observable-state transactions

## Implementation

`Source/WebKit/UIProcess/API/uwp/WebKitWebView.h/.cpp` now routes both the public setter and generic XAML/DP assignments through one validated command path. Generic assignments restore the currently published URI under a nesting-safe guard before validation/enqueue. Invalid schemes report `E_INVALIDARG` with no native command, property/DP drift or recursive navigation. Unchanged nonnull Source is not Reload; null is still processed when the CLR wrapper is called with an already-null value.

`native/NavigationStateUWP.h/.cpp` keeps initialization and main-navigation phases separate, copies requested/committed/restoration targets, and consumes actual page IDs, navigation IDs, document generations and callback sequences. It never allocates a navigation ID or infers a terminal outcome from diagnostic text. During initialization only the latest target survives; null clears it without canceling the shared attempt. Failure/cancellation before commit restores the last committed URI, including same-document changes. Committed-loading null retains the document; terminal-document null stops remaining work without a new main terminal or SourceChanged event.

The projection prepares a candidate state and immutable WinRT payloads before installing the full observable snapshot. `Source`, `CanGoBack`, `CanGoForward`, `IsLoading` and `DocumentTitle` are coherent before any property/event invocation. `ICustomPropertyProvider` supplies real runtime Binding accessors; only Source is writable. Getters and actual DP values stay on the current transaction while reentrant setters enqueue FIFO. Even B,C,B intent is preserved when the last setter equals the frozen DP value. Commands are revalidated after dispatch, and no replacement starts between A's loading=false notification and owed Completed(A).

Starting interruption waits for its structured terminal before draining queued work. Terminal obligations are marked before PropertyChanged, so reentrant Close cannot suppress an already-owed completion. Handler failures are contained; generation/page/sequence checks reject stale and cross-control signals. Deferred native notifications own their borrowed URI/title bytes before returning. `WebKitSourceChangedEventArgs.IsNewDocument` derives from document generation, independently of HistoryTraversal classification.

## Acceptance scope

The device tests embed the **real authored UserControl** and real DP/INPC/runtime bindings. A diagnostic-only per-control command driver supplies deterministic frozen V1 structured events. It is compiled from `Tools/uwp/CXStateTests/` only with `-SourceStateTests`; the shipping component exports no test function and its WinMD contains no controller/test methods. The command port is internal, instance-bound and borrowed through call/dispatch unwind.

These results accept the Source/observable-state layer. The production negotiated page slots still report Unsupported. Distinct initialization actions/joiner resolution and actual engine page binding are task 5.4/5.5 integration; history, HTML/retry commands, full lifecycle cleanup and the complete notification matrix retain their tasks 5.6/5.7/5.9 gates. G3 is not complete from this row alone.

## Build and host verification

On 2026-10-08, PowerShell 7.6.6, MSVC 14.44.35207, SDK 10.0.19041.0:

```powershell
.\Tools\uwp\build\test-navigation-state.ps1 `
  -ComponentDirectory "$env:TEMP\opencode\m10-source-53-arm-v2" `
  -OutDir "$env:TEMP\opencode\m10-source-native-v1"
.\Tools\uwp\build\test-cx-component-contract.ps1 `
  -OutDir "$env:TEMP\opencode\m10-source-53-contract-v1"
.\Tools\uwp\build\build-cx-source-state-probe.ps1 `
  -OutDir "$env:TEMP\opencode\m10-source-state-phone-v3" -Version 1.0.0.3 `
  -SignThumbprint 18FD65CF5E103CF96F04C2A451DC49C94788A40D
.\Tools\uwp\build\build-arm32-cx-component.ps1 `
  -OutDir "$env:TEMP\opencode\m10-source-53-production-v4"
```

- Native value-state contract: **30 checks, zero failures**.
- Generated-WinMD consumer/factory regression: **20 checks, zero failures**; four forbidden constructor/setter modes still fail for their intended reason.
- ARM32 `/W4 /WX` native/CX compile and full DLL/WinMD link: pass.
- ARMNT/AppContainer/export and exact metadata checks: pass, including absent production diagnostic exports and hidden controller entries.
- Final production rebuild after documentation-only edits: pass.

Latest production SHA256:

- `WebKit.dll`: `AADCBB14F56C555FF4360E6F8BEB34952BBFC96AD3D5926443FB17A5089FFE06`
- `WebKit.winmd`: `C02243EBBC16E5D03CDD22893BE9E2B219F5A8CA744D0B56B08C062B24DEC360`

## Lumia evidence

Phone: Lumia 950 XL, `MobileCoreProd`, OS `15254.603.armfre.feature2_rs3svc.200106-1633`, portal `https://192.168.1.52`.

Package: `WebKitProbe.SourceStateHost_1.0.0.3_arm__2932e6rj4m41p`. App ID: `WebKitProbe.SourceStateHost_2932e6rj4m41p!App`. Installation returned 202, completion state Code=0/Success=true, launch 200. The owner reported the visible **SOURCE / STATE PASS** screen. After a portal reconnection the version-tagged file was fetched with HTTP 200:

```text
version=1.0.0.3
result=PASS
checks=42
failures=0
```

All null-state rows, pending/effective/restored URIs, invalid setter/direct-DP rollback, same-URI no-op, four same-document kinds, copied strings after borrowed buffer scrambling, stale/foreign-page rejection, Starting-null cancellation, reentrant frozen getters/DP, FIFO execution/order, Close during property dispatch, interleaved controls, handler-error containment, state-before-typed-events and loaded title/history XAML binding assertions passed. Full assertions are retained in `$env:TEMP/opencode/m10-source-state-phone-v3-result.txt` (2597 bytes).

Device payload/report SHA256:

- Signed APPX: `736140A2274B86C315DB6CA36D599AE5D18B282F10FDD9FE18FD9EC27DC0BB63`
- Diagnostic `WebKit.dll`: `B317E42F46457DB154EFAA6FADF8AB0E8DFACD6A53D78328EB7756AA15D1DA7A`
- Diagnostic `WebKit.winmd`: `4CA358BCBDA7AACF6386F0BB39310B1B89DEF9ADE6C0E17AC14EF3CB1A985116`
- Result file: `520FB83F720DB9E1B6BDFDB1202A6DBD3C89C4578954AD8F1DE5F3CE1111F538`

## Source documentation

All new section-5 C/C++ functions/methods, properties/events, constants, enums, types and owned fields have declaration/definition documentation. The source audit now recognizes CX declarations/qualified accessors/constructors/constants, and excludes historical generated headers by **directory**, so the authored API-root WebKitWebView.h is audited. Targeted `--check` reports **zero candidates**. `python -m unittest Tools/uwp/test_audit_source_docs.py -v` passes four regressions covering CX coverage, constructors, local-initializer exclusions and authored-header inclusion. Python/PowerShell callable documentation is included as well.
