// Native ARM32 PE TLS and compiler-generated C++ TLS support.
// Contracts verified with runtime-abi-probe.cpp (Clang 19.1.5 ARM MSVC).
#include <windows.h>

extern "C" {

// TLS ABI values and per-thread destructor storage limits.
constexpr unsigned kTlsDestructorCapacity = 32;
constexpr int kTlsRegistrationSuccess = 0;
constexpr int kTlsRegistrationOutOfMemory = -1;
constexpr unsigned char kTlsGuardUninitialized = 0;
constexpr unsigned char kTlsGuardInitialized = 1;
constexpr SIZE_T kTlsBlockHeapZeroFlag = HEAP_ZERO_MEMORY;
constexpr SIZE_T kTlsBlockHeapFreeFlags = 0;
constexpr DWORD kNoTlsZeroFillBytes = 0;
constexpr DWORD kNoTlsDirectoryCharacteristics = 0;

// Compiler TLS initializer callback type recorded in PE TLS callback arrays.
typedef void(__cdecl* Initializer)(void);

// PE loader index for the module's native TLS slots.
unsigned long _tls_index = kTlsGuardUninitialized;

// Per-thread guard prevents rerunning dynamic TLS initializers for every entry.
__declspec(thread) unsigned char __tls_guard = kTlsGuardUninitialized;

#pragma section(".tls", read, write)
#pragma section(".tls$ZZZ", read, write)
__declspec(allocate(".tls")) char _tls_start = kTlsGuardUninitialized;
__declspec(allocate(".tls$ZZZ")) char _tls_end = kTlsGuardUninitialized;

#pragma section(".CRT$XLA", read)
#pragma section(".CRT$XLZ", read)
__declspec(allocate(".CRT$XLA")) PIMAGE_TLS_CALLBACK __xl_a = nullptr;
__declspec(allocate(".CRT$XLZ")) PIMAGE_TLS_CALLBACK __xl_z = nullptr;
#pragma section(".rdata$T", read)
// PE TLS directory consumed by the loader for this module's static TLS block.
extern __declspec(allocate(".rdata$T")) const IMAGE_TLS_DIRECTORY32 _tls_used = {
    reinterpret_cast<ULONG_PTR>(&_tls_start),
    reinterpret_cast<ULONG_PTR>(&_tls_end),
    reinterpret_cast<ULONG_PTR>(&_tls_index),
    reinterpret_cast<ULONG_PTR>(&__xl_a + 1), kNoTlsZeroFillBytes, kNoTlsDirectoryCharacteristics
};

// Some vendored sources use the x86 spelling even on ARM.
#pragma comment(linker, "/include:_tls_used")
#pragma comment(linker, "/alternatename:__tls_used=_tls_used")

#pragma section(".CRT$XDA", read)
#pragma section(".CRT$XDZ", read)
__declspec(allocate(".CRT$XDA")) Initializer __xd_a = nullptr;
__declspec(allocate(".CRT$XDZ")) Initializer __xd_z = nullptr;

// Runs this module's .CRT$XD initializers once for each attached thread.
void __stdcall __dyn_tls_init(void*, unsigned long reason, void*)
{
    if (reason != DLL_THREAD_ATTACH || __tls_guard)
        return;
    __tls_guard = kTlsGuardInitialized;
    for (Initializer* next = &__xd_a + 1; next < &__xd_z; ++next) {
        if (*next)
            (*next)();
    }
}

// Exposes the same TLS initializer path for compiler-generated lazy calls.
void __cdecl __dyn_tls_on_demand_init(void)
{
    __dyn_tls_init(nullptr, DLL_THREAD_ATTACH, nullptr);
}

// Stack of per-thread C++ TLS destructor registrations.
struct DestructorBlock {
    // Previous block when a thread registers more callbacks than one block holds.
    DestructorBlock* previous;

    // Number of live callback entries in this block.
    unsigned count;

    // Callbacks invoked in reverse registration order at thread detach.
    Initializer callbacks[kTlsDestructorCapacity];
};

// First per-thread destructor block avoids a heap allocation on the common path.
static __declspec(thread) DestructorBlock initialDestructors;

// Current top block for this thread's registered TLS destructors.
static __declspec(thread) DestructorBlock* destructors;

// Registers a TLS destructor, allocating another callback block only when full.
int __cdecl __tlregdtor(Initializer callback)
{
    if (!destructors)
        destructors = &initialDestructors;
    if (destructors->count == kTlsDestructorCapacity) {
        auto block = static_cast<DestructorBlock*>(HeapAlloc(GetProcessHeap(), kTlsBlockHeapZeroFlag, sizeof(DestructorBlock)));
        if (!block)
            return kTlsRegistrationOutOfMemory;
        block->previous = destructors;
        destructors = block;
    }
    destructors->callbacks[destructors->count++] = callback;
    return kTlsRegistrationSuccess;
}

// Invokes registered TLS destructors in reverse order on thread/process detach.
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
                HeapFree(GetProcessHeap(), kTlsBlockHeapFreeFlags, block);
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
