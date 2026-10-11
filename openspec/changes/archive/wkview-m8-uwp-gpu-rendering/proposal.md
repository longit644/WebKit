# Proposal — M8 UWP GPU rendering

## Why

WebKitWebViewUWP now composites and presents with TextureMapper/ANGLE/D3D11, but Cairo still rasterizes page tiles on the CPU. The owner reports unsmooth scrolling, and MiniBrowser 1.0.0.12 still records repeated tile rasterization/uploads during ordinary scrolling; GPU compositing alone has not met the rendering goal.

## What Changes

- Establish and implement a GPU-backed page-painting backend compatible with ARM32-UWP, ANGLE and the Lumia 950 XL. Cairo is the first feasibility check, not an assumed GPU capability.
- Hand painted GPU textures directly to TextureMapper, with explicit context, synchronization and resource-lifetime contracts.
- Reuse unchanged tiles, bound texture memory, and preserve fixed/sticky elements, DPI correctness and coherent edge behavior.
- Measure frame pacing and input latency on-device, targeting sustained 60 Hz scrolling of the app-local static demo.
- Preserve the current Cairo/D3D11 fallback and expose which backend actually paints each frame.
- Complete M7's existing font/clipboard requirements before promoting or closing M8. The rendering experiments in M7's evidence are bring-up history, not a reason to expand M7's acceptance scope.

## Capabilities

### New Capabilities

- `uwp-gpu-rendering`: GPU-backed page painting, direct texture handoff, caching, diagnostics, lifecycle and device acceptance for WebKitWebViewUWP.

### Modified Capabilities

None. The repository currently has no archived capability specifications.

## Impact

- WebCore graphics backends, ImageBuffer creation, native images and TextureMapper tiles/texture ownership.
- `Source/WebCore/platform/uwp/`, `Source/WebKit/UIProcess/API/uwp/`, ARM-UWP build options and dependency overlays.
- `Tools/MiniBrowser/uwp/` diagnostics and device evidence.
- Backend dependencies may change: installed Cairo 1.18.6 removed GL/GLES drawing, so GPU rasterization cannot be enabled by an existing Cairo build switch.

## Non-goals

Live networking, media playback, JavaScript JIT, full browser navigation and WinMD packaging are separate milestones. Style/layout, JavaScript execution, image decoding and font preparation are not required to run entirely on the GPU. Arbitrary websites are not promised sustained 60 FPS.
