# WebKitWebViewUWP — WPE WebKit core for UWP ARM32 (950 XL)

## Active M10 design

Read [DESIGN.md](DESIGN.md) for Sol's detailed C++/CX architecture, naming,
folder structure, public contract, compiler boundary and migration gates.
[M10 OpenSpec](../../openspec/changes/wkview-m10-uwp-cppcx-restructure/README.md)
contains the normative requirements, migration map and implementation tasks for
the implementing model. M9 is superseded history; M10 implementation is in
progress. The browser-free compiled CX/XAML gate now passes on the Lumia.

Names use `UWP` (for example `PageControllerUWP`), lowercase `uwp/` directories,
and the unchanged public control `WebKit.WebKitWebView` authored directly in
`Source/WebKit/UIProcess/API/uwp/WebKitWebView.h/.cpp`.

Base: WPE tag `wpewebkit-2.54.0`, branch `uwpwebkit/2.54`.
Core only: `WTF + JSC CLoop + WebCore`. No browser chrome here.

## Rules

1. Upstream tag pristine. Our work on `uwpwebkit/2.54` only.
2. Preserve upstream structure and use the established WK_UWP / PLATFORM(UWP) guards for port-specific edits.
3. ASCII paths only. Native engine exceptions remain off; MSVC C++/CX components/apps enable `/ZW` and their required exception/runtime support separately.
4. Single engine-owning thread. MiniBrowser uses the validated Skia backend; JIT remains unsupported.
5. Tags = device proof: `wkview-m0-empty`, `wkview-m1-jsc`, `wkview-m2-parse`, `wkview-m3-render`.

## Layout

- `Tools/uwp/` (here) — port tooling: triplet, toolchain, compiler wrapper,
  ARM C/C++ runtime sources + prebuilt `lib/`, env script, this doc set.
- `WebKitLibraries/triplets|toolchains/` — vcpkg triplet + cmake toolchain.
- `Source/WebKit/UIProcess/API/uwp/` — current native wrapper; M10 places the
  C++/CX public control and decomposed native/input/rendering/platform services here.
- `Tools/MiniBrowser/uwp/` — sample browser; M10 consumes the reusable component.
- Patches generated via `git diff upstream...HEAD -- Source/`.

See [ARCHITECTURE.md](ARCHITECTURE.md) for the ownership boundary between the
browser framework, MiniBrowser, device test harnesses and platform tooling.

## Verified M10 build entry points

Run from the repository root in Windows PowerShell:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\uwp\build\discover-arm32-toolchain.ps1 -SdkVersion 10.0.19041.0 -Json
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\uwp\build\check-wkuwp-abi-layout.ps1 -OutDir "$env:TEMP\opencode\m10-abi-check"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\uwp\build\test-wkuwp-engine-negotiation.ps1 -OutDir "$env:TEMP\opencode\m10-negotiation-check"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\uwp\build\test-wkuwp-engine-module.ps1 -OutDir "$env:TEMP\opencode\m10-module-check"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\uwp\build\test-wkuwp-callback-ownership.ps1 -OutDir "$env:TEMP\opencode\m10-callbacks-check"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\uwp\build\test-wkuwp-bridge-ordering.ps1 -OutDir "$env:TEMP\opencode\m10-bridge-check"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\uwp\build\check-wkuwp-legacy-surface.ps1 -OutDir "$env:TEMP\opencode\m10-legacy-check"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\uwp\build\build-arm32-cx-compiled-probe.ps1 -OutDir "$env:TEMP\opencode\m10-cx-package" -SignThumbprint <development-certificate-thumbprint>
```

The ABI layout check compiles the frozen `Source/WebKit/Shared/uwp` assertions with
both boundary compilers **as C and as C++**, and inspects all four objects with
`llvm-readobj`; the C++ pass exists because the framework consumer is C++/CX and a
divergence that only appears in one language still breaks the boundary.

`test-wkuwp-engine-negotiation.ps1` verifies the task 3.2 discovery provider in
`Source/WebCore/platform/uwp/WKUWPEngineAPI.cpp`: clean ARM32 `/W4 /WX` compiles with
both boundary compilers, an ARM32 module that exports the unmangled
`WKUWPGetEngineAPI`, and negotiation behaviour run from an x64 copy of the same
source, loaded through `GetProcAddress`. Unsupported version, table-size and
capability requests must fail with the caller's output byte-for-byte untouched, and
no call may create a page. It writes only to `-OutDir`, accepts `-MsvcVersion`, and
proves nothing about a full WebCore rebuild, module pinning or device behaviour.

Use a **fresh** package output directory. The compiled-probe driver discovers
VS/SDK/CX references, runs both XAML passes, generates matching component/app
WinMD, checks ARM images/exports/imports/PRI and signs the Appx. It accepts
`-VsPath`, `-MsvcVersion`, `-SdkRoot`, `-SdkVersion` and `-FrameworkRef` overrides;
`-SkipSign` is build-only. See [CXCompiledProbe/README.md](CXCompiledProbe/README.md)
for the verified v6 results and the full build/resource/metadata rules.

The native engine remains a separate CMake/Ninja domain. Dot-source the native
environment before configuring a fresh directory:

```powershell
. .\Tools\uwp\arm32-uwp-env.ps1 -SdkVersion 10.0.19041.0
$nativeBuild = Join-Path $env:TEMP 'opencode\m10-native-check'
$toolchain = (Resolve-Path WebKitLibraries\toolchains\Toolchain-ARM32-UWP-clang.cmake).Path
cmake -S Tools/uwp/build/native-toolchain-smoke -B $nativeBuild -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
cmake --build $nativeBuild --target WKUWPNativeToolchainSmoke --parallel 8
```

The environment supports `-VsPath`, `-MsvcVersion`, `-SdkRoot`, `-SdkVersion` and
`-VcpkgRoot`. Repository paths derive from the script location; installation flag
paths use discovered space-free aliases, not hard-coded aliases from this machine.
Missing ARM SDK libraries or unsupported Debug builds fail explicitly. Provide
Ruby, Perl, Ninja and other engine generator dependencies through the normal PATH;
no developer's Ruby profile path is inserted. Do not reuse old cached native
flags as proof of the new configuration or copy exception-disabled native flags
into a CX build.

The smoke target verifies native ARM compilation, SDK includes, RC/link inputs
and an imported OS symbol; it is not an engine execution or full WebCore rebuild.
The existing Skia build directory is preserved. Task 4.1 now also has a successful
full `WebCore` build in `build-arm-uwp-fresh`, with SDK 19041 and eight jobs. It uses
the installed dependencies; dependency-port/vcpkg triplet migration remains later
M10 build/integration work. Configure/build and PCH recovery commands:
[EngineUWP extraction evidence](../../openspec/changes/wkview-m10-uwp-cppcx-restructure/artifact/engine-uwp-extraction.md).

Detailed native/CX commands and task 2.7 verification evidence:
[M10 build-and-packaging.md](../../openspec/changes/wkview-m10-uwp-cppcx-restructure/artifact/build-and-packaging.md).
