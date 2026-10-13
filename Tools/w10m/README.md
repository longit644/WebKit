# WebKitWebView W10M — WPE WebKit core for Windows 10 Mobile ARM32 (950 XL)

## Active M10 design

Read [DESIGN.md](DESIGN.md) for Sol's detailed C++/CX architecture, naming,
folder structure, public contract, compiler boundary and migration gates.
[M10 OpenSpec](../../openspec/changes/wkview-m10-w10m-cppcx-restructure/README.md)
contains the normative requirements, migration map and implementation tasks for
the implementing model. M9 is superseded history; M10 implementation is in
progress. The browser-free compiled CX/XAML gate now passes on the Lumia.

Framework port paths and authored implementation identifiers use `W10M` (for
example `PageControllerW10M`) and lowercase `w10m/` directories. Windows SDK/UWP
APIs and their canonical names remain unchanged. The public control remains
`WebKit.WebKitWebView` in `Source/WebKit/UIProcess/API/w10m/WebKitWebView.h/.cpp`.

Base: WPE tag `wpewebkit-2.54.0`, branch `w10mwebkit/2.54`.
Core only: `WTF + JSC CLoop + WebCore`. No browser chrome here.

## Rules

1. Upstream tag pristine. Our work on `w10mwebkit/2.54` only.
2. Preserve upstream structure and keep the underlying Windows app-model guards/API semantics intact for port-specific edits.
3. ASCII paths only. Native engine exceptions remain off; MSVC C++/CX components/apps enable `/ZW` and their required exception/runtime support separately.
4. Single engine-owning thread. MiniBrowser uses the validated Skia backend; JIT remains unsupported.
5. Tags = device proof: `wkview-m0-empty`, `wkview-m1-jsc`, `wkview-m2-parse`, `wkview-m3-render`.

## Layout

- `Tools/w10m/` (here) — W10M tooling: triplet, toolchain, compiler wrapper,
  ARM C/C++ runtime sources + prebuilt `lib/`, env script, this doc set.
- `WebKitLibraries/triplets|toolchains/` — vcpkg triplet + cmake toolchain.
- `Source/WebKit/UIProcess/API/w10m/` — authored C++/CX public control and its
  decomposed native/input/rendering/platform services.
- `Tools/MiniBrowser/w10m/csharp/` — C# W10M sample-browser application over `WebKit.winmd`. The retired WRL sample shell has been removed.
- Patches generated via `git diff upstream...HEAD -- Source/`.

See [ARCHITECTURE.md](ARCHITECTURE.md) for the ownership boundary between the
browser framework, MiniBrowser, device test harnesses and platform tooling.

## Verified M10 build entry points

Run from the repository root in Windows PowerShell:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\w10m\build\discover-arm32-toolchain.ps1 -SdkVersion 10.0.19041.0 -Json
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\w10m\build\check-wkw10m-abi-layout.ps1 -OutDir "$env:TEMP\opencode\m10-abi-check"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\w10m\build\test-wkw10m-engine-negotiation.ps1 -OutDir "$env:TEMP\opencode\m10-negotiation-check"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\w10m\build\test-wkw10m-engine-module.ps1 -OutDir "$env:TEMP\opencode\m10-module-check"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\w10m\build\test-wkw10m-callback-ownership.ps1 -OutDir "$env:TEMP\opencode\m10-callbacks-check"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\w10m\build\test-wkw10m-bridge-ordering.ps1 -OutDir "$env:TEMP\opencode\m10-bridge-check"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\w10m\build\check-wkw10m-legacy-surface.ps1 -OutDir "$env:TEMP\opencode\m10-legacy-check"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\w10m\build\build-arm32-cx-compiled-probe.ps1 -OutDir "$env:TEMP\opencode\m10-cx-package" -SignThumbprint <development-certificate-thumbprint>
```

For a fresh end-to-end M10 build, use the orchestration entry point (each run
requires a new output root):

```powershell
pwsh -NoProfile -File Tools\w10m\build\build-m10-clean.ps1 -OutRoot "$env:TEMP\opencode\m10-clean-01" -SdkVersion 10.0.19041.0 -SkipSign
```

It resolves and records the VS/MSVC/SDK identities, builds the named native
targets (`WebCore`, `JavaScriptCore`) with eight jobs by default, then builds the
CX component and compiled-XAML app. Use `-DryRun` to inspect all resolved
commands without creating output. On a memory-limited host, `-NativeParallel 2`
reduces native concurrency while retaining the same fresh-build and identity
checks; this does not establish that the eight-job build passes on that host.
If a build is interrupted, rerun with `-Resume` and the same `-OutRoot` and
explicit tool versions; the script validates recorded discovery and the W10M
CMake cache, resumes native targets, and reuses only completed CX phases with
their build identities. The individual component/app commands above remain
supported for targeted builds.

For task 11.0 phone qualification only, `-HostHtmlCandidate` opts the configured
engine into `StructuredNotifications (0x001)` + `HostHtml (0x002)` + `GPUCompositor (0x010)`
(mask `0x013`) for the local HostHtml presentation path. `CertificateDecisions (0x040)`
remains gated for warning/consent and is not part of this demo mask.
The ordinary build keeps the production capability mask at zero (`0x0`); do not use the
candidate as a release package until its matching on-device run is recorded.

`build/stage-minibrowser-candidate.py` also requires `--dependency-baseline`
and `--baseline-identity`: use a previously phone-tested dependency stage and
its recorded DLL hashes. The baseline is passed through the runtime stager's
`--override` before the vcpkg prefix. The configured build retains priority for
`WebCore.dll`, `JavaScriptCore.dll`, `libEGL.dll` and `libGLESv2.dll`; staging checks
all four payload hashes against that build. The preserved vcpkg install contains
older FreeType/Fontconfig runtime helpers and
ICU mutex initialization; copying those DLLs back into an otherwise fresh
package reintroduces their phone failures. Staging validates every baseline DLL
hash and records baseline provenance alongside the final payload hashes.
Candidate native diagnostics are written to `AC/Temp/hosthtml-engine.txt` on
Mobile; the C# event/state report remains `LocalState/hosthtml-candidate.txt`.

The CMake cache option `WKW10M_FRAME_READBACK_DIAGNOSTICS` defaults to `OFF`.
Enabling it in a HostHtml candidate adds a synchronous `glReadPixels` sample to
geometry/exposure frames; leave it off for interaction-performance comparisons.
Those frames still report `gpu-painted-present ... readback=off`. Build the C#
shell with `-HostHtmlSmoke` and omit `-LayoutDiagnostics` for the same comparison:
layout snapshots otherwise call `UpdateLayout()` on the UI thread. GPU aggregate
`flush-us`, `composite-us` and `swap-us` values are microseconds; v99 and earlier
logs overstate those three values by 1,000 and require normalization before
comparison. Tile `paint-us`, `upload-us` and `submit-us` were already correct.

The ABI layout check compiles the frozen `Source/WebKit/Shared/w10m` assertions with
both boundary compilers **as C and as C++**, and inspects all four objects with
`llvm-readobj`; the C++ pass exists because the framework consumer is C++/CX and a
divergence that only appears in one language still breaks the boundary.

`test-wkw10m-engine-negotiation.ps1` verifies the task 3.2 discovery provider in
`Source/WebCore/platform/w10m/WKW10MEngineAPI.cpp`: clean ARM32 `/W4 /WX` compiles with
both boundary compilers, an ARM32 module that exports the unmangled
`WKW10MGetEngineAPI`, and negotiation behaviour run from an x64 copy of the same
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
. .\Tools\w10m\arm32-w10m-env.ps1 -SdkVersion 10.0.19041.0
$nativeBuild = Join-Path $env:TEMP 'opencode\m10-native-check'
$toolchain = (Resolve-Path WebKitLibraries\toolchains\Toolchain-ARM32-W10M-clang.cmake).Path
cmake -S Tools/w10m/build/native-toolchain-smoke -B $nativeBuild -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
cmake --build $nativeBuild --target WKW10MNativeToolchainSmoke --parallel 8
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
full `WebCore` build in `build-arm-w10m-fresh`, with SDK 19041 and eight jobs. It uses
the installed dependencies; dependency-port/vcpkg triplet migration remains later
M10 build/integration work. Configure/build and PCH recovery commands:
[Windows 10 Mobile engine extraction evidence](../../openspec/changes/wkview-m10-w10m-cppcx-restructure/artifact/engine-w10m-extraction.md).

Detailed native/CX commands and task 2.7 verification evidence:
[M10 build-and-packaging.md](../../openspec/changes/wkview-m10-w10m-cppcx-restructure/artifact/build-and-packaging.md).
