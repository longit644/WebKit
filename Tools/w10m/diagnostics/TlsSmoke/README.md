# TlsSmoke diagnostic

The C# W10M application host (using the UWP model) and native transport payload
are built separately:

- `TlsSmoke.csproj` builds the ARM Release host through .NET Native and stages
  the payload DLLs as AppX content. `Properties/Default.rd.xml` is also AppX
  content so the .NET Native compiler loads its runtime directives.
- `CMakeLists.txt` builds `TlsProbe.cpp` as `WebKitTlsProbe.dll`; it deliberately
  does not refer to the former `TlsSmoke.cpp` application-host translation unit.
- `TlsHostProof.cpp` is host-only test glue and is never packaged.

Run `build-arm32-tls-smoke.ps1` with a new output directory to build and package
the ARM32 diagnostic. Run `verify-tls-smoke-host.ps1` with a different new
output directory to compare probe semantics with the committed pre-migration
source and exercise the fail-closed path on the build host. Both scripts keep
their generated files under `-OutDir`.

## Device runbook

Build and sign each changed payload with a fresh package version. The development
certificate must be installed on the phone and the package signature must verify.
Install through Device Portal, require `Code=0` and `Success=true`, launch the
installed identity, wait for the probe to finish, then fetch its LocalState report.

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File Tools/w10m/diagnostics/TlsSmoke/build-arm32-tls-smoke.ps1 `
  -OutDir "$env:TEMP/opencode/m10-tlssmoke-device" `
  -SignThumbprint 18FD65CF5E103CF96F04C2A451DC49C94788A40D
python Tools/w10m/device-portal.py --url https://<portal> --insecure install `
  "$env:TEMP/opencode/m10-tlssmoke-device/appx/TlsSmoke_1.0.0.9_ARM_Test/TlsSmoke_1.0.0.9_ARM.appx"
python Tools/w10m/device-portal.py --url https://<portal> --insecure get /api/app/packagemanager/state
python Tools/w10m/device-portal.py --url https://<portal> --insecure launch `
  'WebKitWebView.TlsSmoke_2932e6rj4m41p!App' `
  'WebKitWebView.TlsSmoke_1.0.0.9_arm__2932e6rj4m41p'
python Tools/w10m/device-portal.py --url https://<portal> --insecure get `
  '/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=WebKitWebView.TlsSmoke_1.0.0.9_arm__2932e6rj4m41p&path=%5CLocalState&filename=tls-smoke.txt'
```

Require the report to retain the ordered `tls-startup`, `tls-package-path`,
`tls-ca-present`, transport/OpenSSL phase markers, verified/insecure outcome
fields, and final `tls-done`. The verified phase must fail closed when the
bundled CA is absent/invalid; the diagnostic insecure comparison is separate
and never enables warning/consent behavior in the application. The host proof
in `verify-tls-smoke-host.ps1` is not device evidence; section 11 owns the
phone run and report capture.
