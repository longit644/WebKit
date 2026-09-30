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

// --- int64 -> double ---
extern double __floatdidf(long long);
double __i64tod(long long v) { return __floatdidf(v); }

// --- MSVC float conversion helpers -> compiler-rt generics ---
extern double __floatundidf(unsigned long long);
extern long long __fixdfdi(double);
extern unsigned long long __fixunsdfdi(double);
double __u64tod(unsigned long long v) { return __floatundidf(v); }
long long __dtoi64(double v) { return __fixdfdi(v); }
unsigned long long __dtou64(double v) { return __fixunsdfdi(v); }

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

// --- DLL entry: .CRT init walk, user DllMain forward, atexit on detach ---
typedef void (__cdecl *_PVFV)(void);
// selectany: real .CRT anchors (linker-synthesized when objects carry .CRT
// sections) override these empties; with none present the walk is a no-op.
__declspec(selectany) _PVFV __xc_a[1] = { 0 };
__declspec(selectany) _PVFV __xc_z[1] = { 0 };
// selectany: a strong user DllMain in some future configuration would
// collide at link time (revisit then); today nothing defines one, and the
// MSVC CRT default it replaces just returned TRUE.
BOOL __stdcall DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

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
