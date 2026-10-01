# Tasks — wkview-m3-deps-2

- [x] Agent: write `WebKitLibraries/triplets/arm-uwp-webkit.cmake`
- [x] Agent: `vcpkg install icu` for the triplet, iterate failures
- [x] Agent: verify ARMNT libs, log versions, commit, push
- [x] Agent: full stack same way: curl/openssl/zlib/bzip2/brotli/png/jpeg/webp/freetype/harfbuzz(+overlay)/xml2(-iconv overlay)/sqlite3
- [x] Owner: ack; agent tags `wkview-m3-deps-2`

ICU 78.3 ARM32 proof (2026-09-30): icuuc78.dll 1.37 MB + icuin78.dll 1.98 MB
+ icudt78.dll 33 MB + icuio, all COFF-ARM/thumb/32bit, installed to
C:\vcpkg\installed\arm-uwp-webkit. Walls cleared en route: vcpkg flag
pipeline (toolchain *_INIT authoritative), -Xcompiler escaping vs clang-cl
(wrapper), msys path conversion (EXCL list), missing ARM builtins (built),
missing CRT startup/vcruntime (own crtstart + crrt runtime: div helpers,
security cookie, sized delete, TLS/thread-once, atexit, float converts,
call_once via InitOnce, DllMain+.CRT walk, RTTI stubs), cleanupret
(-EHsc strip), UCRT stddef shadowing (-imsvc), rc.exe (shim PATH+INCLUDE),
LINK.EXE shim, msvcrt-managed ti_inst dodge (own stubs first).
