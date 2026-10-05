# M10 baseline record

Task 1.1 evidence. Measured 2026-10-05 on the development host by running
`Tools/uwp/build/discover-arm32-toolchain.ps1` and inspecting installed
components, build directories and package metadata. Every value below is
observed, not assumed; nothing here claims device or runtime support.

## Repository state

| Item | Value |
|---|---|
| Branch | `uwpwebkit/2.54` |
| HEAD at record time | `23b87a6aec` "UWP: define M10 C++/CX restructuring plan and supersede M9" |
| Base tag | `wpewebkit-2.54.0`; `wpe-uwp-arm32/main` frozen |

Uncommitted work deliberately preserved and carried into the migration map
instead of being discarded or folded into unrelated changes:

| Path | Content |
|---|---|
| `Tools/uwp/WinRTComponent/XamlProbeComponent.cpp` | Uncommitted C++/CX ARM probe using `/ZW`: `public ref class XamlProbe : UserControl, IXamlProbe`, WRL activation shell, `DllGetActivationFactory`/`DllCanUnloadNow`. Compiles to an ARM object; never linked or packaged. |
| `Tools/uwp/WinRTComponent/README.md` | Probe IDL/CX compile recipe and the note that the probe stays outside CMake. |
| `Tools/uwp/WinRTComponent/CMakeLists.txt` | Probe deliberately excluded from the shipping target. |
| `openspec/changes/wkview-m9-uwp-webview-navigation/xaml-probe-findings.md` | Toolchain findings, including the installed SDK C++/WinRT `_M_ARM` rejection. |
| `openspec/changes/wkview-m9-uwp-webview-navigation/handoff-2026-10-05-morning.md` | Session handoff replaced by the M10 decision record. |

The probe's manual activation shell is not a template for the shipping control:
it predates the reviewed contract and has unverified reference-count and
exception behavior. M10 review findings (task 2.4, 5.1) govern the real
activation/metadata path.

## Known-good artifacts to preserve

| Artifact | Identity |
|---|---|
| `build-arm-uwp-skia/bin/WebCore.dll` | 41,846,272 bytes, written 2026-10-04 21:56; Skia Ganesh engine from `build-arm-uwp-skia` |
| `build-arm-minibrowser/MiniBrowserUWP-Skia.appx` | 45,228,611 bytes, written 2026-10-04 11:29 |
| `build-arm-minibrowser/package-skia/AppxManifest.xml` | Identity `WebKitWebView.MiniBrowser`, version `1.0.0.77`, `ProcessorArchitecture="arm"`, `TargetDeviceFamily` min `10.0.15063.0`, max tested `10.0.15254.0`, package dependency `Microsoft.VCLibs.140.00` min `14.0.0.0` |

This package is the rollback reference and the device-comparison baseline for
M10. Its version supersedes the `1.0.0.33` figure still quoted in older prose;
M10 evidence must always name the exact version it measured.

## Build tools present

| Tool | Location / version |
|---|---|
| Native compiler | `.../2022/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe` (clang-cl 19.1.5 per toolchain file), target `armv7-unknown-windows-msvc` |
| Generator | CMake + Ninja; `build-arm-uwp-skia/CMakeCache.txt` records `CMAKE_BUILD_TYPE=Release`, `CMAKE_GENERATOR=Ninja` |
| CMake / Ninja | `C:\Program Files\CMake\bin\cmake.exe`, `C:\Strawberry\c\bin\ninja.exe` |
| Codegen helpers | `C:\Strawberry\perl\bin\perl.exe`, `C:\ProgramData\chocolatey\bin\bison.exe` |
| Python | `C:\Users\Longi\scoop\apps\python\current\python.exe` |
| vcpkg triplet | `C:\vcpkg\installed\arm-uwp-webkit\lib\libcurl.lib` present |

## C++/CX (MSVC ARM32) surface that already exists

| Requirement | Status | Path |
|---|---|---|
| ARM32 compiler | present | `.../2022/BuildTools/VC/Tools/MSVC/14.44.35207/bin/Hostx64/arm/cl.exe`, banner "Optimizing Compiler Version 19.44.35229 for ARM" |
| CX runtime import library | present | `.../14.44.35207/lib/arm/vccorlib.lib` |
| CX UWP runtime libraries | present | `.../14.44.35207/lib/arm/uwp/{msvcrt,msvcprt,vcruntime,vccorlib}.lib` |
| CX metadata reference | present | `.../14.44.35207/lib/x86/store/references/platform.winmd` |
| MSBuild | present | `.../2022/BuildTools/MSBuild/Current/Bin/MSBuild.exe` |
| MSBuild ARM toolset | present | `.../MSBuild/Microsoft/VC/v170/Platforms/ARM/PlatformToolsets/v143` |

Visual Studio Community 2022 (`C:\Program Files\Microsoft Visual Studio\2022\Community`,
17.14.37710.0) is installed but exposes no ARM `cl.exe`; all ARM32 work must
resolve to the Build Tools instance. Discovery reports this rather than
silently picking an installation without an ARM compiler.

## Blocking gaps

| Blocker | Measured fact | Consequence |
|---|---|---|
| No Windows SDK with ARM32 libraries | Installed SDK: `10.0.26100.0` only, whose `um`/`ucrt` trees contain `arm64`, `x64`, `x86` and no `arm`. `Lib/10.0.22621.0`, referenced by the native toolchain file, no longer exists. `Include/10.0.16299.0` is a partial WPD-era tree with no `um`/`ucrt`/`winrt` base and no libraries. | Every ARM32 link fails: `um/arm` (`ole32.lib`, `runtimeobject.lib`, `uuid.lib`) and `ucrt/arm` (`ucrt.lib`) are absent. This blocks G1 DLL linking and also blocks a clean native engine relink. |
| Toolchain-named MSVC library missing | `.../14.44.35207/lib/arm/msvcurt.lib` (named by `Toolchain-ARM32-UWP-clang.cmake`) does not exist in this toolset; `lib/arm/store` and `lib/arm/uwp` provide `msvcrt`, `msvcprt`, `vcruntime`, `vccorlib` but no `msvcurt`. | Even after an ARM32 SDK is installed, the native link recipe must be revalidated against the libraries this MSVC actually ships. Do not assume the old name still resolves. |

Required user action: install a Windows 10 SDK that ships ARM32 UWP libraries
(10.0.19041.0 is the planned candidate), then re-run discovery and revalidate
the native link recipe. No M10 migration work may substitute ARM64 libraries,
invent exception/RTTI shims, or claim feasibility from object-only compiles.

## Recorded discovery run

```text
> powershell -NoProfile -ExecutionPolicy Bypass -File Tools/uwp/build/discover-arm32-toolchain.ps1
...
BLOCKER  Native engine      ARM msvcurt.lib named by toolchain ...\lib\arm\msvcurt.lib
BLOCKER  Windows SDK        ARM32 link libraries  No installed SDK provides
                            Lib\<ver>\um\arm and Lib\<ver>\ucrt\arm; install a
                            Windows 10 SDK such as 10.0.19041.0
Summary: 2 blocker(s), 0 warning(s), 13 measured capability(ies).
EXIT=1
```

An explicit unsupported-SDK override was also exercised to prove precise
diagnostics rather than silent fallback:

```text
> ... -SdkVersion 10.0.19041.0 -Quiet
BLOCKER  Windows SDK  requested SDK installed  10.0.19041.0 not installed; available: 10.0.26100.0
EXIT=1
```

## Task status after this record

| Task | State |
|---|---|
| 1.1 | complete: baseline recorded with measured values; uncommitted probe work preserved; known-good `1.0.0.77` Skia package identified as rollback reference |
| 2.1 | discovery implemented and verified to fail fast; cannot be completed because no ARM32-capable SDK is installed, so no versions can be selected and documented yet |
| 2.2-2.7 | blocked on the two gaps above |
| 3.x-11.x | not started |