# Tasks — wkview-m7-uwp-fonts

- [x] Agent: USE(FREETYPE)=ON + fontconfig/freetype find-packages for WIN-UWP
- [x] Agent: FontPlatformData.h UWP branch (FcPattern + cairo face)
- [x] Agent: select existing FontCacheFreeType + FontCustomPlatformDataFreeType implementations instead of WinCairo GDI implementations
- [x] Agent: ComplexTextController UWP shaping path (HarfBuzz)
- [x] Agent: assess GDI headers and exclude unsupported operations (WTF GDIObject UWP cleanup guard, FontMemoryResource UWP guard; SharedGDIObject is a template wrapper)
- [x] Agent: assess remaining Win font files and select/exclude through the FreeType UWP source list
- [x] Agent: WebCore links; text renders (DejaVu bundled)
  - [x] Compile and link ARM32 WebCore.dll with the FreeType/fontconfig/HarfBuzz stack
  - [x] Bundle DejaVu font resources, register an application-local fontconfig configuration, and verify text rendering on the device
- [ ] Agent: WinRT clipboard bridge (DataPackage, copy/paste; drag-drop stays XAML-mediated)
  - [x] Compile WinRT transport for ARM32: clear, writeText via DataPackage, asynchronous readText via GetTextAsync
  - [x] Compile engine-facing UI-dispatch bridge and Pasteboard text copy/clear methods for ARM32
  - [x] Compile asynchronous Pasteboard text-snapshot preparation and plain-text fragment reading for ARM32
  - [ ] Install the XAML dispatcher and invoke asynchronous paste preparation from the application/control
    - [x] Add the opaque WKInstallClipboardDispatcherUWP bridge and install the control's ICoreDispatcher; ARM WebCore/MiniBrowser builds passed
    - [ ] Invoke asynchronous Pasteboard preparation from a user paste action
    - [x] Verify dispatcher installation on the device (1.0.0.14 startup log, HRESULT S_OK)
    - [ ] Verify asynchronous paste completion on the device
  - [ ] Implement HTML, image, and origin-scoped custom clipboard formats
  - [ ] Verify text copy/paste on the device
- [ ] Agent: commit, push
- [ ] Owner: ack; agent tags `wkview-m7-uwp-fonts`

## Compilation verification

The targeted ARM32 build passed for FontCacheFreeType, FontCustomPlatformDataFreeType, FontPlatformDataFreeType, GlyphPageTreeNodeFreeType, SimpleFontDataFreeType, and ComplexTextControllerHarfBuzz. On 2026-10-01, the resumed `ninja -j 4 WebCore` build in `build-arm-uwp` completed successfully with `[105/105] Linking CXX shared library bin\WebCore.dll`. This verifies compilation and DLL linkage, not runtime font rendering.

ClipboardUtilitiesWin.cpp, WCDataObject.cpp, ClipboardUWP.cpp, and PasteboardWin.cpp pass targeted ARM32 compilation. Clipboard text copy and clear enqueue WinRT operations through an application-supplied UI dispatcher. Read completions are delivered on the originating engine run loop; createForCopyAndPasteAsync prepares a text snapshot for Editor::paste(Pasteboard&). The application/control must install the dispatcher and use this asynchronous entry point. HTML, image, and origin-scoped custom formats remain unfinished. No runtime clipboard or on-device text-rendering verification has occurred.

The former Cairo/GDI compilation blockers are resolved. GraphicsContextWinCairo.cpp and ImageAdapterWinCairo.cpp exclude unsupported GDI adapters under UWP, and DragImageUWP.cpp replaces DragImageWinCairo.cpp with a retained native-image preview and scale/opacity/orientation metadata. CairoUtilities.cpp and GraphicsContextCairo.cpp now exclude the desktop cairo-win32.h path under UWP. The XAML/GPU host must consume drag-preview metadata; unsupported file-icon and link-label previews remain pending.

The build uses x64-host Clang 19.1.5 while retaining the `armv7-unknown-windows-msvc` target, avoiding the i386-host compiler's address-space exhaustion on generated style sources. UWP disables MediaFoundation registration as well as excluding desktop MediaFoundation source files. The RenderText size-check model now mirrors its separate unsigned and FontCascade::CodePath bitfields so MSVC enum-bitfield packing is represented accurately; the size assertion remains enabled.

## Linked artifact inspection

`llvm-readobj --file-headers --coff-imports bin\WebCore.dll` reports COFF-ARM, 32-bit Thumb, `IMAGE_FILE_MACHINE_ARMNT`, and `IMAGE_DLL_CHARACTERISTICS_APPCONTAINER`. Direct imports include cairo-2.dll, fontconfig-1.dll, freetype.dll, harfbuzz.dll, harfbuzz-icu.dll, JavaScriptCore.dll, libEGL.dll, and libGLESv2.dll. No direct USER32.dll, GDI32.dll, SHELL32.dll, or WINMM.dll imports appear. Inspection output is saved locally at `C:\Users\Longi\.local\share\opencode\tool-output\tool_0f6a1098a001CzRpU1MROzD3Gv`.

Runtime readiness is still unverified. The artifact has a zero TLS data-directory RVA/size, matching the outstanding custom CRT/TLS implementation concern. Direct imports still include desktop-oriented APIs such as AdjustTokenPrivileges, LookupPrivilegeValueW, OpenProcessToken, GetConsoleScreenBufferInfo, WriteConsoleA, GetVersionExW, and LoadLibraryW; their origins and AppContainer/device compatibility require review along with transitive DLL dependencies. Successful linkage and the AppContainer header flag do not establish loader legality or correct CRT/STL/TLS behavior. XAML control integration, bundled-font rendering, clipboard runtime verification, and GPU presentation on the device remain pending.

## Subsequent runtime audit

The initial artifact inspection above is historical. The subsequent CRT/TLS audit corrected confirmed compiler-helper ABI defects, implemented native TLS metadata and lifecycle callbacks, and removed mimalloc's UWP privilege-adjustment path. Rebuilt WebCore, JavaScriptCore, and ANGLE DLLs now have populated TLS directories. A native ARM-UWP runtime smoke host and probe DLL build successfully, and MakeAppx produced an unsigned diagnostic package. Installed third-party dependency DLLs still require rebuilding with the corrected embedded runtime, and signing/deployment/device execution remain pending. Detailed evidence and unresolved startup/STL/import issues are recorded in `Tools/uwp/RUNTIME-AUDIT.md` and `Tools/uwp/RuntimeSmoke/README.md`.

The diagnostic package was subsequently signed and version 1.0.0.2 successfully installed and ran on Lumia 950 XL / OS 15254.603. Device Portal retrieved a zero CRT/TLS failure mask, successful JavaScript evaluation, and successful WebCore DLL loading; see `runtime-smoke-lumia950xl.txt`. This supersedes the pending signing/deployment status above. Text/page/GPU rendering remains unverified, so the combined font-rendering milestone is still open.

## First-page bring-up

`platform/uwp/FirstPageUWP.cpp` now builds and links into WebCore. It uses a local Page/LocalFrame, the internal synchronous document writer, bundled DejaVu fonts registered with fontconfig, and Cairo BGRA painting. The diagnostic host includes `WebKitWebViewPresenterUWP`, with compiled D3D11 CoreWindow and composition/SwapChainPanel presentation entry points. The reusable persistent XAML browser control remains pending; the current harness is a one-frame local-page test, with scripting disabled and no navigation/input integration.

Versions 1.0.0.3 and 1.0.0.4 installed and launched on Lumia. Version 1.0.0.4 logs a viewport of 411×731 logical pixels, a zero runtime failure mask, successful JavaScript evaluation and WebCore loading, and `page-render-result: 0x00000003` after `page-main-thread-ready`. This identifies bundled-font registration/configuration as the first runtime blocker. No layout, paint, or GPU present success is claimed. Version 1.0.0.5 is built and signed with native file/FreeType/fontconfig step diagnostics and an app-scoped display request; its deployment is blocked until the phone is reachable again. Logging now closes its append handle after each message so Device Portal can retrieve partial logs.

These intermediate failures are now resolved for the first-page harness. Version 1.0.0.5 exposed FreeType error 0x0A (Array_Too_Large) with native file access working; relinking FreeType with the corrected ARM runtime resolved it. A version 1.0.0.6 dump located an instruction-state fault in fontconfig's locally imported strdup wrapper; explicit __imp_* function pointers in crrt.cpp preserve the Thumb bit and resolved font registration. A subsequent WebCore dump identified an unset PlatformStrategies provider, now installed by the local-only harness. Cairo, HarfBuzz/ICU, and pixman were rebuilt/relinked with the current runtime and staged as dependency overrides. Version 1.0.0.9 completed HTML parsing/layout/painting but failed the UI dispatcher handoff. Version 1.0.0.10 performs the single snapshot on the owning UI/engine thread and presents it directly.

On 2026-10-01, version 1.0.0.10 installed successfully and its device log reports successful font registration, Page/LocalFrame creation, HTML parsing, layout, Cairo painting, D3D11 initialization, and Present. The process remained running and the owner confirmed the page is visible. Evidence is preserved in `first-page-lumia950xl.txt`. This verifies local static first-page/font rendering and GPU presentation through CoreWindow. Persistent XAML WebKitWebViewUWP control integration, composition/SwapChainPanel runtime testing, live navigation, input/scrolling, animated repaint, clipboard hookup, and the broader runtime/dependency audit remain pending.

## Persistent control and MiniBrowser follow-up

`PageUWP.h/.cpp` now provide an opaque persistent-page bridge for lifecycle, local HTML loading, resize, scroll, pointer input, generic run-loop ticks, and Cairo repaint. `WebKitWebViewUWP.h/.cpp` provide a native C++ wrapper exposing a XAML UserControl backed by SwapChainPanel, with event hookup and a dispatcher timer. The D3D11 presenter now reuses its upload texture and supports swapchain buffer resizing. These sources compile and link for ARM; their interactive device behavior is not yet verified.

`Tools/MiniBrowser/uwp/` is now a buildable native XAML shell with Back/Reload/address/Go/status controls and two local demo pages. Its separate package version 1.0.0.0 installed and reached XAML application startup and persistent Page creation, then failed IUserControl content attachment with E_NOINTERFACE. The attachment was corrected; version 1.0.0.1 is built and signed, with deployment pending after a Device Portal timeout. See the shell README for build/package commands and the device checks. Live HTTPS navigation still requires a real loader strategy and browser clients.

Subsequent version 1.0.0.4 verified the persistent XAML composition control on Lumia: the owner confirmed visible rendering, JavaScript tap-counter changes, swipe scrolling, and local Go/Back/Reload. A transparent parent Grid provides hit testing because SwapChainPanel Background is unsupported on this device. Logs show a 411×663 logical viewport and pointer/scroll updates; evidence is preserved in `minibrowser-lumia950xl.txt`. WIC-based page PNG capture is implemented but not yet verified through download.

The owner's photo/feedback identifies soft text and stuttering in the logical-resolution/eager-repaint baseline. Version 1.0.0.5 adds composition-scale-aware Cairo buffers and inverse swapchain transforms, plus scroll coalescing and dirty-only repaint on CompositionTarget.Rendering. It compiles/links; deployment and quality regression checks are pending after another portal timeout. Live navigation, proper browser loader/clients, keyboard/IME, clipboard, inertia/device-loss handling, and WinMD packaging remain open.

## Current M7 acceptance status — 2026-10-02

The earlier pending statements above are chronological bring-up history. Font rendering is verified, including DejaVu regular/bold, device-scale-aware text and the persistent XAML control. `WTF/win/GDIObject.h` guards unsupported cleanup under PLATFORM(UWP), `FontMemoryResource.h` excludes RemoveFontMemResourceEx, and PlatformWin.cmake removes desktop Win/GDI/Uniscribe font implementations in favor of platform/FreeType.cmake. Those source/header assessment tasks are complete.

M7 remains open for clipboard application dispatcher hookup, asynchronous paste use, HTML/image/origin-scoped custom formats and device copy/paste proof. Commit/push and milestone tagging remain unchecked until explicitly requested and owner acceptance is recorded. GPU-backed painting and 60 Hz rendering acceptance are tracked separately in `wkview-m8-uwp-gpu-rendering`; they do not enlarge M7's font/clipboard completion requirements.

The dispatcher hookup is now implemented through `WKInstallClipboardDispatcherUWP`: the control obtains its XAML ICoreDispatcher, the engine validates owner-thread/UI access, and the ClipboardUWP transport retains an enqueue adapter. Completion still returns through the originating engine RunLoop. The adapter and a clean MiniBrowser rebuild passed ARM compilation/linkage with eight parallel jobs. These binaries have not been repackaged or deployed for this clipboard change; device clipboard behavior is not yet verified.
