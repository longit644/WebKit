// See arm-crtstart.c (kept as documentation/master).
#include <windows.h>

enum { kNoPreviousInstance = 0 };

// User entry point supplied by the executable.
int __stdcall WinMain(HINSTANCE, HINSTANCE, LPSTR, int);

// Narrow GUI startup wrapper using the current module and command line.
void __stdcall WinMainCRTStartup(void)
{
    ExitProcess((UINT)WinMain(GetModuleHandleW(NULL), kNoPreviousInstance, GetCommandLineA(), SW_SHOWDEFAULT));
}
