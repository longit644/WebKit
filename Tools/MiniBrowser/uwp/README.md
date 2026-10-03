# MiniBrowser UWP

Native XAML test shell for `WebKitWebViewUWP`, built for ARM32-UWP with the same toolchain as WebCore. The shell has Back, Reload, an editable address box, Go, and a diagnostic status line. It embeds the native C++ wrapper's XAML UserControl/SwapChainPanel element. Version 1.0.0.8 attempts WebKit TextureMapper layer composition through ANGLE/D3D11, with Cairo tile rasterization and cached layer backing stores. The Cairo full-viewport/D3D11 presenter is the fallback.

## Current scope

The persistent page API supports creating/destroying a Page on one engine thread, loading app-supplied HTML, resizing, scrolling, pointer events, pumping the generic WTF run loop, and repainting. The XAML wrapper wires SizeChanged/composition-scale changes, pointer/touch drag scrolling, tap events, mouse-wheel scrolling, and CompositionTarget.Rendering. Pending scroll deltas are coalesced per frame; the native Chrome client tracks invalidation so unchanged pages do not repaint continuously. Cairo buffers use the panel's composition scale while layout/input remain in logical coordinates; the swapchain applies the inverse scale transform. The presenter reuses its upload texture and resizes the swapchain buffers.

`about:demo` and `about:second` are local demonstration pages. The demo contains a JavaScript click counter, a long document for scrolling, and fixed/sticky layer probes. Back and Reload currently operate on the shell's local demonstration history. Touch scrolling includes velocity-based inertia and a damped elastic edge transform. Live HTTPS navigation is not implemented yet: entering a remote URL reports that the network loader integration is required. The current WebCore loader strategy is local-only, so external scripts/images/stylesheets are unavailable. Keyboard/text editing inside web content, IME, clipboard host hookup, device-loss recovery, and full browser navigation remain pending. Logical viewport dimensions are currently capped at 2048 and backing-buffer dimensions at 4096 per axis; this is an initial memory bound, not a phone-resolution assumption.

The control is a reusable native C++ wrapper exposing `xamlElement()`, `loadHTML()`, `scrollBy()`, `pageState()`, and `repaint()`. It has not yet been packaged as a WinMD component or registered as a custom XAML type. Construct, call, and dispose it on the same XAML/engine UI thread.

## Build

**MiniBrowser always uses Skia (`USE_SKIA=ON`).** Build the engine in `build-arm-uwp-skia` using `Tools/uwp/SKIA-PORT.md`. The Cairo engine in `build-arm-uwp` is a diagnostic reference only; MiniBrowser staging rejects it. This deployment decision avoids silently reintroducing CPU tile painting/upload costs while updating unrelated dependencies such as TLS.

From the repository root:

```powershell
cmake -S Tools/MiniBrowser/uwp -B build-arm-minibrowser -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=C:/Users/Longi/Workspace/WebKit/WebKitLibraries/toolchains/Toolchain-ARM32-UWP-clang.cmake
cmake --build build-arm-minibrowser --parallel 8
```

Build WebCore first to provide the persistent-page bridge exports. Stage the shell and the verified updated font/painting dependencies:

```powershell
python Tools/uwp/RuntimeSmoke/stage-app.py --build build-arm-minibrowser --host MiniBrowserUWP.exe --no-probe --manifest Tools/MiniBrowser/uwp/AppxManifest.xml --engine build-arm-uwp-skia/bin --prefix C:/vcpkg/installed/arm-uwp-webkit --stage build-arm-minibrowser/package-skia --override C:/vcpkg/buildtrees/freetype/arm-uwp-webkit-rel --override C:/vcpkg/buildtrees/fontconfig/arm-uwp-webkit-rel --override C:/vcpkg/buildtrees/harfbuzz/arm-uwp-webkit-rel/src --override C:/vcpkg/buildtrees/icu/arm-uwp-webkit-rel/lib --reader "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/Llvm/x64/bin/llvm-readobj.exe"
& "C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64/makeappx.exe" pack /d build-arm-minibrowser/package-skia /p build-arm-minibrowser/MiniBrowserUWP-Skia.appx /o
```

Sign the package with the existing app-specific `CN=WebKitWebView` development certificate before Device Portal deployment. Increment the manifest version whenever the contents change. Diagnostics are written to the app's `LocalState/minibrowser.log` and debugger output.

## Verification status

**Current verified baseline: 1.0.0.33 (2026-10-03).** The owner confirms verified HTTPS loading of `https://example.com` and correct demo text/images, sticky header, scrolling and edge behavior with Skia. Device logs show GPU tile painting, zero sampled CPU tile paints/manual tile upload time, and later warmed callback batches near 60 Hz. Some slower batches remain; this is not proof of uniformly steady displayed 60 FPS. The package includes the corrected TLS libraries and `cacert.pem`. Remote loading currently fetches main HTML only; external CSS/JavaScript/images remain pending. Evidence: `openspec/changes/wkview-m8-uwp-gpu-rendering/skia-lumia950xl.md`.

WebCore's persistent bridge and the shell/control sources compile and link for ARM. MakeAppx validates the package. MiniBrowser version 1.0.0.4 installed and ran on Lumia 950 XL / 15254.603. Device logs verify initialization, composition resize/repaint, pointer events, and scroll-position changes. The owner confirmed visible rendering, JavaScript counter clicks, swiping, and local Go/Back/Reload behavior, and supplied a photo. Evidence: `openspec/changes/wkview-m7-uwp-fonts/minibrowser-lumia950xl.txt`.

Earlier launch failures were corrected by using IUserControl::put_Content and a transparent parent Grid as the hit-test surface. Setting Background directly on this phone's SwapChainPanel is unsupported. Version 1.0.0.5's high-resolution painting (3.5x panel scale, 1439x2321 pixels for a 411x663 logical viewport) was confirmed on-device with sharper text. Version 1.0.0.6's inertia/elastic behavior was also confirmed. The 1.0.0.7 CPU baseline measured roughly 13–17 ms painting plus 6–8 ms full-frame upload during scrolling.

The compositor owns a registered WebCore GLContext, attaches Chrome's root graphics layer, flushes the view's compositing tree, updates cached backing stores, and presents directly to the SwapChainPanel. Initialization/render failure or a composition-scale change selects the Cairo/D3D11 fallback. Version 1.0.0.8 built, packaged, installed, and launched, but crashed during the first EGL platform-display call. Its dump identifies ANGLE's global `std::mutex::lock` calling the phone's older inbox `msvcp140.dll` with the newer headers' constexpr mutex representation.

Version 1.0.0.9 adds `_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR` to UWP ANGLE definitions so the installed runtime initializes its own mutex storage. It installed and reached TextureMapper initialization on the Adreno 430 and root-layer attachment, passing the earlier mutex crash. Its first draw exposed an ARM32 compiler virtual-member-pointer thunk overwriting the shader's uniform-location argument.

Version 1.0.0.10 uses direct virtual dispatch in ANGLE's ARM-UWP Clang uniform setters to avoid the defective thunk. `Tools/uwp/toolchain/arm-virtual-call-probe.cpp` independently reproduces the compiler issue and demonstrates the direct-call control. It installed and rendered/composited scrolling frames on-device, with inertia and edge events and no fallback logged. Warmed samples measured roughly 10.7–12.3 ms layer-update work, 1.6–1.9 ms composition submission, and 1 ms swap. These timings do not establish visual correctness: the owner's photo showed duplicated sticky-label and button pixels after scrolling.

Version 1.0.0.11 implements Chrome's scroll/slow-scroll invalidation hooks, repainting the scrolled non-composited backing-store region because this host does not perform the blit assumed by WebKit's fast-scroll path. It also binds the default framebuffer, disables scissoring, and resets color/stencil write masks before the full-surface clear. ARM build/link, staging, MakeAppx validation, signing, installation, and launch passed. Device logs show the GPU compositor active and successful resize; visual confirmation of the ghosting fix is pending.

The owner's 1.0.0.11 recording/photo and scroll check showed continued lag and an edge transform that moved composited labels while the document stayed still. Logs measured roughly 3.36 million rasterized pixels and 25–28 ms layer-update work per scrolling frame: the RenderView still painted into the host window, and the host's complete viewport invalidation rasterized that copy every frame.

Version 1.0.0.12 makes forced UWP compositing paint the RenderView into its own backing store, following the existing forced-compositing behavior on other viewless ports. The host stops drawing a viewport copy when the document uses composited scrolling. The document and its composited children now share the outer elastic transform, and ordinary scrolling can move cached document tiles instead of repainting the host viewport. ARM build/link, package validation/signing, installation, and launch passed. Device logs confirm `gpu-document-composited-scrolling: 1`. Scrolling performance, complete page painting, and edge behavior still require device samples and owner visual confirmation. The non-coordinated TextureMapper backing store currently caches the document's full layer bounds; visible-range tile coverage and tighter memory budgeting remain follow-up work.

Diagnostics include `control-compositor-initialize`, `gpu-texturemapper-context-ready`, `gpu-root-layer-attached`, and `control-cairo-fallback`. The `gpu-flush-average-us`, `gpu-composite-average-us`, and `gpu-swap-average-us` values measure CPU-side work/submission, not GPU execution. `gpu-tile-raster-calls`, `gpu-tile-raster-pixels-per-frame`, `gpu-tile-raster-average-us`, and `gpu-tile-upload-average-us` cover GraphicsLayer tile updates, including RenderLayer backings; `gpu-host-paint-calls` alone does not cover those backings. Compare warmed scrolling samples against 1.0.0.7 after confirming that the compositor remains active.

The separate RuntimeSmoke version 1.0.0.10 has already verified static HTML/CSS/font/Cairo/CoreWindow-D3D11 presentation on this phone, with owner visual confirmation. That result does not establish XAML composition or MiniBrowser interaction correctness.

## Device checks

After the corrected shell is running:

1. Verify that the toolbar and local page are visible and the process remains running.
2. Tap the HTML button and verify its JavaScript counter changes.
3. Swipe down/up the long document and verify repaint follows its scroll position.
4. Load `about:second`, use Back, then Reload.
5. Rotate/resize the host and verify the control relayouts and presents at the new size.
6. Review `minibrowser.log` for initialization, size-change, or frame failures.
7. Verify that the fixed probe remains at the lower right and the sticky probe stays at the top after reaching its sticky position.
8. Tap Capture to save a fresh page snapshot as `LocalState/page.png`, then download it through the portal file API. Capture uses WIC PNG encoding and exports the page, not the XAML toolbar or the whole phone screen. Capture failed in the previous device baseline; the encoder failure remains unresolved. The stock mobile portal's standard screenshot endpoint is unavailable on this device.

Next live-site work needs a real single-process resource loader, frame-loader policy/client integration, cookie/storage/network context, certificate handling, and corresponding device tests. `WebKitLegacy/WebCoreSupport/WebResourceLoadScheduler` is an existing in-tree reference, but it is not wired into this local-only harness.
