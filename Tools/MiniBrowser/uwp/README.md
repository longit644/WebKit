# MiniBrowser UWP (STUB — not wired to build)

Test shell for the WebKitView core port on Windows 10 Mobile ARM32 (950 XL).
Modeled on `../win/CMakeLists.txt`. Status: skeleton only, do NOT add to any
parent `add_subdirectory` until the engine (WTF/JSC/WebCore, WK_WEBKITVIEW)
configures for `thumbv7-windows-uwp`.

## Mapping from win/ (desktop, HWND)

| win/ file              | uwp/ plan                                              |
|------------------------|--------------------------------------------------------|
| WinMain.cpp            | App entry (`App.xaml.cpp`, no WinMain in AppContainer) |
| MainWindow.cpp         | CoreWindow + SwapChainPanel host page                  |
| BrowserWindow.cpp      | Page view: Cairo RGBA -> WriteableBitmap (v0.1)        |
| WebKitBrowserWindow.cpp| WK page-load + input forwarding (single process)       |
| Common.cpp/Common2.cpp | Reuse where AppContainer-clean, else stub              |
| InjectedBundle.cpp     | Dropped v0.1 (no child process in UWP)                 |

## Banned in AppContainer (win/ links these — uwp/ must NOT)

`user32`, `comctl32`, `shlwapi`, `GetKeyState`, `BSTR`, HWND-based RunLoop.
Replacements: CoreWindow input, XAML controls, generic WTF RunLoop,
harness-supplied modifiers/text (same approach as Apotheosis).
