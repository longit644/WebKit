# wkview-m2-arm-cross — thumbv7 ARM32-UWP toolchain + WTF configure

## Why

The phone is ARM32 + AppContainer. Host x64 proved the chain; now prove
clang-cl can target `thumbv7-unknown-windows-msvc` against the 22621-era
ARM libs + 15254-era API surface, starting with the smallest piece: WTF.

## What

1. `WebKitView/Toolchain-ARM32-UWP-clang.cmake` (new file, our code — not
   upstream): CMAKE_SYSTEM_NAME=Windows, CMAKE_SYSTEM_PROCESSOR=ARM,
   compiler clang-cl with `--target=thumbv7-unknown-windows-msvc`,
   CRT/linkage dynamic, exceptions off, AppContainer defines.
2. `WebKitView/arm32-uwp-env.ps1` (new file): sets INCLUDE/LIB from VS
   22621 ARM libs + SDK, PATH for clang-cl/ninja, VCPKG_ROOT.
3. Probe: configure WTF-only for ARM32 (expect AppContainer API failures —
   collect the wall list, that IS the deliverable).
4. Record every wall in `WebKitView/ARM-WALLS.md` for the shim work (M3).

## Non-goals

No JSC/WebCore yet, no .appx, no device. Configure probe only — a clean
WTF configure (or a complete wall list) closes this change.

## Acceptance

Toolchain + env script committed; WTF ARM32 configure log reviewed; wall
list filed. Owner ack → tag `wkview-m2-arm`, open M3 (shims).
