# WK_WEBKITVIEW shim plan (derived from Apotheosis wk-winuwp.patch analysis)

Base: WPE `wpewebkit-2.54.0`. Every item guarded `#if defined(WK_WEBKITVIEW)`
with a `WebKitView:` comment. Order = dependency order (WTF first).

## 1. WTF core (M1)

- Generic RunLoop (no HWND — AppContainer has no window handle at this layer).
- `getCurrentProcessID()` instead of `getpid()` (absent from AppContainer CRT).
- Per-type thread stack reservations (JS 8 MB, rest 1 MB) — 32-bit address
  space dies with default 16 MB stacks once workers spawn.
- Crash hook: `LocalState\crash.txt` via vectored handler + SIGABRT handler
  (W10M writes no WER dump for AppContainer fast-fail).
- `RELEASE_ASSERT` prints location instead of silent death.
- No `libpsl` (stub PublicSuffixStore in driver).

## 2. JavaScriptCore (M1-M2)

- CLoop interpreter only: `ENABLE_JIT=0`, FTL off (no RWX pages for 3rd party).
- Drop `JSStringRefBSTR` (no BSTR in AppContainer partition).
- Guard `Wasm::WasmOrigin` for JIT-without-WASM config.
- 4 MB stack for JSC compiler threads (when JIT attempted later).
- Known APOTHEOSIS-class bug to pre-fix if JIT enabled: ARMv7
  `replaceWithJump` T4 displacement must be range-checked (wraps mod 32 MB).

## 3. Networking (M2)

- Own curl + OpenSSL + packaged `cacert.pem` (OS Schannel caps at TLS 1.2).
- Caps: 32 total connections, 8 per host, explicit `CURLPIPE_MULTIPLEX`.
- Cancel (not pause-drop) paused 3xx transfers on terminal paths; release
  handle/delegate/request/response on every terminal path (leak = 1 handle/load).
- Clear client in `ResourceHandle::cancel()` (dangling-client crash class).
- DNS prefetch on preconnect hints. Never send `<a ping>`.

## 4. Fonts/text (M2-M3)

- FreeType/HarfBuzz path (no CoreText/GDI in container).
- No subpixel AA (color fringing after pinch); synthetic-bold in device pixels.
- Surrogate-pair placeholder: no shaping pipeline, so never emit `.notdef`
  after non-BMP chars.

## 5. Render/composite (M3+)

- v0.1: Cairo software -> RGBA -> WriteableBitmap. Fixed/sticky needs backing
  (`AcceleratedCompositingForFixedPosition` equivalent) or flicker.
- v0.2: ANGLE D3D11 FL9_3 + TextureMapper -> SwapChainPanel, gated by init flag.
- Viewport-limited 1024px tiling (full-page textures OOM 32-bit instantly).

## 6. CMake plumbing

- `Source/cmake/OptionsMiniWPE.cmake`, `WTF/wtf/PlatformMiniWPE.cmake`,
  `JavaScriptCore/PlatformMiniWPE.cmake`, `WebCore/PlatformMiniWPE.cmake`.
- Exceptions off globally: `_HAS_EXCEPTIONS=0`, `/EHs-c-` (clang armv7
  cannot lower `cleanupret`).
- clang-cl `--target=armv7-unknown-windows-msvc`, lld-link for driver.
