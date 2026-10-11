# Skia bring-up — Lumia 950 XL, 2026-10-03

## Current verified baseline — 1.0.0.33 (owner accepted)

Package: `WebKitWebView.MiniBrowser_1.0.0.33_arm__2932e6rj4m41p`, built from the current MiniBrowser sources and `build-arm-uwp-skia/bin` engine. Includes rebuilt OpenSSL 3.6.4/libcurl 8.22.0 using the corrected Microsoft ARM division ABI helpers, `cacert.pem`, and the documented refreshed ICU common-library override.

The owner confirmed that `https://example.com` loads correctly in v33, and that the demo's text/images, sticky header, scrolling and edge behavior are correct. The owner also reports scrolling feels better after restoring Skia. Versions 1.0.0.30–1.0.0.32 accidentally packaged the Cairo engine during TLS repair; v33 restores the selected backend. MiniBrowser staging now rejects an engine CMake cache without `USE_SKIA:BOOL=ON`.

Device logs confirm successful control/clipboard initialization, status-bar hide request, `gpu-paint-backend-skia-ganesh: 1`, and composited document scrolling. All sampled tile paints are GPU paints; CPU tile paints and manual tile upload time are zero. Representative later warmed batches:

| Frames | Work average / p95 / max (ms) | Callback rate (Hz) | Over-budget / long intervals |
| ---: | --- | ---: | --- |
| 64 | 7.391 / 11.061 / 11.487 | 59.814 | 0 / 0 |
| 64 | 4.961 / 10.288 / 13.267 | 59.913 | 0 / 0 |
| 64 | 6.008 / 11.882 / 13.831 | 60.002 | 0 / 0 |

Other batches still show slower work and intervals, including work p95 above 20 ms and first-use resource/submission costs. These results establish the working Skia + TLS baseline and the owner's visual acceptance for the tested pages, not universally steady 60 FPS, measured scan-out, complete web navigation, or completion of every M8 requirement. Remote navigation currently fetches the main HTML only; external CSS/JS/images still need WebCore resource-loader integration.

## Locked smooth-simple baseline — 1.0.0.20 (owner accepted)

Simple pages meet the 60Hz callback budget with GPU tile painting and correct sticky/fixed:

- Demo (tall, 6442 logical): streams 2 small tiles/frame, ~283k px, paint ~1.3ms, work 8-9ms avg / p95 11-12ms, over-budget ~0, intervals ~16.6ms. Correct bounded-memory streaming, not waste.
- Plain (short, 1312 logical, 1.0.0.18 log): zero paints/uploads after warmup, work ~3.3ms avg / p95 ~4.4ms, over-budget 0. Fully cached best case.
- Owner confirmed sticky/fixed stay correct on demo and plain feels smooth. Raw logs: `build-arm-minibrowser/device-1.0.0.18-plain-vs-demo.log` (both pages), `build-arm-minibrowser/device-1.0.0.20-demo-vs-plain.log` (demo streaming).
- This locks the simple-page model: retained GPU tile surfaces, single batched submit/frame, viewport-stable pinned invalidation, 60Hz callbacks. Heavy pages build on streaming, plus prefetch and memory bounds. Scan-out still not directly measured.

Device: RM-1085, Windows 10 Mobile 15254.603 ARM. Portal: https://192.168.1.52. Build: ARM32 clang-cl 19.1.5, Skia Ganesh/EGL + TextureMapper/ANGLE/D3D11.

## 1.0.0.13 failure and correction

Installation was accepted, but launch returned RPC failure. `MiniBrowserUWP.exe.5300.dmp` records 0xc0000005 at 0x62f13a93 in inbox msvcp140.dll. Stack memory contains callers in icuuc78.dll (base 0x5faf0000); initialization logged through font registration, persistent page creation and clipboard dispatcher installation, then failed before HTML-load completion.

ICU's umutex.cpp and unifiedcache.cpp construct std::mutex objects using the modern headers' constexpr representation. The phone runtime requires its own initialization, as previously observed for ANGLE. Rebuilt both objects with `_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR` and exceptions disabled, then relinked icuuc78.dll against the current custom ARM runtime with `_tls_used` retained. The build-tree `common/Makefile.local` contains that local refresh recipe; the installed prefix is not replaced. The overlay triplet now passes the compatibility macro in C++ flags for future dependency builds. This is a targeted ICU refresh, not a complete third-party runtime audit.

## 1.0.0.14 device proof

Package: `WebKitWebView.MiniBrowser_1.0.0.14_arm__2932e6rj4m41p`. Staging adds `C:/vcpkg/buildtrees/icu/arm-uwp-webkit-rel/lib` to the font/painting overrides. MakeAppx validation and signing passed. Installation returned Success=true, Code=0; launch returned HTTP 200.

Selected device log:

```text
clipboard-ui-dispatcher-installed: 0x00000000
control-clipboard-dispatcher: 0x00000000
persistent-html-loaded: 0x00000000
gpu-paint-backend-skia-ganesh: 0x00000001
gpu-texturemapper-context-ready: 0x00000000
ANGLE (Qualcomm, Qualcomm Adreno 430 (0x46363432) Direct3D11 vs_5_0 ps_5_0, D3D11-11.18.1078.53): 0x00000000
gpu-root-layer-attached: 0x00000001
gpu-compositor-enabled: 0x00000000
gpu-document-composited-scrolling: 0x00000001
control-resize: 0x00000000
control-dpi-change: 0x00000000
```

Dragging, inertia and edge-hit/return events occur in the retrieved log. Selected warmed regular scrolling batches:

| Counter | Batch A | Batch B | Batch C |
| --- | ---: | ---: | ---: |
| Frames sampled | 54 | 60 | 60 |
| Layer flush/update CPU time (us/frame) | 7744 | 7037 | 6460 |
| Composition submission CPU time (us/frame) | 803 | 804 | 957 |
| Swap call CPU time (us/frame) | 3082 | 1936 | 1970 |
| Tile paint calls | 108 | 120 | 120 |
| GPU tile paint calls | 108 | 120 | 120 |
| CPU tile paint calls | 0 | 0 | 0 |
| Host paint callbacks | 0 | 0 | 0 |
| Tile CPU image-upload time | 0 | 0 | 0 |

These counters prove that the measured changed-layer tiles use the direct Ganesh painting branch rather than CPU ImageBuffer extraction/re-upload. They do not exclude CPU font shaping, path preparation, decoding, Skia-internal uploads or fallbacks, and they do not measure GPU completion. Tile painting is included in layer flush time; do not add it twice.

The initial batch has much higher painting/submission cost (roughly 72 ms layer update and 18 ms composition per frame averaged over the first eight samples). Shader/resource warm-up remains visible and must be evaluated separately. Regular scrolling still paints about two tiles per frame, so caching/invalidation optimization remains open.

Raw log saved locally at `build-arm-minibrowser/device-1.0.0.14-skia.log`. The app remained responsive to multiple scroll sequences in that log, with no fallback or GL error reported in the retrieved sample. Owner visual correctness, smoothness and complete/displayed frame cadence are pending. This does not establish 60 FPS or M8 completion.

## Owner feedback and 1.0.0.15 correction candidate

The owner reports 1.0.0.14 is much better than Cairo, but a photo shows heavier/doubled-looking text in sections 5 and 6 compared with neighboring cards. The owner also reports slight sticky-label movement during scrolling. Visual correctness is therefore not accepted yet.

The GPU partial-repaint translation used the rounded logical sourceRect origin. At 3.5x DPI, that changes the raster origin with the dirty rectangle, even within the same tile. The GPU path now uses `(targetRect.origin - sourceOffset) / scale` for its transform; the rounded sourceRect is only used for conservative paint traversal. Clearing uses SkCanvas with an integer device-pixel clip and transparent clear before drawing, preserving pixels outside the dirty region. This addresses the observed paint-grid instability without switching tile painting back to the CPU. Sticky movement must be retested to distinguish glyph repaint jitter from independent layer-position snapping or elastic movement.

Version 1.0.0.15 ARM WebCore compilation/linkage, staging, MakeAppx validation and signing passed. Portal install returned HTTP 202 and deployment state returned HTTP 204. The launch attempt timed out during the initial /api/os/info request before submitting launch. Device rendering and visual correction are not yet verified for this candidate.

After unlocking, the first launch request returned Element not found, so the prior HTTP 204 did not establish package availability. Reinstallation returned Success=true, Code=0, followed by launch HTTP 200. The retrieved 1.0.0.15 log confirms Ganesh initialization, composited document scrolling, successful resize, and multiple drag/inertia/edge sequences. All sampled layer tile paints use GPU painting; CPU tile-paint count and manual tile upload time remain zero. Selected warmed layer-update times are about 4.7–6.6 ms with composition submission about 0.8–1.0 ms; swap waits vary and are not displayed-frame measurements. The first eight-frame sample includes high shader/resource warm-up costs.

Raw log: `build-arm-minibrowser/device-1.0.0.15-skia.log`. Owner confirmation of consistent text weight and sticky stability is still pending; successful GPU execution does not establish that visual fix.

The owner subsequently confirmed sections 5/6 are correct, the sticky label no longer moves slightly, and rendering is smoother than Cairo. This accepts the reported text-origin/sticky visual regression for 1.0.0.15, not all M8 rendering scenarios or 60 Hz cadence.

## 1.0.0.16 caching and pacing candidate

Code inspection found that requiresCompositingForPosition promotes sticky content only for async-scrollable hosts. This standalone synchronous TextureMapper host therefore painted the sticky element into the document backing, causing movement to repaint its old/new areas. Forced UWP main-frame compositing now promotes stacking-context sticky elements without an overflow-clip ancestor to their own layers. Normal overflow/nested-scroll cases retain their existing rules.

The wrapper now measures the active Rendering callback from inertia/scroll processing through RunLoop/rendering updates, dirty queries, compositing and swap. In 64-submission warmed batches it reports:

- `work-us`: average/p95/max callback work (excluding the summary report itself).
- `interval-us`: average/p95/max QPC start intervals for consecutive callbacks that submit frames; idle gaps and intervals >=250 ms are excluded.
- `callback-mHz`: observed consecutive submission-callback rate in millihertz, not scan-out FPS.
- `over-budget`: callbacks exceeding 16.667 ms work.
- `long-intervals`: consecutive intervals exceeding 25 ms.
- `scroll-us`, `engine-us`, `render-us`: average work by phase, summing to the callback work average.

The first eight submitted callbacks after HTML load are excluded from these warmed batches; compositor logs retain initial shader/tile costs. Diagnostics are observational and do not suppress legitimate invalidation or alter time to claim 60 FPS. Actual display cadence, GPU completion and OS-to-app input delivery are not measured by this callback profiler.

Both ARM builds (eight parallel jobs), staging, validation/signing, installation (Success=true, Code=0) and launch passed for 1.0.0.16. Startup shows Ganesh and document composited scrolling active. Continuous warmed scrolling samples and owner regression checks are pending.
