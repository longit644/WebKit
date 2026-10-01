# wkview-m5-arm-webkit — ARM WebCore configure (PORT=Win)

## Why

All 14 third-party deps are COFF-ARM. Now: does WebKit itself configure
for thumbv7-UWP with our shims (RunLoop/BSTR/PSL/getpid) and prebuilt deps?

## What

1. Point WebKit at ARM deps ( devo: `CMAKE_PREFIX_PATH=C:/vcpkg/installed/arm-uwp-webkit`
   or a `WEBKIT_LIBRARIES` overlay) + our toolchain file + PORT=Win +
   WK_WEBKITVIEW_UWP=ON + the proven flag set (INCREMENTAL/builtins/profiler
   /wasm/LCMS/JPEGXL/WOFF2 offs).
2. Iterate configure errors (expect: more banned-API hits in WebCore/PlatformWin,
   missing feature toggles). No source fixes yet beyond what M3 covered —
   collect the wall list first (ARM-WALLS.md phase 2).
3. Host build stays green (default flags path untouched).

## Non-goals

No compiling WebCore yet (configure gate only), no device, no .appx.

## Acceptance

ARM configure completes (or a complete new-wall list is filed). Owner ack →
tag `wkview-m5-arm-webkit`.
