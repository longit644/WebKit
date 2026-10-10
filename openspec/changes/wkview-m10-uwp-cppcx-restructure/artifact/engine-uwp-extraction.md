# M10 4.1 - EngineUWP extraction and fresh ARM32 build

## Outcome

`WebCore` successfully built and linked in `build-arm-uwp-fresh` with eight jobs, using MSVC headers/runtime 14.44.35207, native clang-cl 19.1.5 and SDK 19041. The preserved `build-arm-uwp-skia` tree was read for configuration/inspection only.

- `EngineUWP.h/.cpp` owns the existing startup guard, font setup and platform strategies. `WKInitializeEngineUWP` delegates to that implementation.
- Page ticks and dispatcher wakes use `engineServiceUWP`, the single implementation of the UWP `RunLoop::cycle` call. Rendering remains per-page; shared tick/wake scheduling and deduplication are task 6.2.
- The first-page snapshot renderer and its smoke HTML remain in `FirstPageUWP.cpp`.
- `check-engine-uwp-extraction.py` compares the moved startup body, strategies and result enum with committed baseline `5f885ad7cc`, and verifies the smoke renderer was preserved. This is source-preservation proof, not new runtime/device evidence.

## Why the build was stuck

1. Copying cached compiler/link flags and RC from the old tree retained SDK 22621 paths and the absent `msvcurt.lib`. The fresh initial cache now excludes those settings; the current toolchain supplies SDK 19041 flags and RC.
2. After the force-included compatibility header changed, ANGLE's PCH was stale. Deleting only the `.pch` left its tracked producer `.obj` present. Ninja's `.pch` node is a phony dependency on that object, so repeating `WebCore` could not regenerate it. Cleaning/rebuilding the producer object fixed the missing file.
3. The PCH producer did not explicitly depend on the compatibility header's canonical path. `WEBKIT_ADD_PREFIX_HEADER` now adds that dependency for UWP. The generated graph includes it for ANGLE, GLESv2, JSC, PAL and WebCore.
4. ANGLE selected its UWP sources through `angle_is_winuwp`, but lacked the `ANGLE_IS_WINUWP` compiler definition. That made `system_utils_win.cpp` enter a desktop-only module-handle branch. The UWP CMake configuration now defines it.
5. SDK 19041 hides native loader and working-set declarations under the App partition. The compatibility header supplies their real signatures/layouts, including pointer-width working-set fields. `SystemInfo.cpp` uses the existing wide module lookup on UWP. The restored APIs link against OneCoreUAP; no fake success implementation was added.

## Reproducible configure/build

Run from the repository root, using a fresh destination for a clean configuration. `prepare-arm32-engine-cache.ps1` reads the rollback cache and emits 346 typed feature/dependency selections, excluding old build-directory references, native compiler/link flags and RC. This reuses installed ARM dependencies; it does not claim their clean rebuild or task 10.3 packaging orchestration.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\uwp\build\prepare-arm32-engine-cache.ps1
. .\Tools\uwp\arm32-uwp-env.ps1 -SdkVersion 10.0.19041.0 -VcpkgRoot C:\vcpkg
$toolchain = (Resolve-Path WebKitLibraries\toolchains\Toolchain-ARM32-UWP-clang.cmake).Path
cmake -S . -B build-arm-uwp-fresh -G Ninja -C "$env:TEMP\opencode\m10-fresh-uwp-initial-cache.cmake" "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
$env:CCACHE_DISABLE = '1'
cmake --build build-arm-uwp-fresh --target WebCore --parallel 8
```

For recovery when a PCH side file was manually deleted, clean its producer object and rebuild that object before resuming. Do not delete just the `.pch` again:

```powershell
ninja -C build-arm-uwp-fresh -t clean Source/ThirdParty/ANGLE/CMakeFiles/ANGLE.dir/cmake_pch.cxx.obj
ninja -C build-arm-uwp-fresh -j 8 Source/ThirdParty/ANGLE/CMakeFiles/ANGLE.dir/cmake_pch.cxx.obj
```

## Verified results

- The extraction's three actual WebCore objects compiled in the fresh target.
- Full eight-job build log `m10-sol-webcore3.log` ended with: `[1259/1259] Linking CXX shared library bin\WebCore.dll`.
- A follow-up named-target build returned `ninja: no work to do`; the final SystemInfo formatting build also compiled/relinked successfully.
- PE inspection: `IMAGE_FILE_MACHINE_ARMNT`, thumb, AppContainer.
- All 24 legacy UWP exports plus `WKUWPGetEngineAPI` are present in the new DLL. The V1 operational slots are still the 3.2 implementation; presence of discovery does not establish navigation, callbacks or shutdown behaviour.
- `check-wkuwp-native-compat.ps1`: both ARM32 compilers passed working-set stride, header-offset and loader/query function-type assertions at `/W4 /WX`.
- The frozen ABI check passed as C and C++ on both ARM32 compilers, with all four objects inspected as ARMNT.
- Source documentation audit reports no findings for this extraction/build repair. Existing CX lifetime-probe findings remain outside this slice.

Final DLL SHA-256:

```text
fresh WebCore.dll:    947A530BA0875002852734ED01C588B5770FCC0B5B566FE9E1640CCFD24A79B0
rollback WebCore.dll: 9D9B1C28727712E300CB7509AB3CCDAA04AB3E9AAB0F9360026B57E3C3F6436D
```

## Focused phone regression

Device: Lumia 950 XL, Windows Mobile `15254.603.armfre.feature2_rs3svc.200106-1633`.

Side-by-side packages used the fresh `build-arm-uwp-fresh` engine DLLs:

- `WebKitProbe.EngineRuntime 1.0.0.1`
  - Appx SHA-256: `72819941D25C3B618B30804C0A7859E1FAF72E77FD6B3EF99DA64CC7B19A9553`
  - Staged `WebCore.dll` SHA-256: `947A530BA0875002852734ED01C588B5770FCC0B5B566FE9E1640CCFD24A79B0`
- `WebKitProbe.EngineBrowser 1.0.0.1`
  - Appx SHA-256: `BEF760E93470926FE282033A00643F70265319C3D12E8CA3F79F89F2E7648661`
  - Staged `WebCore.dll` SHA-256: `947A530BA0875002852734ED01C588B5770FCC0B5B566FE9E1640CCFD24A79B0`

Both Appx packages were packed with SDK 19041 MakeAppx and signed with the existing `CN=WebKitWebView` development certificate. Fixture servers ran on `192.168.1.69:8002` and `:8003`.

RuntimeSmoke passed:

- `runtime-smoke-failure-mask: 0x00000000`
- `javascript-evaluation: 0x00000000`
- `load-webcore: 0x00000000`
- Font install, HTML parsing, layout, paint and D3D presentation all returned `0`.
- The owner confirmed the visible snapshot was correct. The demo copy still contains the stale word “Cairo”; the packaged engine was the Skia fresh-tree build, so only the wording is stale.

MiniBrowser passed the retained fixture suite after both fixture servers ran:

- `gpu-paint-backend-skia-ganesh: 0x00000001`
- `control-compositor-initialize: 0x00000000`
- `gpu-document-composited-scrolling: 0x00000001`
- `fixture-suite-cors-allowed: 0x00000001`
- `fixture-suite-cors-blocked: 0x00000001`
- `fixture-suite-completed: 0x00000001`
- HTTPS navigation to `https://example.com/` committed and completed.
- Owner-confirmed manual checks: HTTPS page loads, `about:demo` JavaScript counter increments, and long-page scrolling repaints.
- The retrieved `page.png` is 325,194 bytes and `1439x2321`, matching the portrait backing size, so it does not establish the sideways landscape presentation.

Open regression:

- Rotation reports succeed in the log across five viewport reports, alternating `411x663` and `731x343` with `control-resize: 0` and continued GPU frames, but the owner-supplied photos show the displayed image lagging the resize: portrait is clipped on the right edge until a scroll corrects it, and landscape is blank before scrolling and still clipped after scrolling. The retrieved `page.png` is `1439x2321`, matching the portrait backing size, so it does not establish the landscape presentation. Rotation/resize recovery remains open for task 6.3.
- The MiniBrowser log also contains `native-fault-code: 0xe06d7363` entries. The app continued and completed its suite; no crash dump was produced and no root cause is established.
- Device Portal connectivity was intermittent. After the successful runs it timed out again, so no further device reads were possible in this slice.

No full MiniBrowser interaction matrix, suspend/resume coverage or phone-side performance acceptance is claimed.
