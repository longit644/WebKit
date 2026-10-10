# M10 5.2: copied options and pre-page validation

## Ownership and implementation

`WebKitWebViewOptions` is an application-owned mutable bag. The control's constructor copies all six values into `native/WebViewConfigurationUWP.h/.cpp`, whose stored input is const. Public strings are copied with their explicit length, so embedded NULs cannot silently truncate a path. The public options object is not retained. All native storage stays in the component.

`PrepareConfiguration` runs before the initialization placeholder can reach a page. Default/relative paths resolve against `Package::Current->InstalledLocation->Path`: `cacert.pem` and `Fonts` are the defaults. Windows lexical canonicalization covers dot segments, separators and case. An invalid relative package root is rejected rather than falling back to the working directory. Filesystem permissions still govern custom absolute paths.

Validation checks the exact concurrency/resource/buffer/deadline ranges first, then opens and reads CA bytes, decodes each PEM certificate to an X509 context, checks required readable regular/bold DejaVu faces, and computes SHA256 over CA bytes and sorted font names/contents. Windows certificate APIs only parse the file; they never augment its trust with a native certificate store. Font parsing and process-global adoption remain the engine initialization step.

`ValidatedWebViewOptionsUWP` owns const CA bytes, trust fingerprint, canonical paths, copied limits and font fingerprint. Its V1 page/engine views borrow those buffers with the exact frozen headers; queue=128 and redirects=10 are fixed. Actual CA bytes survive subsequent filesystem mutation, and a failed validation leaves an existing output snapshot untouched. A successfully prepared control reuses its holder on retry. Native validation compiles without `/ZW`; the public projection remains in its own CX TUs.

Invalid ranges/path shapes/null options report `E_INVALIDARG`; missing assets retain their originating Windows error; empty/malformed/noncertificate/NUL CA data reports `HRESULT_FROM_WIN32(ERROR_INVALID_DATA)`. Allocation failure maps to `E_OUTOFMEMORY`. No validation helper creates a page or invokes engine startup.

## Verification

Succeeded on 2026-10-08, PowerShell 7.6.6, MSVC 14.44.35207, SDK 10.0.19041.0:

```powershell
.\Tools\uwp\build\test-webview-configuration.ps1 `
  -OutDir "$env:TEMP\opencode\m10-webview-52-v4"
.\Tools\uwp\build\test-cx-component-contract.ps1 `
  -OutDir "$env:TEMP\opencode\m10-cx-component-52-host-v3"
```

The configuration driver accepts `-FontDirectory`. This run copied the preserved `build-arm-minibrowser/package-skia/Fonts` into a temporary fixture; mutation and restoration touched only the temporary CA file. Independent PowerShell/.NET oracles checked CA SHA256 and sorted-font SHA256.

- ARM32 `/W4 /WX` native/CX compile, full Store DLL/WinMD link, ARMNT/AppContainer/export and exact metadata checks: pass.
- **42 configuration checks, zero failures**: all copied values, null/default paths, immutable bytes/fingerprints, V1 views, canonical paths, every range's low/high/extreme rejections, inclusive endpoints, missing assets, malformed CA data, embedded NULs, URL rejection, untouched output and retry after an intentionally changed fixture file.
- **20 factory/consumer checks, zero failures**: 5.1 regression plus length-aware class-name matching. WinMD-only compilation exercises every public operation, overload, event subscription and static SourceProperty name. Four forbidden constructor/setter modes still fail compilation for the intended reasons.

Latest ARM output: `$env:TEMP/opencode/m10-webview-52-v4/arm/`.

- `WebKit.dll` SHA256: `8F877F1C25EB4AEB355700E880C430A5D45643671F83590EAA118B37751D186B`
- `WebKit.winmd` SHA256: `C40065B10905DA6438F3C40ADADC93B14DA68EFCD43E56A2EF2725BF5320B6AB`
- `configuration-results.txt` and `contract-results.txt` record each run's assertions in the respective output roots.

This is host/build validation evidence. Task 5.4 will deliver distinct async actions/error observations and native page readiness; the negotiated production page slots remain Unsupported until controller/engine forwarding is wired. No new control phone/package or TLS enforcement run is claimed here.
