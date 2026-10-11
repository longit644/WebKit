# M10 7.3: UWP-conditioned OS interaction review

## Scope and method

Every UWP-conditioned OS interaction inventoried in `migration-map.md` ("UWP-conditioned sources outside the three main directories") was re-read at its current site, plus full-tree sweeps for both spellings (`PLATFORM(UWP)`, `WTF_PLATFORM_UWP`), for `OS(UWP)` (no hits anywhere) and for `WINAPI_FAMILY`/`WINAPI_PARTITION_APP` in engine directories (vendored mimalloc/dav1d only, retained as-is). `Source/WebKit/UIProcess/API/uwp/` has zero `PLATFORM(UWP)` sites: the new boundary code conditions on the AppContainer family at the toolchain level instead. Each site below is either migrated to a CX service or retained as required native ABI with its rationale. No source was modified by this review.

Projection-token sweep (`Windows::`, `ABI::Windows`, `Microsoft::WRL`, `HSTRING`, `RoActivateInstance`, `CoreDispatcher`, `IAsyncOperation`, `HStringReference`, `wrl/client`, `wrl/event`) over `platform/win`, `platform/graphics`, `platform/network/win`, `accessibility`, `page`, `RenderLayerBacking/Compositor`, `DragImage.h`, `Pasteboard.h`, `MainThreadSharedTimer.h` and `wtf/`: **zero hits**. Outside `platform/uwp/`, no file touches a WinRT/CX projection.

## Migrated to CX service

- Clipboard control path: new host service, retained-token router and WinRT transport (7.1/7.2), proven by the v44 device round trip. The engine `PasteboardWin` text path below stays as the engine-internal consumer.
- Owner-thread scheduling: engine timers stay on WTF's generic loop, but all app-visible service/presentation scheduling runs through the CX `FrameSchedulerDriverUWP` owner timer and wake (6.2).
- Input events: gestures, scroll physics and capture originate in the CX `InputAdapterUWP`/`ScrollControllerUWP` layer (6.4); engine `GetKeyState` / Alt / layout-map queries are v0 stubs whose comments assign modifiers and text input to the XAML layer.
- Font assets: engine uses FreeType/fontconfig with DejaVu last-resort (`FontCacheSkia.cpp`); the faces themselves are packaging-owned, staged by the probe build (5.2 package-default fonts).

## Retained as required native ABI

WTF core: `PlatformUse.h` generic event-loop selection (upstream mechanism, no projection); `WindowsExtras.h` fail-closed registry/HWND stubs; `StackTrace.h` addresses-only (no DbgHelp); `RandomDevice.cpp` CNG `BCryptGenRandom` (store-legal OS crypto); `CurrentTime.cpp` winmm skip (absence of a call).

WTF win: `DbgHelperWin` inline false-stub; `FileSystemWin.cpp` temp-dir backing store (WTF must stay projection-free; engine sessions are ephemeral per 4.7, so no persistent backing is required — the in-code ApplicationData remark is a known limitation, not a 7.3 action); `GDIObject.h` no-op deletes (no GDI handles can exist); `ThreadingWin.cpp` unnamed threads (no SEH naming on ARM32); `SignalsWin.cpp` inert vectored handling; `MemoryPressureHandlerWin.cpp` timer-only coarse pressure (no `CreateMemoryResourceNotification`).

Allocators/VM: `VMAllocate.h` `VirtualQuery`/`VirtualFree`/`VirtualAlloc` with a 32-bit `SIZE_T` template pin; mimalloc `prim.c` ordinary pages (AppContainer cannot grant `SeLockMemoryPrivilege`); `simde/arm/neon.h` `__isb` barrier selection. The 1.3 "decision needed" on the SIMDE header is decided here: it is a CPU-intrinsic selection, not an OS interaction — retained, correctly conditioned, no migration possible. JSC `ExecutableAllocator.cpp` `getCurrentProcessID()` in a log line only.

PAL: `SoundWin.cpp` silent `systemBeep` (the engine must not produce UI; a WinRT dialog beep would be app UX, not engine behavior).

Engine clipboard/pasteboard: `platform/uwp/ClipboardUWP.cpp` (WRL DataPackage transport with RunLoop delivery and `setUIDispatcher` hop), `PasteboardWin.cpp` text path (`m_uwpText`, `readTextAsync`, `clearAsync`), `WCDataObject` in-process COM-memory utilities, local format-key registry. Retained: engine-internal synchronous machinery, headers projection-free, no CX app objects cross it; replacing it with a host-service round trip would add boundary hops to every pasteboard op for no boundary gain. The v44 suite proves the host round trip; engine text-path device assertions are not claimed.

Engine dispatcher hops: `PageUWP.cpp` WRL `CoreDispatcher` wake posts and `TextureMapperCompositorUWP.cpp` WRL `PropertySet` activation. Retained with the same rationale; queued-task lifetime after owner loss stays 7.5.

Desktop-API stubs (USER32/shell/OLE/GDI absent, fail-closed or constant): `CursorWin`, `DragDataWin`, `GDIUtilities` (DPI 1.0, double-click 500), `HWndDC` null DC, `KeyEventWin`, `PlatformMouseEventWin` (crackers spelled out, client-relative coords), `PlatformScreenWin` fixed 950 XL rect, `SharedMemoryWin` `MapViewOfFile`, `SystemInfo.cpp` wide loader spelling (unaccounted in 1.3, accounted here), `WheelEventWin` (crackers, defaults 1/3), `WindowMessageBroadcaster` opaque `WNDPROC`, `WindowsKeyNames` empty layout map, `DragControllerWin` never-copies, `AXObjectCacheWin` dropped MSAA notify (UIA is future work), `NetworkStateNotifierWin` startup-sampled online state, `EventHandler.cpp` desktop drag path excluded, `RenderLayerBacking/Compositor` forced-compositing tile/positioning rules, `MainThreadSharedTimer` RunLoop timer member, `DragImageUWP.cpp` whole-file no-op drag images, font `Font.h`/`FontCache.h`/ `FontCustomPlatformData.h`/`FontPlatformData.h` GDI/DWrite paths excluded, `FontMemoryResource.h`/`OpenTypeUtilities.cpp` registration-free FreeType faces, `SystemFontDatabaseWin.cpp` generic families, cairo/GDI/DIB/Icon files null-or-image-surface fallbacks, `BitmapTexture` Skia GPU context and frame stats (6.1 renderer path), `GraphicsContextCairo.h` deleted HDC constructor.

Third-party/build: ANGLE `ProgramExecutable.cpp` ARM32 clang vcall-thunk workaround (keep patched, not renamed); Skia/ANGLE `CMakeLists.txt`, `PlatformWin.cmake` and the `WK_UWP` CMake consumers are build configuration with no OS interaction — retained unchanged.

## Checks and commands

On 2026-10-10, PowerShell 7.6.6, MSVC 14.44.35207, SDK 10.0.19041.0, native ARM clang-cl 19.1.5, Lumia 950 XL portal `https://192.168.1.52`:

```powershell
cmake --build build-arm-uwp-fresh --target WebCore JavaScriptCore --parallel 8
python Tools/w10m/build/check-w10m-engine-purity.py
.\Tools\w10m\build\check-wkw10m-abi-layout.ps1 -OutDir "$env:TEMP\opencode\m10-73-abi"
```

- Fresh-tree `WebCore` + `JavaScriptCore` build: `ninja: no work to do` — every retained path above stands compiled in its reviewed state.
- Built `build-arm-uwp-fresh/bin/WebCore.dll`: `COFF-ARM`, `IMAGE_FILE_MACHINE_ARMNT`, `APPCONTAINER`, 24 legacy exports plus `WKUWPGetEngineAPI`.
- Engine purity gate: **PASS** — no C++/CX or WinRT projection dependency in engine headers, re-verified after the review.
- ABI layout gate: four ARM objects (C and C++ on both boundary compilers) carry every frozen size/offset/value assertion.
- UIProcess-side proof is the v44 diagnostic component build from 7.2, which compiled the service, router, dispatcher and transport with metadata, contract and export gates passing.

## Manifest deltas applied with this review

- `SystemInfo.cpp` (wide loader spelling) added to the `platform/win` inventory; it was present in-tree but unlisted.
- The SIMDE `neon.h` "decision needed" is resolved as retain (above).
- `DragImageUWP.cpp`, `ClipboardUWP.cpp`, `MainThreadSharedTimer.h` and the `PLATFORM(WIN) && !PLATFORM(UWP)` exclusion family are now explicitly listed as reviewed-retained.

## Remaining gates

This review claims classification and build proof only. Engine pasteboard device assertions, UIA accessibility, real screen-metric wiring (6.3), `postWake`/quiescence/shutdown (7.5) and public capability reporting (7.4) retain their own gates. No migration work is left open by this review: every projection use is either behind a shipped CX service or explicitly retained above.
