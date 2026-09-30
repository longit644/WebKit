# ARM32-UWP walls (probe log, 2026-09-30; link recipe closed same day)

Toolchain: `WebKitLibraries/toolchains/Toolchain-ARM32-UWP-clang.cmake` v0.3 +
`Tools/UWP/arm32-uwp-env.ps1`. clang-cl 19.15, SDK 22621 ARM libs present.

## Solved

1. **Compiler-check link** (`msvcrtd.lib`/`oldnames.lib` missing): fixed with
   `CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY` + Release-only CRT.
   clang-cl compiles thumbv7 objects clean (`probe.obj` OK).
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
   -DCMAKE_C_COMPILER=clang -DCMAKE_C_COMPILER_TARGET=thumbv7-unknown-windows-msvc
   -DCMAKE_ASM_COMPILER=clang -DCMAKE_ASM_COMPILER_TARGET=thumbv7-unknown-windows-msvc
   -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON`.
5. **ARM link recipe PROVEN** (`wmain-arm.exe`, COFF-ARM/ARMNT): clang-cl 19.15
   `--target=thumbv7-unknown-windows-msvc`, `-fuse-ld=lld`, `/MD`,
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
