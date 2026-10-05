# Compiled ARM32 CX/XAML gate probe

This browser-free application embeds `WebKitProbe.XamlProbe` directly in
`ProbePage.xaml`. The SDK generates the app metadata provider and XBF; the
package contains no source XAML and no manually attached native visual.

## Build

From the repository root, choose a fresh output directory:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\uwp\build\build-arm32-cx-compiled-probe.ps1 -OutDir "$env:TEMP\opencode\compiled-cx-run" -SignThumbprint <certificate-thumbprint>
```

Use `-SkipSign` for build-only verification. Installation requires a signed
package. Overrides: `-VsPath`, `-MsvcVersion`, `-SdkRoot`, `-SdkVersion`, and
`-FrameworkRef` (the UWP reference directory containing `mscorlib.dll`). SDK
19041 ships the required ARM32 libraries. Tool discovery checks actual inputs;
no developer-home path or certificate is embedded in the driver.

The driver compiles the component and uses MSVC `/WINMD` to generate
`XamlProbeCX.winmd` from the same object used for `XamlProbeComponent.dll`. It
does not stage the independently generated prototype MIDL WinMD. After pass 1,
local app declarations produce the intermediate app WinMD. Pass 2 receives
authored/generated headers in `ClIncludeFiles`; omitting them caused WMC9999 in
the earlier isolated reproduction. Generated consumer code force-imports the
component WinMD with `/FU`.

Store CRT libraries must precede repository native startup helpers in the app
link. The reversed order selected the native helper startup in v5 and failed
phone launch; v6 uses the working CX-aware startup without LNK4210 warnings.
`arm-crtstart.lib` and `clang_rt.builtins-arm.lib` are repository-owned native
fallback inputs, not substitutes for CX reference counting or exception handling.

Staging validates ARMNT/AppContainer flags, activation exports, CX property/IID
metadata, the PRI primary map and compiled page path. Both packaged native images
import only named OS API contracts, Kernel32/Ole32 and ARM VCLibs framework DLLs;
unexpected DLL imports fail staging. System/framework DLLs are not copied from
the build machine. Phone execution is the proof of their runtime resolution.
`imports.txt`, `resources-dump.xml`, `inspect-component/` and `build-identity.json`
record the audit, installed input paths and SHA256 payload hashes.

## Device check

Package: `WebKitProbe.XamlCompiledHost`, app ID `App`. The manifest registers
`WebKitProbe.XamlProbe` to `XamlProbeComponent.dll`. The primary PRI map matches
the package identity, while compiled page paths use `CXCompileProbe/`.

Use `Tools/uwp/device-portal.py` to install, poll deployment, and launch with the
actual package full name/version. Retrieve `LocalState/compiled-xaml-result.txt`.
Only a report carrying the newly deployed version is current evidence; an old
PASS file survives package updates and must not be counted after failed launch.

V6's report is:

```text
version=1.0.0.6
result=PASS
identity=1
property=1
binding=1
inherited=1
layout=1
reinsert=1
destroyed=32/32
```

The one-shot Loaded callback verifies actual outer-control interfaces and layout,
initial property assignment and binding, content removal/restoration, and final
release of 32 newly activated, unattached controls through expired weak references.
It does not claim broader application suspend/resume or browser engine lifetime.
Evidence: `openspec/changes/wkview-m10-uwp-cppcx-restructure/artifact/compiled-xaml-lumia-v6.txt`.
