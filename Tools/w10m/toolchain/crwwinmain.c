// See arm-crtstart.c (kept as documentation/master).
#include <windows.h>

enum { kNoPreviousInstance = 0 };

// User entry point supplied by the executable.
int __stdcall wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int);

// Wide GUI startup wrapper using the current module and command line.
void __stdcall wWinMainCRTStartup(void)
{
    ExitProcess((UINT)wWinMain(GetModuleHandleW(NULL), kNoPreviousInstance, GetCommandLineW(), SW_SHOWDEFAULT));
}
