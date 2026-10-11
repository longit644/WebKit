# M10 5.8: public usage, error contract and compiled-XAML metadata

## Public documentation and contract gates

`Source/WebKit/UIProcess/API/w10m/USAGE.md` documents declarative embedding, programmatic copied options/package defaults, independent SDK initialization observations, Source/null/restore semantics, engine history/reload/Stop, typed events/synchronous cancellation, reentrant transactions, opaque warning/retry flow, exact HRESULTs, ordinary Close and deferred capabilities. It identifies Unsupported production page forwarding rather than claiming a completed browser.

`Tools/w10m/build/check-public-control-contract.py` audits the **actual compiler source manifest** emitted by `build-arm32-cx-component.ps1`. It requires authored public/event/token/callback/controller inputs, rejects historical wrapper/prototype compiler sources and generated prototype header authority, and verifies the frozen WKUWPPageEvent/table/copied-field route without legacy log types, fixture content or diagnostic-string event inference. The build invokes this gate after writing its Sources manifest. Four tests accept the structured baseline and detect legacy logging, prototype input and framework fixture contamination.

The existing WinMD-only consumer verifies every public operation/overload/event/ static member, exact signatures/enums, read-only state, nonconstructible tokens/ arguments and distinct interface identity. No historical MIDL prototype metadata is input to the new component.

## Compiled consumer

`Tools/w10m/CXWebViewCompiledProbe/` is separate from the preserved G1 XamlProbe. Its app imports only **WebKit.winmd**, declares WebKitWebView controls using `using:WebKit`, and loads generated XBF/type-provider code with real public getters/setters. The same production DLL/WinMD pair is packaged; no private implementation .cpp or diagnostic component export is consumed by the app.

One element declares null Source; another declares an absolute URI. The actual URI setter result is captured immediately after InitializeComponent, before the intentionally Unsupported lazy page forward runs; that control is then closed. The page checks compiled read-only bindings, inherited interfaces, measured layout, remove/reinsert, option projection, Close/errors and weak release of sixteen temporary public controls. Matching SDK XAML passes/local app metadata, generated provider compilation, XBF-only staging and package-matched PRI are audited.

## Verification

On 2026-10-08, PowerShell 7.6.6, MSVC 14.44.35207, SDK 10.0.19041.0, UWP framework references 6.2.14 / ref/uap10.0.15138:

```powershell
.\Tools\w10m\build\build-cx-webview-compiled-probe.ps1 `
  -OutDir "$env:TEMP\opencode\m10-webkit-compiled-v1" -Version 1.0.0.1 `
  -SignThumbprint 18FD65CF5E103CF96F04C2A451DC49C94788A40D
python -m unittest Tools/w10m/build/test_public_control_contract.py -v
.\Tools\w10m\build\test-cx-component-contract.ps1 `
  -OutDir "$env:TEMP\opencode\m10-public-58-contract-v1"
.\Tools\w10m\build\build-arm32-cx-component.ps1 `
  -OutDir "$env:TEMP\opencode\m10-public-58-production-v1"
```

- Both XAML passes, generated provider/app sources, full ARM DLL/EXE/WinMD links, generated member checks, XBF/PRI identity and signing: pass.
- ARMNT/AppContainer/import inspection and actual source-manifest contract gate: pass.
- Source-boundary regression tests: **4/4**.
- Host factory/WinMD-only consumer: **20/20**, plus four intended forbidden construction/read-only setter compile rejections.
- Documentation audit: **zero candidates**; authored methods/functions, constants/enums/types/fields have comments. Generated output remains outside source and is not manually edited.

## Lumia acceptance

Lumia 950 XL, `MobileCoreProd`, OS `15254.603.armfre.feature2_rs3svc.200106-1633`, portal `https://192.168.1.52`. Package `WebKitProbe.WebKitCompiledHost_1.0.0.1_arm__2932e6rj4m41p`; App ID `WebKitProbe.WebKitCompiledHost_2932e6rj4m41p!App`. Install Code=0/Success=true, launch HTTP 200. After one 30-second poll and portal reconnection the LocalState result was fetched HTTP 200:

```text
version=1.0.0.1
result=PASS
checks=13
failures=0
```

All compiled type/base, new writable URI Source, readonly member metadata, actual URI markup setter, initial state, title/history/loading bindings, inherited interfaces, layout/reinsert, defaults, Close/RO_E_CLOSED and 16-control weak-release assertions passed. Full result: `$env:TEMP/opencode/m10-webkit-compiled-v1-result.txt`, 972 bytes.

SHA256 identities:

- Signed APPX: `827019D08A62F5FCC145DE71FF1E0638F99C25EE755A451448EDE8601E516258`
- Result: `9981F41A811919C94E43B79B5F75A918F82DCAA6E3903AF997B86942964F2C07`
- Packaged DLL: `3A504E926A6AD8EECC170185AC5D0FD88F893C4B009DD68A073E157059BF5CCD`
- Packaged WinMD: `947D333B3C577C2AF2B1A26165C1F867F885EE99F5DB9B541C4E5D942B9F3EB4`
- App EXE: `2F67FBD2D3D9E4EBA9CDA74DD624FB1967F9DB4CC34013FC6BFC53CDDCA282C2`
- ContractPage XBF: `26D0A135723996D4808BA9A59BA97832A98E896476FD8101A8C45B6918C76EE2`
- PRI: `03EAD4071D18C6BBDD6B0FC954AE50966D0743F3AD69DCE9EFD84F1C8AC2A099`
- Fresh gated production DLL: `8764E4DA976223B7FFFA587648104019DAEA578322D715CECDC200083831DE36`
- Fresh gated production WinMD: `E8B8D293A21A55C3024608D7F4A451BE3EB1731AB936B05ED2BA63FEBF3CB661`

Compiled-XAML public contract acceptance is complete for this row. Production WebCore forwarding/renderer/services, broader notification traces and combined G3/G4/G6 retain their separate gates; this is not universal website evidence.
