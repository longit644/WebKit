# Backend feasibility — 2026-10-02

## Cairo result

The current overlay `Tools/uwp/ports/cairo/vcpkg.json` pins **1.18.6**. Its description still mentions experimental OpenGL, but the actual source/build evidence supersedes that description:

- `C:/vcpkg/buildtrees/cairo/src/cairo-1-0384d095db.clean/NEWS`, lines 139–145: GL and GLES drawing backends were removed.
- The source's `meson.options` lists PNG, Quartz, tee, XCB, Xlib and zlib surface features; no GL/EGL/GLES option exists.
- `C:/vcpkg/installed/arm-uwp-webkit/include/cairo/cairo-features.h` defines `CAIRO_HAS_IMAGE_SURFACE`; it does not expose a GL surface backend.
- `ImageBufferCairoImageSurfaceBackend::create` allocates CPU memory and calls `cairo_image_surface_create_for_data`.
- TextureMapper's `BitmapTexture::updateContents(GraphicsLayer*, ...)` creates an unaccelerated ImageBuffer, paints it, extracts a NativeImage and uploads it.

**Conclusion:** the installed Cairo cannot provide GPU painting through a configuration switch. Cairo remains the CPU fallback. A restored historical backend would require a separate maintained dependency port and device proof.

## Candidate to investigate

`Source/WebCore/platform/graphics/skia/ImageBufferSkiaAcceleratedBackend.cpp` already uses Skia Ganesh GPU APIs and has native image/compositing integration. Its coordinated-graphics integration is not automatically usable in the current non-coordinated UWP TextureMapper host. The owner selected Skia after this initial investigation; the separate Skia build and direct tile-painting implementation are described below.

## Collected scrolling baseline

The 1.0.0.12 device log was collected from Device Portal and saved at `build-arm-minibrowser/device-1.0.0.12-scroll.log` (local ignored build artifact). It confirms document composited scrolling and zero host paint callbacks, but several regular scrolling batches have two tile raster calls per frame, with CPU-side layer flush around 11.7–14.7 ms, composition submission around 1.4–1.6 ms and swap around 1 ms. Raster/upload times are included in layer flush and must not be added twice. This is not a displayed-frame/GPU completion measurement. Owner feedback reports scrolling still feels unsmooth.

## Skia ARM-UWP implementation/build result

- Separate build: `build-arm-uwp-skia`, USE_SKIA=ON, USE_CAIRO absent/off, WebCore USE_FREETYPE=OFF; FreeType/fontconfig remain Skia dependencies and support application-local font registration.
- Skia CMake uses FreeType/fontconfig fonts with Windows file/logging implementations on UWP; desktop DirectWrite/GDI font sources are excluded.
- FreeType runtime features use the packaged build version rather than POSIX dlopen. Fontconfig interface access/string helpers use their Windows counterparts.
- `_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR` retains compatibility with the phone's old inbox MSVCP140. The engine configuration explicitly selects mimalloc and ARM compiler builtins.
- Added missing SkStrikeRef.cpp to the Skia CMake source list after its constructor caused a link failure.
- Skia.lib compiled, and the complete Skia-configured ARM WebCore.dll linked successfully using eight parallel jobs.
- CPU snapshot/capture APIs compile with GraphicsContextSkia and a caller-backed raster surface; these are not the accelerated page-painting path.
- The compositor creates a Ganesh context from the current EGL interface. Changed layer tiles wrap TextureMapper-owned RGBA textures as Skia GPU surfaces, clear/clip the dirty region, paint via GraphicsContextSkia, and flush/submit on the same GL command stream without CPU readback or tile re-upload.
- CPU fallback remains available when a tile cannot be wrapped. `gpu-tile-gpu-paint-calls` and `gpu-tile-cpu-paint-calls` distinguish the actual paths; raster timing becomes CPU-side command submission for GPU-painted tiles, not GPU completion time.
- Initial Ganesh resource-cache limit is 64 MiB; this does not bound all TextureMapper document textures and does not complete tile-memory budgeting.
- Staged 25 ARM binaries; MakeAppx validation and development signing passed for MiniBrowser 1.0.0.13 at `build-arm-minibrowser/MiniBrowserUWP-Skia.appx`.
- Deployment timed out before upload during `/api/os/info` at 192.168.1.52. No Skia GPU painting, font output, orientation, scrolling smoothness or 60 FPS claim is device-verified yet.

## Subsequent device result — 2026-10-03

After unlocking, 1.0.0.13 installed but failed in ICU's modern-header mutex construction on the old Mobile runtime. A targeted rebuild of umutex.o/unifiedcache.o and relink of icuuc78.dll resolved that blocker in 1.0.0.14. The refresh is staged from the ICU build-tree lib directory; the installed prefix remains unchanged. The overlay triplet now includes the compatibility macro for future C++ dependency builds.

Version 1.0.0.14 installed/launched and logged successful Ganesh context creation, document composited scrolling, and multiple drag/inertia/edge sequences. Measured batches show all layer tile paints using Ganesh, zero CPU tile paints and zero manual tile re-upload time. Warmed layer-update samples are about 6.5–7.7 ms, composition submission about 0.8–1.0 ms and swap about 1.9–3.1 ms in the selected regular batches. Shader warm-up and continued dirty-tile painting remain concerns. Full evidence: `skia-lumia950xl.md`. Visual correctness and 60 Hz displayed cadence remain unverified.
