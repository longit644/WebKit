// See arm-crtstart.c (kept as documentation/master).
#include <windows.h>

enum { kNoCommandLineArguments = 0 };

// User entry point supplied by the executable.
int main(int, char**);

// Narrow console startup with no command-line arguments.
void __stdcall mainCRTStartup(void) { ExitProcess((UINT)main(kNoCommandLineArguments, NULL)); }
