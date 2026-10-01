# Tasks — wkview-m7-uwp-fonts

- [x] Agent: USE(FREETYPE)=ON + fontconfig/freetype find-packages for WIN-UWP
- [x] Agent: FontPlatformData.h UWP branch (FcPattern + cairo face)
- [x] Agent: select existing FontCacheFreeType + FontCustomPlatformDataFreeType implementations instead of WinCairo GDI implementations
- [x] Agent: ComplexTextController UWP shaping path (HarfBuzz)
- [ ] Agent: GDI header stubs (GDIObject/SharedGDIObject/FontMemoryResource)
- [ ] Agent: remaining win font files assess + guard
- [ ] Agent: WebCore links; text renders (DejaVu bundled)
  - [x] Compile and link ARM32 WebCore.dll with the FreeType/fontconfig/HarfBuzz stack
  - [ ] Bundle font/config resources and verify text rendering on the device
- [ ] Agent: WinRT clipboard bridge (DataPackage, copy/paste; drag-drop stays XAML-mediated)
  - [x] Compile WinRT transport for ARM32: clear, writeText via DataPackage, asynchronous readText via GetTextAsync
  - [x] Compile engine-facing UI-dispatch bridge and Pasteboard text copy/clear methods for ARM32
  - [x] Compile asynchronous Pasteboard text-snapshot preparation and plain-text fragment reading for ARM32
  - [ ] Install the XAML dispatcher and invoke asynchronous paste preparation from the application/control
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
