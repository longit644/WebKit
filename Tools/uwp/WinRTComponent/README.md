# UWP WinRT component sources

`WebKitWebView.idl` is the source of truth for the public WinRT contract and its
documentation. Edit and review the IDL, then regenerate its WinMD and C/C++
projection outputs with the installed Windows SDK MIDLRT compiler. Run from the
repository root in a Visual Studio developer PowerShell where `cl.exe` is on
`PATH`:

```powershell
$sdk = 'C:\Program Files (x86)\Windows Kits\10'
$sdkVersion = '10.0.22621.0'
$env:Path = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64;' + $env:Path
& "$sdk\bin\$sdkVersion\x64\midlrt.exe" `
  /winmd Tools/uwp/WinRTComponent/WebKitWebView.winmd `
  /metadata_dir "$sdk\UnionMetadata\$sdkVersion" `
  /I "$sdk\Include\$sdkVersion\winrt" `
  /I "$sdk\Include\$sdkVersion\um" `
  /I "$sdk\Include\$sdkVersion\shared" `
  /h Tools/uwp/WinRTComponent/WebKitWebView.h `
  /iid Tools/uwp/WinRTComponent/WebKitWebView_i.c `
  Tools/uwp/WinRTComponent/WebKitWebView.idl
```

This repository uses the classic MIDLRT dialect. It emits additional proxy
artifacts (`WebKitWebView_p.h`, `WebKitWebView_p.c`, and `dlldata.c`) in the
compiler's working directory. Keep required generated outputs with the component
build; do not hand-edit generated C/C++ files, since regeneration overwrites them.

`WebKitWebViewComponent.cpp` contains the WRL implementation. Its local event
argument and async-action classes are component implementation details; their
public contracts are documented in the IDL. `WebKitWinRTBase.h` supplies the
minimal OS-stable async interfaces needed by this toolchain and documents each
method and status value.

## XAML identity probe

`XamlProbe.idl` is a browser-free declarative `UserControl` feasibility probe.
The implementation uses C++/CX because the SDK C++/WinRT base header rejects
32-bit ARM. Select the ARM32 MSVC tools and the Windows 10 SDK 10.0.19041.0;
the newer Windows 11 SDK installed on this machine has no `um\arm` libraries.
Keep generated files under the build directory. Run from the repository root:

```powershell
$vs = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools'
$vc = "$vs\VC\Tools\MSVC\14.44.35207"
$sdk = 'C:\Program Files (x86)\Windows Kits\10'
$sdkVersion = '10.0.19041.0'
$probeBuild = 'build-arm-winrtcomponent\probe-component'
$env:Path = "$vc\bin\Hostx64\arm;$vc\bin\Hostx64\x64;" + $env:Path
New-Item -ItemType Directory -Force $probeBuild | Out-Null
& "$sdk\bin\$sdkVersion\x64\midlrt.exe" `
  /winmd "$probeBuild\XamlProbe.winmd" `
  /metadata_dir "$sdk\UnionMetadata\$sdkVersion" `
  /I "$sdk\Include\$sdkVersion\winrt" `
  /I "$sdk\Include\$sdkVersion\um" `
  /I "$sdk\Include\$sdkVersion\shared" `
  /h "$probeBuild\XamlProbe.h" `
  /iid "$probeBuild\XamlProbe_i.c" `
  Tools/uwp/WinRTComponent/XamlProbe.idl
& "$vc\bin\Hostx64\arm\cl.exe" /nologo /c /ZW /EHsc /std:c++17 `
  /DWIN32 /D_UNICODE /DUNICODE `
  /I "$sdk\Include\$sdkVersion\winrt" `
  /I "$sdk\Include\$sdkVersion\um" `
  /I "$sdk\Include\$sdkVersion\shared" `
  /I "$sdk\Include\$sdkVersion\ucrt" `
  /I "$vc\include" `
  /AI"$vc\lib\x86\store\references" `
  /AI"$sdk\UnionMetadata\$sdkVersion" `
  /Fo"$probeBuild\XamlProbeComponent.obj" `
  Tools/uwp/WinRTComponent/XamlProbeComponent.cpp
```

The C++/CX compiler supplies the declarative `UserControl` projection through
`/ZW`; the WRL factory only provides the DLL activation shell. Link the object
with the ARM libraries from the same SDK, including `runtimeobject.lib`,
`ole32.lib`, `uuid.lib`, `vccorlib.lib`, and the repository's
`Tools/uwp/lib/arm-crtstart.lib`. The probe remains outside normal CMake until
that standalone DLL is linked and run inside a real XAML tree.
