# UWP C++/CX component sources

The projection in use is C++/CX, not C++/WinRT, because the SDK's C++/WinRT base
header rejects 32-bit ARM. This directory is named for that projection. It was
`WinRTComponent/` while the prototype was C++/WinRT.

Two things live here and they have different fates. `XamlProbe.idl` and
`XamlProbeComponent.cpp` are the browser-free gate probe, which M10 keeps. The
`WebKitWebView*` files are the prototype control that M10 replaces with authored
sources in `Source/WebKit/UIProcess/API/uwp/`; do not add to them.

## M10 authored control

The new public declarations live in `Source/WebKit/UIProcess/API/uwp/`; see that
directory's README for revision-2 identity and current implementation progress.
`ComponentExports.cpp` here is only the build/activation entry-point shell.
Build the new component with `Tools/uwp/build/build-arm32-cx-component.ps1` and
verify its generated metadata/consumer boundaries with
`Tools/uwp/build/test-cx-component-contract.ps1`. These commands use the new
authored sources and compiler-generated `WebKit.winmd`.

The historical CMake target is still a native prototype regression target. It
generates its legacy IDL/header/WinMD inside the binary directory, using discovered
SDK inputs from `arm32-uwp-env.ps1`; `LegacyWebKitWebView.h` cannot collide with
the authored CX header. The native MiniBrowser build produces this matching pair
as a subtarget. Presenter extraction evidence is in M10 `artifact/swap-chain-presenter.md`.

## XAML identity probe baseline

The compiled-page host now lives in `Tools/uwp/CXCompiledProbe/`; its README and
`build-arm32-cx-compiled-probe.ps1` describe the successful v6 device gate and
repeatable build. That driver generates component WinMD from the same MSVC
object as the DLL; the older MIDL recipe below remains the runtime-v27 baseline.

`XamlProbe.idl` declares the probe's runtime class, its default interface and its
threading model; `XamlProbeComponent.cpp` implements it. Neither is optional: the
IDL is the metadata source, and the WRL `IActivationFactory` in the .cpp is the
activation entry point.

The probe exposes `ProbeText` as a lazily registered XAML dependency property
and implements `ICustomPropertyProvider` with a `ProbeText` accessor. The native
runtime-markup host has no generated XAML type provider; `[Bindable]` alone did
not make the property discoverable in the measured build. The host validates
binding at `Loaded`, with a standard XAML binding as a timing control, and keeps
each result in `LocalState\xaml-probe-lifetime.txt`.

`/ZW` does **not** generate the activation or lifetime entry points. A component
compiled with `/ZW` alone has no `DllGetActivationFactory` symbol, no
`DllCanUnloadNow` export and no `/EXPORT` directive, and the linked DLL cannot be
activated or queried for unload safety. The link must therefore pass
`/export:DllGetActivationFactory` and `/export:DllCanUnloadNow` explicitly.

Do not feed the IDL-generated WinMD back into the `/ZW` compilation. Compiling
against the generated header redefines the runtime class (`C2011`, `C3094`,
`C2027`). The IDL and the CX class describe one contract from two directions and
are combined by the build, never by handing one to the other.

### Building it

`Tools/uwp/build/build-arm32-cx-probe.ps1` performs the compile, the WinMD
generation and the link, and verifies the result is COFF-ARM,
AppContainer-capable and exports both activation/lifetime entry points. Prefer it
over hand-written commands:

```powershell
.\Tools\uwp\build\build-arm32-cx-probe.ps1
.\Tools\uwp\build\build-arm32-cx-probe.ps1 -MsvcVersion 14.41.34120
```

It resolves the ARM32 toolset and SDK itself, accepts `-MsvcVersion` and
`-SdkVersion` overrides, and defaults its output to a temporary directory. Use
the installed Windows 10 SDK 10.0.19041.0: the newer Windows 11 SDK on this
machine has no ARM32 `um\arm` or `ucrt\arm` import libraries.

### Lifetime host

`Tools/uwp/build/build-arm32-cx-probe-host.ps1` builds the same component and
WinMD, then adds the native XAML `Application::Start` host required to construct
a `UserControl` on the package/UI thread. It stages and signs the ARM package:

```powershell
.\Tools\uwp\build\build-arm32-cx-probe-host.ps1
```

The desktop `test-arm32-cx-probe-lifetime.ps1` harness is still useful for the
rejection and HRESULT paths, but a desktop console is intentionally not accepted
as a successful XAML construction environment: `ActivateInstance` returns
`RPC_E_WRONG_THREAD` there. The packaged host writes its process result to
`LocalState\xaml-probe-lifetime.txt`; successful construction, inherited XAML
interfaces and layout are device gates, and must not be inferred from the x64
build. The current host records construction and repeated-destruction results;
the inherited-interface/layout checks belong to task 2.6. It now creates a
visible XAML status window and stays in the application event loop after the
checks instead of requesting exit. `exit=0` is the check result, not proof that
the process has exited. Package
`WebKitProbe.XamlLifetimeHost_1.0.0.10_arm__2932e6rj4m41p` installed and its
Device Portal launch request returned HTTP 200 on the Lumia 950 XL /
`15254.603.armfre.feature2_rs3svc.200106-1633`; the retrieved local-state
result was `exit=0`. A screenshot/owner observation is still required for the
visual gate. Later custom `using:WebKitProbe` markup hung during `XamlReader::Load`.
The current host instead manually activates the probe and inserts it into a
plain-XAML `ContentControl`. Package `1.0.0.27` installed and launched on the same
device and reported `exit=0`, with separate passing lifetime, inherited-interface,
binding, layout and remove/reinsert results. The control is checked for `IControl`,
`IFrameworkElement` and `IUIElement`; `UserControl` does not derive from
`ContentControl`. Before activation the bound text is empty; at `Loaded` it
matches the probe value. `RunAsync` returned `E_INVALIDARG` on this host, so the
validation falls back to the `Loaded` handler. Full evidence is in
`openspec/changes/wkview-m10-uwp-cppcx-restructure/artifact/xaml-binding-lumia-v27.txt`.
Compiled markup and custom declarative activation remain unproven.

The installed SDK XAML compiler was probed further. A minimal page using
`WebKitProbe.XamlProbe` completes pass 1 and pass 2 when referenced with the MSVC
`/ZW` linker-generated component WinMD and when the generated XAML page header
is included in the project's `ClInclude` set. It emits the generated page header,
XBF and a XAML type provider with probe activation and `ProbeText` accessors.
The generated sources compile for ARM32 and produce an app-side WinMD, but a
complete ARM app/package and phone run are still required; see
`openspec/changes/wkview-m10-uwp-cppcx-restructure/artifact/compiled-xaml-investigation.md`.
The compiled-XAML gate remains open, separately from the passing runtime markup
measurement.

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
