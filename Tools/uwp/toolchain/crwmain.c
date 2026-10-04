// See arm-crtstart.c (kept as documentation/master).
#include <windows.h>

enum { kNoCommandLineArguments = 0 };

// User entry point supplied by the executable.
int wmain(int, wchar_t**);

// Wide console startup with no command-line arguments.
void __stdcall wmainCRTStartup(void) { ExitProcess((UINT)wmain(kNoCommandLineArguments, NULL)); }
