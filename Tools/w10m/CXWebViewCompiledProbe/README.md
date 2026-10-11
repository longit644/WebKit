# Compiled-XAML public WebKit contract host

This ARM32 CX app references only compiler-generated public `WebKit.winmd`.
It compiles `using:WebKit` controls, a URI Source attribute, and read-only
title/history/loading bindings into XBF and an SDK type provider. The app
consumes the matching production DLL and no private framework .cpp/test exports.

```powershell
.\Tools\w10m\build\build-cx-webview-compiled-probe.ps1 `
  -OutDir <fresh-output-directory> -Version 1.0.0.1 -SignThumbprint <thumbprint>
```

PowerShell 7 is required. `-FrameworkRef` overrides installed UWP references;
`-FontDirectory` overrides the preserved DejaVu package fixture directory. Both
XAML passes use the same component WinMD as the DLL. The driver compiles the
generated provider, links ARMNT/AppContainer images, stages XBF-only resources,
audits package PRI/member identity and signs `WebKitCompiledProbe-arm.appx`.

Package: `WebKitProbe.WebKitCompiledHost_<version>_arm__2932e6rj4m41p`.
App ID: `WebKitProbe.WebKitCompiledHost_2932e6rj4m41p!App`.
After confirmed install/launch use one visible 30-second poll to retrieve
`LocalState/webkit-compiled-result.txt`. Require exact version, PASS, expected
assertion count and zero failures. Construction/check errors preserve their
HRESULT; missing reports never pass.

Version 1.0.0.1 passed **13/13** on Lumia 950 XL / Mobile 15254.603: generated
class/base/new URI member, readonly metadata, actual Source markup setter,
bindings, inherited interfaces, layout/reinsert, public defaults, Close/error
results and weak release of sixteen temporary controls. This is declarative
contract acceptance, not completed production networking/renderer integration.
See the M10 `artifact/public-usage-and-compiled-xaml.md` and API-root `USAGE.md`.
