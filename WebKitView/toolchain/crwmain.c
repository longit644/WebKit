// See arm-crtstart.c (kept as documentation/master).
#include <windows.h>
int wmain(int, wchar_t**);
void __stdcall wmainCRTStartup(void) { ExitProcess((UINT)wmain(0, 0)); }
