# ARM-UWP runtime smoke app

This native WinRT diagnostic app loads `WebKitRuntimeSmoke.dll`, checks the custom ARM CRT, loads JavaScriptCore, evaluates a loop whose result must be 4950, and loads WebCore. It then renders a local HTML/CSS/inline-SVG page with bundled DejaVu fonts and presents Cairo's BGRA image using a hardware D3D11 CoreWindow swapchain. Diagnostics are written to `LocalState/runtime-smoke.txt` and the debugger. Run it on the Lumia 950 XL; compiling or packaging it on the x64 build machine does not execute the tests.

## Build and package

From the repository root:

```powershell
cmake -S Tools/uwp/RuntimeSmoke -B build-arm-runtime-smoke -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=C:/Users/Longi/Workspace/WebKit/WebKitLibraries/toolchains/Toolchain-ARM32-UWP-clang.cmake
cmake --build build-arm-runtime-smoke
python Tools/uwp/RuntimeSmoke/stage-app.py --build build-arm-runtime-smoke --engine build-arm-uwp/bin --prefix C:/vcpkg/installed/arm-uwp-webkit --stage build-arm-runtime-smoke/package --reader "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/Llvm/x64/bin/llvm-readobj.exe"
& "C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64/makeappx.exe" pack /d build-arm-runtime-smoke/package /p build-arm-runtime-smoke/WebKitRuntimeSmoke.appx /o
```

The package identity uses `CN=WebKitWebView`. Sign the APPX with a development certificate with that subject, and install its certificate plus the ARM Microsoft.VCLibs.140.00 framework on the phone as required by the deployment method. No certificate is created or installed by these scripts. OS/framework DLL resolution and device API availability remain part of the test.

The stage script collects the local dependency closure without copying host system DLLs. Use `--override` to stage rebuilt dependency DLLs ahead of the installed prefix. The verified first-page run uses updated FreeType, fontconfig, Cairo, HarfBuzz/ICU, and pixman builds; other installed dependencies still predate the corrected runtime. The isolated probe DLL links the current runtime without `/FORCE:MULTIPLE`.

## Failure mask

`runtime-smoke-failure-mask: 0x00000000` is the required result. Other bits indicate:

| Bit | Meaning |
| --- | --- |
| 1 | Global C++ initializer did not run exactly once |
| 2 | Initial TLS object or POD value incorrect |
| 4 | Function-static object value incorrect |
| 8 | TLS values leaked between threads |
| 16 | Division/remainder or float conversion incorrect |
| 32 | Event/thread creation failed |
| 64 | TLS construction/destruction count incorrect |
| 128 | Contended function-static constructor ran an incorrect number of times |

Probe workers are joined before the result is reported, so TLS destructor counts include native thread teardown. An initialization deadlock is diagnosed through a stalled log/debugger rather than reported as success. First-page bring-up runs synchronously on the owning UI/engine thread and keeps that thread alive for the CoreWindow lifetime. This is a single static snapshot, not the persistent browser/event-loop implementation. The diagnostic app requests that the display stay awake while it is active.

## Verified device run

Version 1.0.0.2 was signed, installed, and launched on Lumia 950 XL / Windows 10 Mobile 15254.603 on 2026-10-01. Retrieved diagnostics report a zero runtime failure mask, successful JavaScript evaluation, and successful WebCore loading. Evidence is recorded in `openspec/changes/wkview-m7-uwp-fonts/runtime-smoke-lumia950xl.txt`. This is a runtime smoke result; page rendering is not tested.

`Tools/uwp/device-portal.py` supports package upload, deployment status, app launch, and log retrieval through Device Portal. For example:

```powershell
python Tools/uwp/device-portal.py --url https://192.168.1.52 --insecure get /api/app/packagemanager/state
python Tools/uwp/device-portal.py --url https://192.168.1.52 --insecure launch 'WebKitWebView.RuntimeSmoke_2932e6rj4m41p!App' 'WebKitWebView.RuntimeSmoke_1.0.0.2_arm__2932e6rj4m41p'
python Tools/uwp/device-portal.py --url https://192.168.1.52 --insecure get '/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=WebKitWebView.RuntimeSmoke_1.0.0.2_arm__2932e6rj4m41p&path=%5CLocalState&filename=runtime-smoke.txt'
```

`--insecure` accepts the device's self-signed HTTPS certificate for that connection. Increase the manifest version whenever deploying changed contents; the phone rejects a different package with the same identity/version.

## First visible page

Version 1.0.0.10 passed the runtime regression, HTML parsing, layout, Cairo painting, D3D11 initialization and Present on Lumia 950 XL / 15254.603. The owner confirmed that the page, text, card, gradient, and blue/green shapes are visible. Evidence is recorded in `openspec/changes/wkview-m7-uwp-fonts/first-page-lumia950xl.txt`.

Stage the verified updated font/painting dependencies with:

```powershell
python Tools/uwp/RuntimeSmoke/stage-app.py --build build-arm-runtime-smoke --engine build-arm-uwp/bin --prefix C:/vcpkg/installed/arm-uwp-webkit --stage build-arm-runtime-smoke/package --override C:/vcpkg/buildtrees/freetype/arm-uwp-webkit-rel --override C:/vcpkg/buildtrees/fontconfig/arm-uwp-webkit-rel --override C:/vcpkg/buildtrees/cairo/arm-uwp-webkit-rel/src --override C:/vcpkg/buildtrees/harfbuzz/arm-uwp-webkit-rel/src --override C:/vcpkg/buildtrees/pixman/arm-uwp-webkit-rel/pixman --reader "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/Llvm/x64/bin/llvm-readobj.exe"
```

`WebKitWebViewPresenterUWP` also provides a composition/SwapChainPanel entry point. It compiles in this target, but this device run uses CoreWindow. The final reusable XAML control, real resource loading, scrolling/input, resizing, device-loss recovery, and continuous rendering remain future integration work.
