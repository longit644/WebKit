# ARM32-UWP walls (probe log, 2026-09-30)

Toolchain: `WebKitView/Toolchain-ARM32-UWP-clang.cmake` v0.2 +
`WebKitView/arm32-uwp-env.ps1`. clang-cl 19.15, SDK 22621 ARM libs present.

## Solved in probe

1. **Compiler-check link** (`msvcrtd.lib`/`oldnames.lib` missing): fixed with
   `CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY` + Release-only CRT.
   clang-cl compiles thumbv7 objects clean (`probe.obj` OK).
2. **Configure from repo root**, not `Source/` (`WEBKIT_FRAMEWORK_DECLARE`).
3. **CMake 3.31 empty-var `string(REPLACE)`** (OptionsMSVC 108-113): pre-seed
   the six `*_LINKER_FLAGS*` with `/INCREMENTAL:NO` (line 114 adds it anyway).

## Open (M3 work)

4. **clang `builtins-arm.lib` missing.** Neither scoop LLVM23 nor VS clang 19
   ship Windows/ARM builtins. Needed at link time. Fix: official LLVM 20+
   release (check for `clang_rt.builtins-arm.lib`) or build compiler-rt.
5. **MSVC ships no ARM32 CRT/vcruntime.** Link surface = SDK 22621
   `um\arm` (OneCore/OneCoreUAP/WindowsApp/mincore) + `ucrt\arm` — present.
   Debug configs unsupported, Release only.
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
- First `WK_WEBKITVIEW` source shims per `WebKitView/SHIM-PLAN.md`
  (generic RunLoop, no-BSTR, curl caps, thread stacks).
