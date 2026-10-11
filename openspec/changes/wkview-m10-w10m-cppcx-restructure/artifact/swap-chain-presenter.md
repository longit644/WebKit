# M10 6.1: native presenter extraction and frame metrics

## Implemented ownership

`rendering/SwapChainPresenterUWP.h/.cpp` owns the CPU D3D11 device, immediate context, retained upload and flip-sequential BGRA swap chain. Panel QI ownership is explicit and released after successful detach. Physical dimensions, V1 buffer header/alignment/stride/capacity and finite bounded scale validate before use. Pixels remain caller-owned and borrowed only during upload. The compatibility header aliases the former `WebKitWebViewPresenterUWP` name and nested FrameTiming.

The native helper's GPU path consumes the actual frozen negotiated table with the explicit page handle, retains the host Inspectable and leaves its rendering QI/AddRef and ANGLE/Skia objects to the engine. It creates no second CPU chain. Active CPU/GPU modes cannot replace each other. Failed detach preserves ownership; Pending detach blocks render/reattachment and is issued once. Runtime release acknowledgement must match the monotonic attachment cookie before host ownership ends. A late previous-cookie acknowledgement cannot release a newer attachment.

Owner-thread checks precede argument/native/COM mutation. Positive engine results use the extracted shared `native/EngineResultUWP.h` mapping, preserving the existing PageController semantics and originating negative HRESULTs.

`rendering/FrameMetricsUWP.h/.cpp` owns the wrapper's bounded pacing/paint windows, FrameTimingUWP POD and `frameProfileSampleCount=64`. Measurements use monotonic seconds as input, milliseconds in value types, and microseconds/milli-Hertz in the preserved text protocol. Eight submitted callbacks warm up; nearest-rank p95 is calculated on copied samples. Only consecutive submitted intervals below 250 ms enter cadence. CPU paint windows require eight paints and one second. Navigation resets now clear both old work samples and stage totals together, correcting the old remote-navigation partial reset. Callback cadence is not scan-out.

## Source/build integration

- Authored CX component driver compiles both rendering sources as native C++ without `/ZW` and links the required D3D11/DXGI imports.
- Native MiniBrowser, prototype component and RuntimeSmoke targets compile the extracted presenter; the wrapper delegates its reports to FrameMetricsUWP.
- Prototype MIDLRT output is generated only under the binary directory with a unique `LegacyWebKitWebView.h` basename. The sample's historical include shim is build-only, so native compilation cannot accidentally select the authored CX WebKitWebView.h. DLL and legacy WinMD are freshly rebuilt together.
- MIDLRT uses SDK 19041, its discovered preprocessor and native Windows paths; `/ns_prefix` is omitted to preserve the prototype's original C++ namespace.
- PC_APP-hidden GetModuleHandleExW/flags are declared with exact SDK semantics in the existing native compatibility header for unchanged sample fault logging.
- Stage tooling accepts `--component` to package the freshly built legacy pair.
- The legacy gate continues freezing engine declarations/export signatures and diagnostic app source bytes; presenter/wrapper extraction has its own tests.

Existing native build caches still contained removed SDK 22621/msvcurt inputs; fresh temporary native output roots use the discovered SDK 19041 recipe. The pristine Skia rollback engine/package is preserved. No WebCore behavior was changed.

## Commands and host results

On 2026-10-09, PowerShell 7.6.6, MSVC 14.44.35207, SDK 10.0.19041.0, clang-cl 19.1.5:

The following native MiniBrowser CMake commands are a historical record from before the shell relocation; `Tools/MiniBrowser/uwp` and target `MiniBrowserUWP` are not current paths/targets. The current C# shell is built with `Tools/MiniBrowser/w10m/csharp/build-arm-w10m-minibrowser.ps1`.

```powershell
.\Tools\uwp\build\test-rendering-contract.ps1 `
  -ComponentDirectory "$env:TEMP\opencode\m10-rendering-61-production-v2" `
  -OutDir "$env:TEMP\opencode\m10-rendering-61-tests-v4"
.\Tools\uwp\build\build-arm32-cx-component.ps1 `
  -OutDir "$env:TEMP\opencode\m10-rendering-61-production-v4"
.\Tools\uwp\build\test-cx-component-contract.ps1 `
  -OutDir "$env:TEMP\opencode\m10-rendering-61-contract-v1"
.\Tools\uwp\build\check-wkuwp-legacy-surface.ps1 `
  -OutDir "$env:TEMP\opencode\m10-rendering-61-legacy-check"
. "Tools\uwp\arm32-uwp-env.ps1" -VcpkgRoot C:\vcpkg
$env:CCACHE_DISABLE = '1'
$toolchain = (Join-Path (Get-Location) 'WebKitLibraries/toolchains/Toolchain-ARM32-UWP-clang.cmake')
cmake -S Tools/MiniBrowser/uwp -B "$env:TEMP\opencode\m10-rendering-61-mini" `
  -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$toolchain" -DCMAKE_BUILD_TYPE=Release
cmake --build "$env:TEMP\opencode\m10-rendering-61-mini" --target MiniBrowserUWP --parallel 8
cmake -S Tools/uwp/RuntimeSmoke -B "$env:TEMP\opencode\m10-rendering-61-runtime" `
  -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$toolchain" -DCMAKE_BUILD_TYPE=Release
cmake --build "$env:TEMP\opencode\m10-rendering-61-runtime" `
  --target WebKitRuntimeSmokeHost WebKitRuntimeSmoke --parallel 8
```

- **49/49** host checks: real WARP CPU allocation/upload/copy/Present/resize, format/matrix, observed panel QI/reference ownership, failed CPU/GPU attachment/ detach, exclusive switches, Pending acknowledgement/cookie, copied table, two-page routing, nine actual worker mutations and independent metrics oracles.
- Both ARM MSVC and native clang compile the extracted sources at `/W4 /WX` without `/ZW`. Native ARM clang uses the production no-exception ABI; enabling `/EHsc` in the initial test command exposed its unsupported cleanupret backend.
- Authored ARM DLL/WinMD links and production architecture/metadata/source/export gates pass. External factory/WinMD consumer passes **20/20** plus all four expected negative compilation modes.
- Fresh native MiniBrowser/prototype and RuntimeSmoke targets link with eight jobs. Their historical generated SDK code retains existing macro/extern warnings; no warning-free claim is made for those prototype inputs.
- Frozen legacy ARM signatures/exports and unchanged diagnostic application sources pass. Source-contract and documentation-audit regressions pass **4/4** each; new authored declaration audit has **zero candidates**.

## Phone regression

Lumia 950 XL, `mc-001173b46c8b`, `MobileCoreProd`, OS `15254.603.armfre.feature2_rs3svc.200106-1633`, portal `https://192.168.1.52`. Staging inspected the ARMNT/import closure; SDK 19041 MakeAppx validated both packages. Existing certificate `18FD65CF5E103CF96F04C2A451DC49C94788A40D` signed them. Each installation completed Code=0/Success=true before launch HTTP 200, followed by one visible 30-second poll.

### CPU/CoreWindow

Package `WebKitProbe.EngineRuntime_1.0.0.2_arm__2932e6rj4m41p`, App ID `WebKitProbe.EngineRuntime_2932e6rj4m41p!App`:

```text
runtime-smoke-failure-mask: 0x00000000
javascript-evaluation:    0x00000000
page-bundled-fonts-ready: 0x00000000
page-html-parsed:         0x00000000
page-layout-complete:     0x00000000
page-paint-complete:      0x00000000
page-render-result:       0x00000000
page-d3d11-initialize:    0x00000000
page-d3d11-present:       0x00000000
```

### Skia/ANGLE baseline

Package `WebKitProbe.EngineBrowser_1.0.0.34_arm__2932e6rj4m41p`, App ID `WebKitProbe.EngineBrowser_2932e6rj4m41p!App`, contains the freshly rebuilt native prototype/wrapper/presenter plus the accepted v33 engine. Fixture servers use `192.168.1.69:8002`, `:8003`, HTTPS `:8004`.

Initial 216329-byte log establishes:

- `gpu-paint-backend-skia-ganesh: 1`, Adreno 430 ANGLE/D3D11 renderer.
- `control-compositor-initialize: 0`, root attachment and composited scrolling.
- Actual GPU-paint/submit samples, `cpu-paint=0`, manual `upload-us=0`.
- Extracted `frame-profile n=64` reporting retains its microsecond/milli-Hertz units.
- No CPU fallback or control frame/render failure marker in that sample.
- Retained `fixture-suite-completed: 1`; this marker is a regression observation, not a replacement fail-closed fixture-assertion acceptance claim.

Owner confirmed about:demo rendering, JavaScript counter and scrolling repaint. Owner also reported an override-navigation-failed banner and rotation clipping/ scaling. Their subsequent correction is explicit: **horizontal rendering is still incorrect**. No landscape/resize visual acceptance is inferred from this run.

The 274950-byte interaction log records repeated successful resize return codes between CSS **411x663** / backing **1439x2321** and CSS **731x343** / backing **2559x1201**, with scale **3.5**. These host extent reports do not prove the engine EGL/native surface, compositor clip or cached layer geometry matches. Task **6.3 remains unchecked** and must compare those actual surface/layer extents through rotation and verify the resulting picture. The override banner belongs to the preserved diagnostic BrowserPolicy/template work in 8.3/9.7.

The two pre-engine `native-fault-code: 0xe06d7363` entries persist from the prior baseline; their root cause is not established. App execution/rendering continues.

## Payload and report identities

SHA256:

| Payload/report | SHA256 |
|---|---|
| Runtime signed APPX | `1B73DBAA85227F43721294E2BF95199CCE7A9F9B855E0F663B0F41502C9C6A49` |
| Browser signed APPX | `24C81914AA1CCDB54D8C5F1CEBCB4486D1C106C646A3994766B1D1148D52AC8A` |
| Runtime log, 1007 bytes | `EEF7F579504832622CA2DEAA1D47AC36E2D455CCC6E8A2625DEFE66936D4DE9E` |
| Initial browser log, 216329 bytes | `DE5EF8E6DD55B6E6425CA0BF3891D6F98E82460880A8E6082C29144B5B3680FC` |
| Interaction log, 274950 bytes | `66741AAC3B09557B892A10C815EB07919AB336E76A0A03FDB235C9790CF1DE36` |
| Native 49-check result | `5CB95BF1C195D2AE2076B700BFAEEC5BD16507B630BF1454222C0DD9D5FFB4FC` |
| Rebuilt legacy WebKitWebView.dll | `54338F48498CF5914A62DE92C3C9A4F333EAC4ADB48A6A98BA58B25CC12858B7` |
| Matching legacy WebKitWebView.winmd | `4D701FBE4EA0D6BCA78DC494D1D36AB2E51DD15DC07303F0FB8E039998263410` |
| Accepted unchanged engine | `A676C97C262DF6B713AE548C37BB509582A10D4EC7DB90110A9A23CC5CD22C1D` |
| Authored production WebKit.dll | `A5A9064032DF88DAB1218F9B0C2742699D75073D3BE2D8EEA74C19E6678D5600` |
| Authored production WebKit.winmd | `9781BB7FAD61100567E69A6673BB904ACA5EBB807AF334A25A3B5A6DA6F8B9BC` |

Files remain under `$env:TEMP/opencode/` with the `m10-rendering-61-` prefix: `runtime-v2-result.txt`, `mini-v34-result.log`, `mini-v34-interaction.log`, signed APPXs, `tests-v4/rendering-results.txt`, fresh build roots and staging identity JSONs.

## Gate scope

This accepts native presenter/metrics extraction, tested COM/mode ownership, CPU phone presentation and retained Skia rendering/counter/scrolling baseline. GPU slot behavior in the native ownership matrix is a controlled provider; the phone uses the retained legacy engine path. Production negotiated forwarding, new-control visual ownership, shared scheduling, correct rotation/device-loss and runtime cleanup retain tasks 6.2/6.3/7.5 and later integration gates. Full G4 stays open.
