# UWP C++/CX component sources

The projection in use is C++/CX, not C++/WinRT, because the SDK's C++/WinRT base
header rejects 32-bit ARM. This directory is named for that projection. It was
`WinRTComponent/` while the prototype was C++/WinRT.

Two things live here and they have different fates. `XamlProbe.idl` and
`XamlProbeComponent.cpp` are the browser-free gate probe, which M10 keeps. The
`WebKitWebView*` files are the prototype control that M10 replaces with authored
sources in `Source/WebKit/UIProcess/API/uwp/`; do not add to them.

## XAML identity probe

`XamlProbe.idl` declares the probe's runtime class, its default interface and its
threading model; `XamlProbeComponent.cpp` implements it. Neither is optional: the
IDL is the metadata source, and the WRL `IActivationFactory` in the .cpp is the
activation entry point.

`/ZW` does **not** generate the activation entry point. A component compiled with
`/ZW` alone has no `DllGetActivationFactory` symbol and no `/EXPORT` directive,
and the linked DLL cannot be activated. The link must therefore pass
`/export:DllGetActivationFactory` explicitly.

Do not feed the IDL-generated WinMD back into the `/ZW` compilation. Compiling
against the generated header redefines the runtime class (`C2011`, `C3094`,
`C2027`). The IDL and the CX class describe one contract from two directions and
are combined by the build, never by handing one to the other.

### Building it

`Tools/uwp/build/build-arm32-cx-probe.ps1` performs the compile, the WinMD
generation and the link, and verifies the result is COFF-ARM,
AppContainer-capable and exports the activation entry point. Prefer it over
hand-written commands:

```powershell
.\Tools\uwp\build\build-arm32-cx-probe.ps1
.\Tools\uwp\build\build-arm32-cx-probe.ps1 -MsvcVersion 14.41.34120
```

It resolves the ARM32 toolset and SDK itself, accepts `-MsvcVersion` and
`-SdkVersion` overrides, and defaults its output to a temporary directory. Use
the installed Windows 10 SDK 10.0.19041.0: the newer Windows 11 SDK on this
machine has no ARM32 `um\arm` or `ucrt\arm` import libraries.

## Prototype control (to be replaced)

`WebKitWebView.idl` is the source of truth for the prototype's public WinRT
contract and its documentation. Edit and review the IDL, then regenerate its
WinMD and C/C++ projection outputs with the installed Windows SDK MIDLRT
compiler. This repository uses the classic MIDLRT dialect, which emits
`WebKitWebView_p.h`, `WebKitWebView_p.c` and `dlldata.c` into the compiler's
working directory. Do not hand-edit generated C/C++ files.

Generate into a build directory, never into the source tree. The generated
`WebKitWebView.h` previously occupied exactly the path M10 assigns to the new
authored header, which had to be cleared before that header could be written.

`WebKitWebViewComponent.cpp` contains the WRL implementation. Its local event
argument and async-action classes are component implementation details; their
public contracts are documented in the IDL. `WebKitWinRTBase.h` supplies the
minimal OS-stable async interfaces needed by this toolchain and documents each
method and status value.
