// See arm-crtstart.c (kept as documentation/master).
#include <windows.h>
int __stdcall wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int);
void __stdcall wWinMainCRTStartup(void)
{
    ExitProcess((UINT)wWinMain(GetModuleHandleW(0), 0, GetCommandLineW(), SW_SHOWDEFAULT));
}
