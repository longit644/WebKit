// See arm-crtstart.c (kept as documentation/master).
#include <windows.h>
int main(int, char**);
void __stdcall mainCRTStartup(void) { ExitProcess((UINT)main(0, 0)); }
