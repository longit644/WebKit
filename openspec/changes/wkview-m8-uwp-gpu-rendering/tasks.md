# Tasks — wkview-m8-uwp-gpu-rendering

## 1. Prerequisite and baseline
- [x] 1.1 Preserve M7's font/clipboard scope and identify remaining acceptance work.
- [x] 1.2 Collect the 1.0.0.12 scrolling log and distinguish host repaint from tile raster/upload work.
- [ ] 1.3 Complete M7 dispatcher/paste hookup, rich clipboard formats and device verification before M8 acceptance.
- [ ] 1.4 Add complete active-frame and presentation-interval measurements to the baseline.
  - [x] Add average/p95/max active Rendering callback work and consecutive submission-callback interval measurements, with phase costs and explicit scan-out limitations (1.0.0.16).
  - [ ] Measure actual display presentation intervals or collect suitable independent device cadence evidence.

## 2. Backend feasibility
- [x] 2.1 Inspect the pinned Cairo version, features, source and build options; record removal of GL/GLES drawing.
- [x] 2.2 Identify existing WebCore accelerated ImageBuffer code as a candidate integration reference.
- [ ] 2.3 Evaluate the maintained candidate's ARM32-UWP build dependencies and ANGLE context/texture compatibility.
  - [x] Select Skia following owner direction and configure the separate ARM-UWP Skia build.
  - [x] Compile the in-tree Ganesh/EGL Skia target with UWP-compatible font and platform source selection.
  - [x] Compile and link ARM WebCore with Skia and the UWP snapshot/control bridge.
  - [x] Verify native GPU surface/context compatibility on-device (1.0.0.14 Ganesh tile-paint counters; visual acceptance remains separate).
- [ ] 2.4 Build and run a native GPU painting probe for shapes, text, gradients and images.
- [x] 2.5 Record the Skia backend decision with ARM build and initial device execution evidence.

## 3. GPU page painting and handoff
- [ ] 3.1 Enable GPU-backed WebCore ImageBuffer creation for the selected backend.
- [ ] 3.2 Implement direct painted-texture handoff to TextureMapper, including synchronization/lifetime.
  - [x] Implement and compile direct Ganesh painting into TextureMapper-owned tile textures on the same EGL context.
  - [ ] Verify GPU texture orientation, partial updates, synchronization and lifetime on-device.
- [ ] 3.3 Verify scale, pixel orientation, color and premultiplied alpha handling.
- [ ] 3.4 Verify text/SVG/image/clip/transform correctness and DOM repaint behavior.
  - [x] Implement and compile stable fractional-DPI GPU tile origins and integer-clipped dirty-region clearing after the 1.0.0.14 text report.
  - [x] Verify 1.0.0.15 text appearance after repeated scrolling and partial repaint (owner confirms sections 5/6 correct).

## 4. Scrolling and resources
- [x] 4.1 Lock smooth-simple baseline 1.0.0.20: demo streams 2 small tiles/frame at 8-9ms work, plain caches fully at ~3.3ms, both 60Hz callbacks with sticky/fixed correct (owner confirmed).
  - [x] Identify sticky content painted into the document backing and implement eligible synchronous UWP sticky-layer promotion.
  - [x] Verify viewport-anchored fixed/sticky reposition without breaking correctness (1.0.0.20 owner confirmation).
- [ ] 4.2 Add bounded visible-range coverage, prepaint and eviction for heavy pages.
- [x] 4.3 Verify fixed/sticky positioning and coherent edge bounce on simple pages (owner confirmed 1.0.0.20).
  - [x] Retest the owner's slight sticky-label movement after correcting tile paint origins (owner confirms no slight movement in 1.0.0.15).
- [ ] 4.4 Handle resize/DPI, suspension, device/context loss and memory pressure.
- [ ] 4.5 Verify explicit fallback without misreporting CPU painting as GPU painting.

## 5. Device acceptance
- [x] 5.1 Build ARM targets with eight parallel jobs and validate/sign the smooth-simple baseline 1.0.0.20.
  - [x] Build the Skia WebCore candidate, stage its DLL closure, and validate/sign MiniBrowser 1.0.0.13.
  - [x] Validate/sign 1.0.0.20 with retained GPU surfaces, batched submit and viewport-stable pinned invalidation.
- [x] 5.2 Measure warmed dragging/inertia/edge motion on Lumia 950 XL for simple pages (demo 8-9ms/p95 11-12ms, plain ~3.3ms, 60Hz callbacks, over-budget ~0).
- [x] 5.3 Verify complete-frame budget on local static demo/plain at 60Hz callbacks (scan-out still not directly measured).
- [ ] 5.4 Heavy-page work: images/gradients/video, prefetch, memory bounds, lower-memory target.
- [ ] 5.4 Exercise the lower-memory target and record resource usage.
- [ ] 5.5 Record owner visual/smoothness acceptance and archive only completed requirements.
