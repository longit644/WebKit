# Design — M8 GPU-backed rendering

## Context and sequencing

M7 remains the existing FreeType/fontconfig/HarfBuzz and WinRT clipboard change. Bundled-font device proof exists; clipboard dispatcher/paste integration, rich formats and device copy/paste remain open. Complete those requirements before M8 implementation acceptance. No milestone is complete solely because OpenSpec artifacts validate.

The current engine is PORT=Win with PLATFORM(UWP), ARM32 clang-cl, one engine/UI thread and exceptions disabled. GPU presentation and TextureMapper compositing already run on the Adreno 430. Cairo paints ImageBuffer image surfaces, which are extracted and uploaded by BitmapTexture. MiniBrowser 1.0.0.12 gives the RenderView a cached document backing store and suppresses host viewport painting, but scrolling still frequently rasterizes two tiles per frame.

## Backend feasibility result

The overlay pins Cairo 1.18.6. Its source NEWS explicitly records removal of GL and GLES drawing; meson.options contains no GL/EGL/GLES backend option, and installed cairo-features.h exposes image surfaces but no GL surface. Restoring a removed backend would require maintaining substantial dependency code and proving compatibility, not changing a build flag.

The owner selected Skia on 2026-10-02. Port the in-tree Skia Ganesh/EGL backend in a separate `build-arm-uwp-skia` build, retaining the verified Cairo build as the reference. Selection does not establish ARM-UWP runtime compatibility. Record build/runtime feasibility before making Skia the production default. Keep the existing FreeType/fontconfig dependency stack through Skia's own font implementation; do not compile WebCore's Cairo/FreeType and Skia font implementations together. Do not silently downgrade Cairo or assume desktop GPU backends work in AppContainer.

## GPU painting and texture handoff

### Deployment decision: MiniBrowser always uses Skia

The owner reaffirmed on 2026-10-03 that all MiniBrowser builds and device packages must use `USE_SKIA=ON` and the engine from `build-arm-uwp-skia/bin`. Versions 1.0.0.30–1.0.0.32 accidentally selected the Cairo build during TLS repair, reintroducing CPU tile painting/upload costs. This was a packaging regression, not a reversal of the backend decision.

`stage-app.py` rejects MiniBrowser staging when the selected engine's CMake cache does not declare `USE_SKIA:BOOL=ON`. Use `package-skia` and `MiniBrowserUWP-Skia.appx` for deployment. The Cairo build remains a diagnostic reference only. Runtime GPU-paint counters and frame measurements must still verify the actual drawing path; selecting Skia alone does not guarantee 60 Hz rendering.

The selected backend must support GPU render targets for WebCore drawing. Define compatible texture formats, premultiplied alpha, origin/orientation, device scale and color handling. TextureMapper must consume the painted texture without routine image extraction, CPU readback or re-upload. Synchronization must prevent sampling incomplete painting, and textures must remain alive until their final consumer is finished.

Use an explicitly owned compatible context/device. The existing registered WebCore GLContext is required by TextureMapper; a raw EGL make-current call alone is insufficient. Context switching, surface resize and resource destruction remain owner-thread operations unless a documented shared-context design is introduced.

## Caching and scheduling

Instrument dirty-region causes and tile coverage before optimizing. Unchanged cached content should move through layer transforms. Newly needed tiles and changed content can paint; fixed/sticky position updates must not invalidate unrelated document tiles. Introduce visible-range coverage and bounded eviction rather than allocating textures for arbitrarily large layer bounds.

Frame scheduling must measure the entire callback, including RunLoop work, layout/rendering updates, tile painting, compositing and swap. CPU submission timings must be distinguished from GPU completion and displayed-frame intervals. Do not use glFinish in the normal path to manufacture a GPU timing number.

## Lifecycle and fallback

Handle resize/rotation, DPI changes, suspend/resume, context/device loss and memory pressure. Report initialization/draw errors and select the verified Cairo/D3D11 fallback when necessary. The fallback is not counted as GPU rasterization or successful M8 acceptance.

## Verification

Build using `ninja -j 8 WebCore` and `cmake --build build-arm-minibrowser --parallel 8`. Verify a native GPU paint probe before full-page integration, then compare text, SVG, images, gradients, transparency, transforms and clipping against the Cairo reference. Exercise DOM updates and repeated cached scrolling, fixed/sticky elements and edge pulls.

For the local static demo, the target is a stable 60 Hz presentation cadence with p95 complete active-frame CPU work below 16.7 ms and measured missed-refresh statistics. Actual displayed cadence and owner visual confirmation are required; averages, a GPU renderer string and successful Present calls are insufficient. Record first-use shader/tile costs separately from warmed samples.

## Open decisions

- Which maintained GPU raster backend can build and run on ARM-UWP with this dependency/toolchain stack?
- Can it share textures with the existing ANGLE context without readback?
- Which invalidation source causes the current repeated scroll-time tile painting?
- What tile memory/coverage budget works on both Lumia 950 XL and the lower-memory target?
