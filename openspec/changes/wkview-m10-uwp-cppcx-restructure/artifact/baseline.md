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

## G1 progress: C++/CX ARM32 link is blocked by the MSVC ARM runtime

Measured 2026-10-05 after the SDK 19041 install cleared the earlier blockers.
This section is task 2.2 evidence and a design-level blocker, not a task failure.

What now works:

- `Tools/uwp/build/discover-arm32-toolchain.ps1` reports **0 blockers, 27 measured capabilities**: MSVC ARM `cl.exe` 19.44.35229, `vccorlib.lib`, ARM UWP `msvcrt`/`msvcprt`/`vcruntime`, `platform.winmd`, MSBuild plus ARM v143 toolset, SDK 10.0.19041.0 `um/arm` + `ucrt/arm`, `UnionMetadata`, and the corrected native link set.
- The native ARM32 recipe was repinned to SDK 10.0.19041.0 in `WebKitLibraries/toolchains/Toolchain-ARM32-UWP-clang.cmake` and `Tools/uwp/arm32-uwp-env.ps1`, replacing the removed `msvcurt.lib` with SDK `ucrt.lib` plus MSVC ARM UWP `vcruntime.lib`/`msvcprt.lib`. A minimal `mainCRTStartup` executable then compiled and linked with `clang-cl`/`lld-link`, and `llvm-readobj` reports `COFF-ARM`, `Machine: IMAGE_FILE_MACHINE_ARMNT`, thumb, 32-bit.
- `Tools/uwp/WinRTComponent/XamlProbeComponent.cpp` compiles for ARM32 with MSVC `/ZW` against SDK 19041 metadata, both with `/EHsc` and with `/EHs-c-`.

What cannot link, and why:

`lld-link` cannot produce the C++/CX component DLL from this MSVC. The missing
symbols are not project mistakes; no ARM32 library on this machine defines them:

| Missing symbol | Referenced by | Any ARM32 library defining it? |
|---|---|---|
| `__vcrt_initialize`, `__vcrt_uninitialize` | `msvcrt.lib(utility.obj)` startup path | No |
| `eh vector dtor iterator` | C++/CX object EH cleanup, only with `/EHsc` | No |
| `DllMainCRTStartup` | `/ENTRY` when using the real UWP CRT | Only as an unusable import inside `msvcrt.lib` |

MSVC 14.44 ships no combined ARM32 Store import library (`msvcurt.lib` does not
exist for ARM on any installed toolset), so the startup, EH-helper and
cookie/`_fltused`/`__guard_check_icall_fptr` symbols that library used to supply
have no ARM home. `vccorlib.lib`, the C++/CX runtime, pulls in exactly those
paths, so every `/ZW` component link fails.

This is the risk M10 predicted: the existing native shims (`arm-crtstart.lib`,
`crrt.cpp`, `crtvft.S`) work for the native engine because it links without
`vccorlib`, but they are not a validated substitute for the C++/CX runtime.

Options for the owner, none of which this plan may choose silently:

1. **Install an older v143 ARM toolset that still ships the ARM32 Store CRT**
   (`MSVC v143 - VS 2022 C++ ARM build tools (v14.42-17.12)` or `(v14.43-17.13)`)
   and re-run this link. If that toolset contains `lib\arm\msvcurt.lib`, it
   supplies the startup, EH helper and cookie symbols in one import library and
   the native recipe can return to its original single-library form. This is the
   recommended path because it needs no new runtime code from us.
2. **Extend the repository CRT shim layer to satisfy `vccorlib`.** Explicitly
   discouraged: `build-and-packaging.md` forbids placeholder EH/RTDynamicCast
   implementations, and reimplementing MSVC's ARM startup and EH helper for a
   component runtime would need its own review, device evidence and rollback.
3. **Return to WRL component authoring.** This removes `/ZW` and `vccorlib`, but
   it also removes declarative XAML `UserControl` inheritance, which is the
   reason C++/CX was selected, so it reopens the original M9 blocker.

Until one of these is resolved, tasks 2.2-2.7 stay unchecked and no M10
migration beyond the baseline may start.

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

## G2 contract work that does not depend on the C++/CX runtime

The C++/CX ARM32 link blocker above does not affect the ABI contract, so the
frozen V1 schema was made executable while the toolset question is open.

- `Source/WebKit/Shared/uwp/WKUWPEngine.h`, `WKUWPPage.h`, `WKUWPNavigation.h`,
  `WKUWPInput.h` and `WKUWPRendering.h` carry the frozen schema: fixed-width
  types, `pack(4)`, sizes/versions on every structure, borrowed string spans with
  stated ownership, and an opaque certificate token.
- `Tools/uwp/build/wkuwp-abi-layout-check.c` restates every row of the
  native-abi.md structure table as a compile-time assertion.
- `Tools/uwp/build/check-wkuwp-abi-layout.ps1` runs it for ARM32 with both
  boundary compilers and passes:

```text
ABI layout conformance for armv7
  native compiler : .../VC/Tools/Llvm/x64/bin/clang-cl.exe
  target triple    : armv7-unknown-windows-msvc
  cx compiler      : .../VC/Tools/MSVC/14.44.35207/bin/Hostx64/arm/cl.exe
  sdk version      : 10.0.19041.0
--- native (clang-cl, armv7-unknown-windows-msvc) ---
    PASS
--- cx (MSVC ARM32 cl) ---
    PASS
Both boundary compilers agree on every frozen size, offset,
enumeration value and calling convention.
EXIT=0
```

```text
=== wkuwp-abi-layout-native.obj ===   Format: COFF-ARM / Arch: thumb / IMAGE_FILE_MACHINE_ARMNT (0x1C4)
=== wkuwp-abi-layout-cx.obj ===       Format: COFF-ARM / Arch: thumb / IMAGE_FILE_MACHINE_ARMNT (0x1C4)
```

Two corrections came out of writing this, both worth recording because they are
silent-failure classes rather than build errors:

- The Windows SDK header trees are architecture neutral; only the import
  libraries are per-architecture. The ARM32 SDK decision therefore tests
  `Lib\<version>\um\arm` and `Lib\<version>\ucrt\arm`, not `Include\...\um\arm`.
- Every ABI structure must sit inside a `pack(4)` region. The structures in
  `WKUWPEngine.h` initially sat outside one and would have taken natural 8-byte
  alignment, which would have disagreed with the frozen offsets table.

## Task status after this record

| Task | State |
|---|---|
| 1.1 | complete: baseline recorded with measured values; uncommitted probe work preserved; known-good `1.0.0.77` Skia package identified as rollback reference |
| 1.2-1.4 | complete: framework/engine symbols, exports, consumers, stale references, and UWP-conditioned WTF/PAL/WebCore sources inventoried |
| 2.1 | complete: discovery implemented, verified fail-fast, and now reports 0 blockers after the SDK 19041 install and the native link-set correction; selected versions are SDK 10.0.19041.0 and MSVC 14.44.35207. Discovery enumerates every installed ARM toolset, records each one's `msvcurt.lib` status, prefers a toolset that has it, and accepts `-MsvcVersion` |
| 2.2 | blocked: `/ZW` compiles for ARM32, but the component DLL cannot link because MSVC 14.44 provides no ARM32 Store CRT. An older v143 ARM toolset (`v14.42-17.12` or `v14.43-17.13`) is being installed to recover it; not installed yet |
| 2.3-2.7 | blocked on 2.2 |
| 3.0-3.1 | complete: frozen V1 schema headers added and dual-compiler ARM32 layout conformance proven |
| 3.2-11.x | not started |