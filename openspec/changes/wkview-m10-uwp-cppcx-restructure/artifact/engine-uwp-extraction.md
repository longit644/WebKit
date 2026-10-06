# M10 4.1 - EngineUWP extraction and fresh ARM32 build

## Outcome

`WebCore` successfully built and linked in `build-arm-uwp-fresh` with eight jobs,
using MSVC headers/runtime 14.44.35207, native clang-cl 19.1.5 and SDK 19041.
The preserved `build-arm-uwp-skia` tree was read for configuration/inspection only.

- `EngineUWP.h/.cpp` owns the existing startup guard, font setup and platform
  strategies. `WKInitializeEngineUWP` delegates to that implementation.
- Page ticks and dispatcher wakes use `engineServiceUWP`, the single implementation
  of the UWP `RunLoop::cycle` call. Rendering remains per-page; shared tick/wake
  scheduling and deduplication are task 6.2.
- The first-page snapshot renderer and its smoke HTML remain in `FirstPageUWP.cpp`.
- `check-engine-uwp-extraction.py` compares the moved startup body, strategies and
  result enum with committed baseline `5f885ad7cc`, and verifies the smoke renderer
  was preserved. This is source-preservation proof, not new runtime/device evidence.

## Why the build was stuck

1. Copying cached compiler/link flags and RC from the old tree retained SDK 22621
   paths and the absent `msvcurt.lib`. The fresh initial cache now excludes those
   settings; the current toolchain supplies SDK 19041 flags and RC.
2. After the force-included compatibility header changed, ANGLE's PCH was stale.
   Deleting only the `.pch` left its tracked producer `.obj` present. Ninja's `.pch`
   node is a phony dependency on that object, so repeating `WebCore` could not
   regenerate it. Cleaning/rebuilding the producer object fixed the missing file.
3. The PCH producer did not explicitly depend on the compatibility header's
   canonical path. `WEBKIT_ADD_PREFIX_HEADER` now adds that dependency for UWP.
   The generated graph includes it for ANGLE, GLESv2, JSC, PAL and WebCore.
4. ANGLE selected its UWP sources through `angle_is_winuwp`, but lacked the
   `ANGLE_IS_WINUWP` compiler definition. That made `system_utils_win.cpp` enter
   a desktop-only module-handle branch. The UWP CMake configuration now defines it.
5. SDK 19041 hides native loader and working-set declarations under the App
   partition. The compatibility header supplies their real signatures/layouts,
   including pointer-width working-set fields. `SystemInfo.cpp` uses the existing
   wide module lookup on UWP. The restored APIs link against OneCoreUAP; no fake
   success implementation was added.

## Reproducible configure/build

Run from the repository root, using a fresh destination for a clean configuration.
`prepare-arm32-engine-cache.ps1` reads the rollback cache and emits 346 typed
feature/dependency selections, excluding old build-directory references, native
compiler/link flags and RC. This reuses installed ARM dependencies; it does not
claim their clean rebuild or task 10.3 packaging orchestration.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\uwp\build\prepare-arm32-engine-cache.ps1
. .\Tools\uwp\arm32-uwp-env.ps1 -SdkVersion 10.0.19041.0 -VcpkgRoot C:\vcpkg
$toolchain = (Resolve-Path WebKitLibraries\toolchains\Toolchain-ARM32-UWP-clang.cmake).Path
cmake -S . -B build-arm-uwp-fresh -G Ninja -C "$env:TEMP\opencode\m10-fresh-uwp-initial-cache.cmake" "-DCMAKE_TOOLCHAIN_FILE=$toolchain"
$env:CCACHE_DISABLE = '1'
cmake --build build-arm-uwp-fresh --target WebCore --parallel 8
```

For recovery when a PCH side file was manually deleted, clean its producer object
and rebuild that object before resuming. Do not delete just the `.pch` again:

```powershell
ninja -C build-arm-uwp-fresh -t clean Source/ThirdParty/ANGLE/CMakeFiles/ANGLE.dir/cmake_pch.cxx.obj
ninja -C build-arm-uwp-fresh -j 8 Source/ThirdParty/ANGLE/CMakeFiles/ANGLE.dir/cmake_pch.cxx.obj
```

## Verified results

- The extraction's three actual WebCore objects compiled in the fresh target.
- Full eight-job build log `m10-sol-webcore3.log` ended with:
  `[1259/1259] Linking CXX shared library bin\WebCore.dll`.
- A follow-up named-target build returned `ninja: no work to do`; the final
  SystemInfo formatting build also compiled/relinked successfully.
- PE inspection: `IMAGE_FILE_MACHINE_ARMNT`, thumb, AppContainer.
- All 24 legacy UWP exports plus `WKUWPGetEngineAPI` are present in the new DLL.
  The V1 operational slots are still the 3.2 implementation; presence of discovery
  does not establish navigation, callbacks or shutdown behaviour.
- `check-wkuwp-native-compat.ps1`: both ARM32 compilers passed working-set stride,
  header-offset and loader/query function-type assertions at `/W4 /WX`.
- The frozen ABI check passed as C and C++ on both ARM32 compilers, with all four
  objects inspected as ARMNT.
- Source documentation audit reports no findings for this extraction/build repair.
  Existing CX lifetime-probe findings remain outside this slice.

Final DLL SHA-256:

```text
fresh WebCore.dll:    947A530BA0875002852734ED01C588B5770FCC0B5B566FE9E1640CCFD24A79B0
rollback WebCore.dll: 9D9B1C28727712E300CB7509AB3CCDAA04AB3E9AAB0F9360026B57E3C3F6436D
```

No package staging, deployment or phone execution was performed for this slice.
