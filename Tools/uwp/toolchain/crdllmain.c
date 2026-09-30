// WebKitView default DllMain (own object so archive lazy-loading skips it
// when the target defines its own, e.g. OpenSSL dllmain.c). Mirrors the MSVC
// CRT default: nothing but TRUE.
#include <windows.h>
BOOL __stdcall DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    (void)hinstDLL; (void)fdwReason; (void)lpvReserved;
    return TRUE;
}
