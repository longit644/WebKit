// Minimal native CRT startup for ARM32 AppContainer (WebKitView port).
// MSVC 14.4x ships no ARM32 vcruntime140_app: msvcurt.lib only carries
// C++/CX managed entries (?mainCRTStartup@@...), no undecorated ones.
// These call the user mains with empty args (configure tests and simple
// tools don't parse argv) and exit via kernel32 ApiSet (in OneCoreUAP).
// Build: clang-cl --target=thumbv7-unknown-windows-msvc -c arm-crtstart.c
//        llvm-lib /OUT:arm-crtstart.lib arm-crtstart.obj
#include <windows.h>

int main(int, char**);
int wmain(int, wchar_t**);
int __stdcall WinMain(HINSTANCE, HINSTANCE, LPSTR, int);
int __stdcall wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int);

void __stdcall mainCRTStartup(void) { ExitProcess((UINT)main(0, 0)); }
void __stdcall wmainCRTStartup(void) { ExitProcess((UINT)wmain(0, 0)); }
void __stdcall WinMainCRTStartup(void)
{
    ExitProcess((UINT)WinMain(GetModuleHandleW(0), 0, GetCommandLineA(), SW_SHOWDEFAULT));
}
void __stdcall wWinMainCRTStartup(void)
{
    ExitProcess((UINT)wWinMain(GetModuleHandleW(0), 0, GetCommandLineW(), SW_SHOWDEFAULT));
}
