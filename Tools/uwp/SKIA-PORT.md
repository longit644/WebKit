# ARM-UWP Skia build

The owner selected Skia for M8 GPU-backed page painting. This is a separate build from the verified Cairo engine, using the in-tree Skia Ganesh/EGL renderer and the same ARM dependency prefix/runtime.

**Historical M8 deployment decision (2026-10-03):** the removed native WRL MiniBrowser used Skia from `build-arm-uwp-skia/bin` and deployed `MiniBrowserUWP-Skia.appx`; that shell and its packaging manifest are no longer in this tree. The current C# application shell is `Tools/MiniBrowser/uwp/csharp/` and builds through `build-arm-csharp-minibrowser.ps1`. The Skia engine configuration and M8 device results below remain as historical renderer evidence, not a current MiniBrowser package recipe. See `openspec/changes/wkview-m8-uwp-gpu-rendering/design.md`.

## Configure and build

Use the engine environment documented for the existing ARM build (Ruby, Perl, gperf, LLVM tools and VCPKG_ROOT). From the repository root:

```powershell
cmake -S . -B build-arm-uwp-skia -G Ninja -DPORT=Win -DWK_UWP=ON -DCMAKE_BUILD_TYPE=Release "-DCMAKE_TOOLCHAIN_FILE=C:/Users/Longi/Workspace/WebKit/WebKitLibraries/toolchains/Toolchain-ARM32-UWP-clang.cmake" "-DCMAKE_PREFIX_PATH=C:/vcpkg/installed/arm-uwp-webkit" "-DCLANG_BUILTINS_LIBRARY=C:/Users/Longi/Workspace/WebKit/Tools/uwp/lib/clang_rt.builtins-arm.lib" -DUSE_SKIA=ON -DUSE_SKIA_ENCODERS=OFF -DUSE_MIMALLOC=ON -DUSE_SYSTEM_MALLOC=OFF -DUSE_AVIF=OFF -DUSE_LCMS=OFF -DUSE_JPEGXL=OFF -DUSE_WOFF2=OFF -DENABLE_C_LOOP=ON -DENABLE_JIT=OFF -DENABLE_SAMPLING_PROFILER=OFF -DENABLE_WEBASSEMBLY=OFF -DENABLE_XSLT=OFF -DENABLE_FULLSCREEN_API=OFF
ninja -C build-arm-uwp-skia -j 8 WebCore
```

Skia uses FreeType/fontconfig internally; WebCore's separate Cairo/FreeType font sources must not also compile. The UWP host retains its application-local fontconfig initialization. Do not use the host x64 compiler-builtins library for ARM linking.

## Current application packaging

The former native `MiniBrowserUWP.exe` staging/package recipe has been retired with the WRL shell. Build the current C# application package using `Tools/MiniBrowser/uwp/csharp/build-arm-csharp-minibrowser.ps1` and a fresh TEMP output directory. It creates/signs the component and C# host package independently of this historical Skia engine build.

## GPU path and verification

TextureMapperCompositorUWP owns Ganesh alongside its registered EGL GLContext. BitmapTexture's UWP/Skia layer-paint path wraps the texture directly, paints the dirty region through a GPU-backed GraphicsContextSkia, then flushes/submits before TextureMapper samples it on the same command stream. Snapshot/capture and failed texture-wrap paths can still paint on the CPU.

Check `gpu-paint-backend-skia-ganesh`, `gpu-tile-gpu-paint-calls`, `gpu-tile-cpu-paint-calls`, and upload counters. Ganesh context creation alone is insufficient proof of GPU tile painting. Validate text, gradients, SVG, image drawing, transparency, partial dirty updates, texture orientation, DOM changes, fixed/sticky layers and edge behavior. Full-frame pacing and GPU completion remain separate from CPU submission timing.

Build/link and package validation/signing passed. Version 1.0.0.13 exposed the old-runtime mutex initialization issue in ICU. A targeted ICU common-library refresh fixed that blocker; version 1.0.0.14 installed and ran with Ganesh GPU tile painting, zero measured CPU tile paints and zero manual tile re-upload time. The ICU refresh uses common/Makefile.local in its build tree and is not yet a refreshed installed dependency prefix. Owner visual acceptance and displayed 60 Hz cadence remain pending. OpenSpec evidence: `openspec/changes/wkview-m8-uwp-gpu-rendering/skia-lumia950xl.md`.
