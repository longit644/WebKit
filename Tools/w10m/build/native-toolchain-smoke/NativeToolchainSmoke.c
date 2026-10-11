// Compile-time checks reject accidental host compilation and exercise SDK includes.
#include <stdint.h>
#include <windows.h>

#ifndef _M_ARM
#error Native UWP smoke must compile for ARM32
#endif

_Static_assert(sizeof(void*) == 4, "ARM32 pointer width required");
_Static_assert(sizeof(uint64_t) == 8, "Native ABI requires a 64-bit integer");

// Preserve a real imported OS symbol so success proves SDK ARM link resolution.
__declspec(dllexport) DWORD WKW10MNativeToolchainSmoke(void)
{
    return GetCurrentThreadId();
}
