// WebKitView ARM32 Store C/C++ runtime support (v0, DOCUMENTED tradeoffs).
// MSVC 14.4x ships no native ARM32 vcruntime: these live only in msvcurt's
// MANAGED ti_inst.obj, which lld-link cannot parse. Provided here instead.
// First-match-wins: this lib precedes msvcurt.lib on the link line, so the
// managed object is never pulled. See ARM-WALLS.md. C++ file (operator
// delete needs it); C symbols under extern "C". Built by hand into
// arm-crtstart.lib (keeps one lib):
//   clang-cl --target=thumbv7-unknown-windows-msvc -c crrt.cpp -DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP /MD /GR- /EHs-c-
#include <windows.h>
#include <stdint.h>

extern "C" {

// --- 64/32-bit division helpers (MSVC names) -> compiler-rt generics ---
extern long long __divdi3(long long, long long);
extern unsigned long long __udivdi3(unsigned long long, unsigned long long);
extern long __divsi3(long, long);
extern unsigned __udivsi3(unsigned, unsigned);
long long __rt_sdiv64(long long a, long long b) { return __divdi3(a, b); }
unsigned long long __rt_udiv64(unsigned long long a, unsigned long long b) { return __udivdi3(a, b); }
long __rt_sdiv(long a, long b) { return __divsi3(a, b); }
unsigned __rt_udiv(unsigned a, unsigned b) { return __udivsi3(a, b); }

// --- /GS buffer security (v0: fixed cookie, abort on mismatch) ---
uintptr_t __security_cookie = 0xBB40E64EUL;
void __security_check_cookie(uintptr_t cookie)
{
    if (cookie != __security_cookie)
        abort();
}

// --- thread-local / magic-statics support (v0: single-threaded init) ---
unsigned long _tls_index = 0;
int _Init_thread_header(int* once)
{
    if (*once != -1)
        return 0; // run the initializer now
    return 1; // already done, skip
}
void _Init_thread_footer(int* once) { *once = -1; }
void _Init_thread_epoch(int* epoch) { *epoch = 1; }

// --- atexit registry (runs on DLL PROCESS_DETACH, LIFO) ---
#define WK_ATEXIT_MAX 32
static void (__cdecl *g_atexit_fns[WK_ATEXIT_MAX])(void);
static int g_atexit_count = 0;
int atexit(void (__cdecl *fn)(void))
{
    if (g_atexit_count >= WK_ATEXIT_MAX)
        return -1;
    g_atexit_fns[g_atexit_count++] = fn;
    return 0;
}
static void wk_run_atexits(void)
{
    while (g_atexit_count > 0)
        g_atexit_fns[--g_atexit_count]();
}

// --- MinGW-style float conversion aliases (compiler-rt only defines these
// for __MINGW32__, but clang emits them for windows-msvc ARM too; see
// fixsfdi.c/floatdisf.c COMPILER_RT_ALIAS lines). s-toi-64 = single->i64,
// i64-to-s = i64->single. Wrappers (not aliases: keep it simple, tail calls).
extern long long __fixsfdi(float);
extern float __floatdisf(long long);
extern "C" long long __stoi64(float f) { return __fixsfdi(f); }
extern "C" float __i64tos(long long v) { return __floatdisf(v); }

// --- MSVC float conversion helpers -> compiler-rt generics ---
extern double __floatdidf(long long);
double __i64tod(long long v) { return __floatdidf(v); }
extern double __floatundidf(unsigned long long);
extern long long __fixdfdi(double);
extern unsigned long long __fixunsdfdi(double);
double __u64tod(unsigned long long v) { return __floatundidf(v); }
long long __dtoi64(double v) { return __fixdfdi(v); }
unsigned long long __dtou64(double v) { return __fixunsdfdi(v); }

// --- _ReadWriteBarrier: clang has no ARM builtin for it; MemoryBarrier()
// (OneCoreUAP) is the exact equivalent full barrier.
extern "C" void _ReadWriteBarrier(void) { MemoryBarrier(); }

// --- _InterlockedAdd64: clang has no ARM lowering; ldrex/strex via GCC
// sync builtin. Returns the INITIAL value (MSVC InterlockedAdd semantics).
long long _InterlockedAdd64(volatile long long* addend, long long value)
{
    return __sync_fetch_and_add_8(addend, value);
}

// --- _onexit: CRT onexit-table entry; ours feeds the atexit registry.
// Exact UCRT signature: _onexit_t _onexit(_onexit_t). Returns func/NULL.
typedef int (__cdecl *_onexit_t)(void);
_onexit_t _onexit(_onexit_t func)
{
    extern int atexit(void (__cdecl *)(void));
    if (!func)
        return 0;
    return atexit((void (__cdecl *)(void))func) == 0 ? func : 0;
}

// (RTTI root node: see C++-linkage definition after extern "C" below;
// crtvft.S carries the undecorated vftable twin.)

// --- std::call_once backend (EXACT 19.44 signatures from xcall_once.h) ---
int __stdcall __std_init_once_begin_initialize_clr(
    void** lpInitOnce, unsigned long dwFlags, int* fPending, void** lpContext)
{
    return InitOnceBeginInitialize((PINIT_ONCE)lpInitOnce, (DWORD)dwFlags, (PBOOL)fPending, lpContext);
}
int __stdcall __std_init_once_complete_clr(void** lpInitOnce, unsigned long dwFlags, void* lpContext)
{
    return InitOnceComplete((PINIT_ONCE)lpInitOnce, (DWORD)dwFlags, lpContext);
}
void __stdcall __std_init_once_link_alternate_names_and_abort(void) { abort(); }

// --- POSIX undecorated aliases (ARM ucrt.lib exports only _open/_close/
// _read/_write; desktop msvcrt had both). Matches MSVC's own open().
// Declarations manual: avoids header remap surprises.
extern "C" int _open(const char*, int, ...);
extern "C" int _close(int);
extern "C" int _read(int, void*, unsigned int);
extern "C" int _write(int, const void*, unsigned int);
extern "C" int open(const char* f, int o, ...)
{
    // _open ignores pmode unless O_CREAT, so always forwarding is safe.
    return _open(f, o, 0);
}
extern "C" int close(int fd) { return _close(fd); }
extern "C" int read(int fd, void* buf, unsigned int n) { return _read(fd, buf, n); }
extern "C" int write(int fd, const void* buf, unsigned int n) { return _write(fd, buf, n); }

// --- more underscore aliases (same story): UCRT ARM exports _x only ---
// Exact UCRT signatures (stdio.h declares the undecorated names too, but
// the ARM import lib lacks them).
extern "C" struct _iobuf;
extern "C" _iobuf* _fdopen(int, char const*);
extern "C" _iobuf* fdopen(int fd, char const* mode) { return _fdopen(fd, mode); }
extern "C" int _fileno(_iobuf*);
extern "C" int fileno(_iobuf* f) { return _fileno(f); }
extern "C" int _setmode(int, int);
extern "C" int setmode(int fd, int mode) { return _setmode(fd, mode); }
extern "C" int _isatty(int);
extern "C" int isatty(int fd) { return _isatty(fd); }
extern "C" int _dup(int);
extern "C" int dup(int fd) { return _dup(fd); }
extern "C" int _dup2(int, int);
extern "C" int dup2(int a, int b) { return _dup2(a, b); }
extern "C" int _stricmp(char const*, char const*);
extern "C" int stricmp(char const* a, char const* b) { return _stricmp(a, b); }
extern "C" int _strnicmp(char const*, char const*, unsigned int);
extern "C" int strnicmp(char const* a, char const* b, unsigned int n) { return _strnicmp(a, b, n); }

// --- DLL entry: .CRT init walk, default DllMain, atexit on detach ---
typedef void (__cdecl *_PVFV)(void);
// selectany: real .CRT anchors (linker-synthesized when objects carry .CRT
// sections) override these empties; with none present the walk is a no-op.
__declspec(selectany) _PVFV __xc_a[1] = { 0 };
__declspec(selectany) _PVFV __xc_z[1] = { 0 };
// NOTE: the default DllMain lives in crdllmain.c (separate object) so that
// targets defining their own DllMain (e.g. OpenSSL dllmain.c) never collide:
// archive members load on demand, and a direct object definition wins.
extern BOOL __stdcall DllMain(HINSTANCE, DWORD, LPVOID);

BOOL __stdcall _DllMainCRTStartup(HINSTANCE hinst, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        for (_PVFV* p = __xc_a; p < __xc_z; ++p) {
            if (*p)
                (**p)();
        }
    }
    BOOL ok = DllMain(hinst, reason, reserved);
    if (reason == DLL_PROCESS_DETACH)
        wk_run_atexits();
    return ok;
}

} // extern "C"

// --- operator new/delete via process heap (no ARM32 vcruntime; pairs with
// HeapAlloc-based new that msvcurt would provide - here fully ours). Sized
// forms forward (v0). ---
void* operator new(unsigned int s) { return HeapAlloc(GetProcessHeap(), 0, s); }
void operator delete(void* p) { if (p) HeapFree(GetProcessHeap(), 0, p); }
void operator delete(void* p, unsigned int) { operator delete(p); }
void* operator new[](unsigned int s) { return operator new(s); }
void operator delete[](void* p) { operator delete(p); }
void operator delete[](void* p, unsigned int) { operator delete(p); }

// --- RTTI root node: C++ linkage with the EXACT struct name so it mangles to
// ?__type_info_root_node@@3U__type_info_node@@A (extern "C" cannot spell it).
// Same never-dereferenced deal as crtvft.S's vftable (kept there too; harmless
// duplicate-free: only one or the other is referenced... actually both may
// coexist - different symbols, no conflict).
struct __type_info_node { void* memPtr; void* next; };
__type_info_node __type_info_root_node = { 0, 0 };
