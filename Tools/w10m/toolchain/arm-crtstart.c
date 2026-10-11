// Minimal native CRT startup for ARM32 AppContainer (WebKitWebView port).
// MSVC 14.4x ships no ARM32 vcruntime140_app: msvcurt.lib only carries
// C++/CX managed entries (?mainCRTStartup@@...), no undecorated ones.
// These call the user mains with empty args (configure tests and simple
// tools don't parse argv) and exit via kernel32 ApiSet (in OneCoreUAP).
// Build: clang-cl --target=armv7-unknown-windows-msvc -c arm-crtstart.c
//        llvm-lib /OUT:arm-crtstart.lib arm-crtstart.obj
#include <windows.h>

// AppContainer startup invokes user entry points with an empty argument vector.
enum { kNoCommandLineArguments = 0 };

// User entry points linked from the application binary.
// Narrow console main supplied by the target executable.
int main(int, char**);

// Wide console main supplied by the target executable.
int wmain(int, wchar_t**);

// Narrow GUI entry point supplied by the target executable.
int __stdcall WinMain(HINSTANCE, HINSTANCE, LPSTR, int);

// Wide GUI entry point supplied by the target executable.
int __stdcall wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int);

// CRT startup wrapper for a console-style narrow main.
void __stdcall mainCRTStartup(void) { ExitProcess((UINT)main(kNoCommandLineArguments, NULL)); }

// CRT startup wrapper for a console-style wide main.
void __stdcall wmainCRTStartup(void) { ExitProcess((UINT)wmain(kNoCommandLineArguments, NULL)); }

// CRT startup wrapper for a GUI-style narrow WinMain.
void __stdcall WinMainCRTStartup(void)
{
    ExitProcess((UINT)WinMain(GetModuleHandleW(NULL), NULL, GetCommandLineA(), SW_SHOWDEFAULT));
}

// CRT startup wrapper for a GUI-style wide wWinMain.
void __stdcall wWinMainCRTStartup(void)
{
    ExitProcess((UINT)wWinMain(GetModuleHandleW(NULL), NULL, GetCommandLineW(), SW_SHOWDEFAULT));
}
