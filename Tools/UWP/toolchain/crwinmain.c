// See arm-crtstart.c (kept as documentation/master).
#include <windows.h>
int __stdcall WinMain(HINSTANCE, HINSTANCE, LPSTR, int);
void __stdcall WinMainCRTStartup(void)
{
    ExitProcess((UINT)WinMain(GetModuleHandleW(0), 0, GetCommandLineA(), SW_SHOWDEFAULT));
}
