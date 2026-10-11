# ARM32-UWP walls (probe log, 2026-09-30; link recipe closed same day)

Toolchain: `WebKitLibraries/toolchains/Toolchain-ARM32-UWP-clang.cmake` v0.3 +
`Tools/UWP/arm32-uwp-env.ps1`. clang-cl 19.15, SDK 22621 ARM libs present.

## Solved

1. **Compiler-check link** (`msvcrtd.lib`/`oldnames.lib` missing): fixed with
   `CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY` + Release-only CRT.
   clang-cl compiles armv7 objects clean (`probe.obj` OK).
2. **Configure from repo root**, not `Source/` (`WEBKIT_FRAMEWORK_DECLARE`).
3. **CMake 3.31 empty-var `string(REPLACE)`** (OptionsMSVC 108-113): pre-seed
   the six `*_LINKER_FLAGS*` with `/INCREMENTAL:NO` (line 114 adds it anyway).
4. **builtins-arm.lib: BUILT from source.** No released LLVM ships Windows/ARM
   builtins (checked scoop 23, VS 19, official 23.1.0 package). Upstream
   `compiler-rt/lib/builtins/CMakeLists.txt` defines no `arm_SOURCES` for
   WIN32-non-MINGW. Fix used: local `elseif(WIN32)` branch (GENERIC +
   thumb2 base + optfp + sync + aeabi RT/CLIB + idivmod/ldivmod/uidivmod/
   uldivmod/chkstk), `CMAKE_ASM_COMPILER_TARGET` set for .S files.
   Result: 180 objects, `__aeabi_*`/`__divsi3`/`__addsf3` present.
   Committed: `Tools/UWP/lib/clang_rt.builtins-arm.lib` (143 KB).
   Rebuild recipe: sparse llvm-project (compiler-rt, cmake, llvm/cmake),
   `cmake -S compiler-rt/lib/builtins -B build-rt-arm -G Ninja
   -DCMAKE_C_COMPILER=clang -DCMAKE_C_COMPILER_TARGET=armv7-unknown-windows-msvc
   -DCMAKE_ASM_COMPILER=clang -DCMAKE_ASM_COMPILER_TARGET=armv7-unknown-windows-msvc
   -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON`.
5. **ARM link recipe PROVEN** (`wmain-arm.exe`, COFF-ARM/ARMNT): clang-cl 19.15
   `--target=armv7-unknown-windows-msvc`, `-fuse-ld=lld`, `/MD`,
   `/APPCONTAINER /MACHINE:ARM /SUBSYSTEM:WINDOWS` (WINDOWSCE rejected),
   `/ENTRY:<own>` (no ARM32 vcruntime140_app ships), NODEFAULTLIB desktop
   CRTs, `msvcurt.lib` (MSVC lib\arm Store CRT) + OneCoreUAP + ucrt (22621
   arm) + our builtins. MSVC CRT/vcruntime for ARM32 do not exist — Store
   CRT path is the only one. Debug unsupported, Release only.
6. **vcpkg cannot build ARM32 deps.** No ARM32 MSVC compiler exists, so
   manifest/classic port builds (icu, curl, …) can't target ARM. Deps must
   arrive prebuilt (own curl like Apotheosis, WebKitLibraries-style drops).
7. **PORT base decision: Win, not WPE.** WPE hard-requires 6 Linux-only
   subsystems (GLib+GioUnix, Epoxy, Soup3, XkbCommon, LibGcrypt, Tasn1) —
   each needs a replacement port before configure passes. Win port needs
   only portable deps (CURL, HarfBuzz, ICU, JPEG, LibXml2, OpenSSL, PNG,
   SQLite3, ZLIB, LibPSL→stub, WebP) — all vcpkg-available for host and
   prebuildable for ARM. Same tree holds both ports; use `-DPORT=Win`.
   MiniBrowser `uwp/` skeleton stays valid (based on `win/`).

## Next (M3)

- Acquire/produce `builtins-arm.lib`, prebuilt ARM icu/curl/xml2/zlib/png.
- First `WK_WEBKITVIEW` source shims per `Tools/UWP/SHIM-PLAN.md`
  (generic RunLoop, no-BSTR, curl caps, thread stacks).

## Solved (M3-deps-3: cairo stack, 2026-10-01)

- harfbuzz needs `icu` feature: OptionsWin wants `HarfBuzz REQUIRED COMPONENTS
  ICU` (harfbuzz-icu). Rebuilt `harfbuzz[freetype,icu]`.
- `wchar_t` is a distinct builtin in C++: header's `unsigned short` LPCWSTR
  conflicted once windows.h loaded. `#ifdef __cplusplus` spelling in
  `uwp-desktop-apis.h`.
- `__u64tos` missing at link (no ARM-store vcruntime): implemented in
  `crrt.cpp` (expat proven).
- fontconfig file-mapping: desktop `CreateFileMapping`/`MapViewOfFile` hidden
  under PC_APP; fcwindows.h pins `_WIN32_WINNT=0x0600` (Vista) which hides the
  SDK's own Win8+ inline wrappers. Fix: `-D_WIN32_WINNT=0x0A00` in triplet +
  toolchain (meson harvests flags from CMAKE_*_FLAGS_INIT, not VCPKG_C_FLAGS).
- fontconfig gperf: `-FI intrin.h` expands into meson's preprocess->cutout
  flow; gperf copies the junk into its header (`_JUMP_BUFFER` clash). Overlay
  port `Tools/uwp/ports/fontconfig` patches cutout.py to drop pre-`%` lines.
- meson llvm-lib `/MACHINE:thumbv7` rejected: target spelling changed to
  `armv7-unknown-windows-msvc` (verified byte-identical COFF-ARM/ARMNT
  objects); meson maps 'arm' in target to /MACHINE:arm.
- fontconfig needs `_mbsrchr`, `GetWindowsDirectoryA` (+unsuffixed macro),
  `unlink/rmdir/strdup/access/chmod`: declared in `uwp-desktop-apis.h`,
  implemented in `crrt.cpp` (all absent from ARM ucrt/msvcurt).
- pthreads (nmake under cmd.exe): extensionless wrapper unusable as CC; raw
  clang-cl + full flags via `CC=` macro, link recipe via `XLIBS=`. Portfile
  scope uses `TARGET_TRIPLET`, not `VCPKG_TARGET_TRIPLET`.
- cairo win32 GDI surface+font backends cannot compile/link on UWP: overlay
  port `Tools/uwp/ports/cairo` skips them when `WINAPI_FAMILY == 2` (PC_APP);
  DWrite font backend disabled too (entangled with GDI fallback/blits).
  Fonts v0 = FreeType; DWrite returns via WebKit in phase 2.
- Duplicate CRT aliases across DLLs (cairo's strdup vs fontconfig's copy):
  `/FORCE:MULTIPLE` in `WK_UWP_LINK_FLAGS` (+ pthreads XLIBS). Copies are
  identical wrappers; proper fix later = shared wkrt.dll.
- Result: cairo 1.18.6 + full dep set installed to
  `C:/vcpkg/installed/arm-uwp-webkit` (COFF-ARM/ARMNT, APPCONTAINER);
  ARM WebCore configure green (PORT=Win + Cairo, `build-arm-uwp`).

## Graphics direction (owner directive, 2026-10-01): GPU-first, never CPU present

- Rasterization stays CPU (Cairo image surfaces): no D2D/Skia-GPU backend
  exists upstream for PORT=Win, and writing one is out of scope.
- Presentation is GPU from the start: D3D11 `SwapChainPanel` texture upload,
  not `WriteableBitmap` blit (bitmap only as a debug fallback).
- Fonts via DirectWrite (App-partition legal), never GDI file probing.
- Later perf option: GPU compositing via TextureMapper+ANGLE if viable.
- Phase 2 (committed, not dropped): GPU compositing via ANGLE (GLES-on-D3D11,
  FL 9_3) as the GL provider behind TextureMapperGL. Rasterization stays CPU
  (Cairo); compositing + presentation move to GPU. Prereq: ANGLE overlay port
  (GN toolchain, armv7-UWP) after v0 first pixels.
