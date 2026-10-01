# wkview-m3-deps-2 — prebuilt ARM deps via vcpkg overlay triplet (ICU first)

## Why

vcpkg cannot target ARM32 out of the box (no ARM32 MSVC compiler), but our
M2/M3 probes proved clang-cl 19.15 compiles AND links thumbv7 AppContainer
binaries (COFF-ARM proof + own builtins). Teach vcpkg that toolchain via a
custom overlay triplet, then build the portable dep stack: icu → curl →
zlib/png/jpeg → xml2/sqlite/webp → harfbuzz.

## What

1. `WebKitLibraries/triplets/arm-uwp-webkit.cmake` (new): chainload our clang
   toolchain, CC/CXX=clang-cl, `--target=thumbv7-unknown-windows-msvc`,
   CRT/link recipe from toolchain v0.3 (msvcurt, OneCoreUAP, ucrt arm,
   builtins), Release-only.
2. `vcpkg install icu --overlay-triplets=WebKitView/triplets
   --triplet arm-uwp-webkit` (classic, C:\vcpkg). Iterate on port failures.
3. On success: curl + zlib (+ record exact versions in ARM-WALLS.md).

## Non-goals

No WebCore ARM configure yet (needs the dep set first), no device.

## Acceptance

`installed/arm-uwp-webkit/{lib/icudt*.lib, ...}` present and `llvm-readobj`
reports ARMNT; versions logged. Owner ack → tag `wkview-m3-deps-2`.
