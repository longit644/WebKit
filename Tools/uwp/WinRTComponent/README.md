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
