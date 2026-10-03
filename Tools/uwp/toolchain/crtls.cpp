// Native ARM32 PE TLS and compiler-generated C++ TLS support.
// Contracts verified with runtime-abi-probe.cpp (Clang 19.1.5 ARM MSVC).
#include <windows.h>

extern "C" {
typedef void (__cdecl *Initializer)(void);
unsigned long _tls_index = 0;
__declspec(thread) unsigned char __tls_guard = 0;

#pragma section(".tls", read, write)
#pragma section(".tls$ZZZ", read, write)
__declspec(allocate(".tls")) char _tls_start = 0;
__declspec(allocate(".tls$ZZZ")) char _tls_end = 0;

#pragma section(".CRT$XLA", read)
#pragma section(".CRT$XLZ", read)
__declspec(allocate(".CRT$XLA")) PIMAGE_TLS_CALLBACK __xl_a = nullptr;
__declspec(allocate(".CRT$XLZ")) PIMAGE_TLS_CALLBACK __xl_z = nullptr;
#pragma section(".rdata$T", read)
extern __declspec(allocate(".rdata$T")) const IMAGE_TLS_DIRECTORY32 _tls_used = {
    reinterpret_cast<ULONG_PTR>(&_tls_start),
    reinterpret_cast<ULONG_PTR>(&_tls_end),
    reinterpret_cast<ULONG_PTR>(&_tls_index),
    reinterpret_cast<ULONG_PTR>(&__xl_a + 1), 0, 0
};
// Some vendored sources use the x86 spelling even on ARM.
#pragma comment(linker, "/include:_tls_used")
#pragma comment(linker, "/alternatename:__tls_used=_tls_used")

#pragma section(".CRT$XDA", read)
#pragma section(".CRT$XDZ", read)
__declspec(allocate(".CRT$XDA")) Initializer __xd_a = nullptr;
__declspec(allocate(".CRT$XDZ")) Initializer __xd_z = nullptr;

void __stdcall __dyn_tls_init(void*, unsigned long reason, void*)
{
    if (reason != DLL_THREAD_ATTACH || __tls_guard)
        return;
    __tls_guard = 1;
    for (Initializer* next = &__xd_a + 1; next < &__xd_z; ++next) {
        if (*next)
            (*next)();
    }
}
void __cdecl __dyn_tls_on_demand_init(void)
{
    __dyn_tls_init(nullptr, DLL_THREAD_ATTACH, nullptr);
}

struct DestructorBlock {
    DestructorBlock* previous;
    unsigned count;
    Initializer callbacks[32];
};
static __declspec(thread) DestructorBlock initialDestructors;
static __declspec(thread) DestructorBlock* destructors;

int __cdecl __tlregdtor(Initializer callback)
{
    if (!destructors)
        destructors = &initialDestructors;
    if (destructors->count == 32) {
        auto block = static_cast<DestructorBlock*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(DestructorBlock)));
        if (!block)
            return -1;
        block->previous = destructors;
        destructors = block;
    }
    destructors->callbacks[destructors->count++] = callback;
    return 0;
}

void __stdcall wk_tls_destroy(void*, unsigned long reason, void*)
{
    if (reason != DLL_THREAD_DETACH && reason != DLL_PROCESS_DETACH)
        return;
    while (destructors) {
        auto block = destructors;
        if (block->count) {
            // Pop before calling: registrations made by a destructor remain
            // visible and completed callbacks cannot execute twice.
            auto callback = block->callbacks[--block->count];
            if (callback)
                callback();
        } else {
            destructors = block->previous;
            if (block != &initialDestructors)
                HeapFree(GetProcessHeap(), 0, block);
        }
    }
}

#pragma section(".CRT$XLC", read)
#pragma section(".CRT$XLD", read)
__declspec(allocate(".CRT$XLC")) PIMAGE_TLS_CALLBACK wk_tls_init_callback = __dyn_tls_init;
__declspec(allocate(".CRT$XLD")) PIMAGE_TLS_CALLBACK wk_tls_dtor_callback = wk_tls_destroy;
#pragma comment(linker, "/include:wk_tls_init_callback")
#pragma comment(linker, "/include:wk_tls_dtor_callback")
}
