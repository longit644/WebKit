// WebKitWebView ARM32 Store C/C++ runtime support (v0, DOCUMENTED tradeoffs).
// MSVC 14.4x ships no native ARM32 vcruntime: these live only in msvcurt's
// MANAGED ti_inst.obj, which lld-link cannot parse. Provided here instead.
// First-match-wins: this lib precedes msvcurt.lib on the link line, so the
// managed object is never pulled. See ARM-WALLS.md. C++ file (operator
// delete needs it); C symbols under extern "C". Built by hand into
// arm-crtstart.lib (keeps one lib):
//   clang-cl --target=armv7-unknown-windows-msvc -c crrt.cpp -DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP /MD /GR- /EHs-c-
#include <windows.h>
#include <stdint.h>
#include <stdlib.h>

extern "C" {

// --- 64/32-bit division helpers (MSVC names) -> compiler-rt generics ---
// Division helpers return quotient AND remainder in registers and receive
// denominator first. See crdiv.S; ordinary C wrappers cannot express that ABI.

// --- /GS buffer security (v0: fixed cookie, abort on mismatch) ---
uintptr_t __security_cookie = 0xBB40E64EUL;
void __security_check_cookie(uintptr_t cookie)
{
    if (cookie != __security_cookie)
        abort();
}

// Compiler guard protocol: 0 = uninitialized, -1 = initializing, other values
// are completed epochs. The compiler reads _Init_thread_epoch as TLS DATA.
__declspec(thread) int _Init_thread_epoch = (-2147483647 - 1);
static int g_init_epoch = (-2147483647 - 1);
static SRWLOCK g_init_lock = SRWLOCK_INIT;
static CONDITION_VARIABLE g_init_changed = CONDITION_VARIABLE_INIT;
void _Init_thread_header(int* once)
{
    AcquireSRWLockExclusive(&g_init_lock);
    while (*once == -1)
        SleepConditionVariableSRW(&g_init_changed, &g_init_lock, INFINITE, 0);
    if (!*once)
        *once = -1;
    else
        _Init_thread_epoch = g_init_epoch;
    ReleaseSRWLockExclusive(&g_init_lock);
}
void _Init_thread_footer(int* once)
{
    AcquireSRWLockExclusive(&g_init_lock);
    *once = ++g_init_epoch;
    _Init_thread_epoch = g_init_epoch;
    ReleaseSRWLockExclusive(&g_init_lock);
    WakeAllConditionVariable(&g_init_changed);
}
void _Init_thread_abort(int* once)
{
    AcquireSRWLockExclusive(&g_init_lock);
    *once = 0;
    ReleaseSRWLockExclusive(&g_init_lock);
    WakeAllConditionVariable(&g_init_changed);
}

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

// --- file/dir POSIX aliases (fontconfig link needs them; ARM ucrt.lib only
// exports the underscore forms). Exact UCRT signatures.
extern "C" int _unlink(char const*);
extern "C" int unlink(char const* f) { return _unlink(f); }
extern "C" int _rmdir(char const*);
extern "C" int rmdir(char const* d) { return _rmdir(d); }
extern "C" char* _strdup(char const*);
extern "C" char* strdup(char const* s) { return _strdup(s); }
extern "C" int _access(char const*, int);
extern "C" int access(char const* p, int m) { return _access(p, m); }
extern "C" int _chmod(char const*, int);
extern "C" int chmod(char const* p, int m) { return _chmod(p, m); }

// UCRT's nonstandard-name declarations mark these aliases dllimport. ARM lld
// synthesizes local __imp_* slots without preserving the Thumb function bit
// (LNK4217); the phone then enters an alias in ARM state and faults. Explicit
// function-address relocations retain the target's Thumb bit.
decltype(&open) __imp_open = &open;
decltype(&close) __imp_close = &close;
decltype(&read) __imp_read = &read;
decltype(&write) __imp_write = &write;
decltype(&fdopen) __imp_fdopen = &fdopen;
decltype(&fileno) __imp_fileno = &fileno;
decltype(&setmode) __imp_setmode = &setmode;
decltype(&isatty) __imp_isatty = &isatty;
decltype(&dup) __imp_dup = &dup;
decltype(&dup2) __imp_dup2 = &dup2;
decltype(&stricmp) __imp_stricmp = &stricmp;
decltype(&strnicmp) __imp_strnicmp = &strnicmp;
decltype(&unlink) __imp_unlink = &unlink;
decltype(&rmdir) __imp_rmdir = &rmdir;
decltype(&strdup) __imp_strdup = &strdup;
decltype(&access) __imp_access = &access;
decltype(&chmod) __imp_chmod = &chmod;

// ARM compiler helpers: 's' means single precision, not string.
extern float __floatundisf(unsigned long long);
extern unsigned long long __fixunssfdi(float);
float __u64tos(unsigned long long value) { return __floatundisf(value); }
unsigned long long __stou64(float value) { return __fixunssfdi(value); }

 // --- _mbsrchr: reverse multibyte-char search. ARM ucrt.lib lacks it
// (only _mbschr). v0: single-byte walk, no lead-byte tables on UWP;
// correct for ASCII paths, which is all fontconfig uses it for ('\\').
extern "C" unsigned char* __cdecl _mbsrchr(const unsigned char* s, unsigned int c)
{
    const unsigned char* found = 0;
    unsigned char ch = (unsigned char)(c & 0xFF);
    while (*s) {
        if (*s == ch)
            found = s;
        ++s;
    }
    if (ch == 0)
        return (unsigned char*)s; // match strrchr: NUL searches hit terminator
    return (unsigned char*)found;
}

// --- GetWindowsDirectoryA: desktop-only; phone Windows dir is C:\Windows.
// fontconfig takes its address as a GetSystemWindowsDirectoryA fallback.
// EXACT SDK semantics: success = chars excl NUL; small buffer = need incl NUL.
extern "C" unsigned int __stdcall GetWindowsDirectoryA(char* buf, unsigned int size)
{
    static const char wdir[] = "C:\\Windows";
    unsigned int need = (unsigned int)(sizeof(wdir) - 1);
    if (size == 0)
        return need + 1;
    unsigned int n = need < size - 1 ? need : size - 1;
    for (unsigned int i = 0; i < n; ++i)
        buf[i] = wdir[i];
    buf[n] = '\0';
    if (size <= need)
        return need + 1;
    return need;
}

// --- MSVC STL thread primitives missing from ARM-store CRT (xthreads.h
// declares them plain, expecting vcruntime; msvcurt only has the older set).
// _Thrd_sleep_for takes milliseconds (unlike _Thrd_sleep's xtime).
extern "C" void __stdcall _Thrd_sleep_for(unsigned long ms) noexcept { Sleep(ms); }
// _Cnd_timedwait_for_unchecked takes relative ms; forward to the absolute
// _Cnd_timedwait in msvcurt. extern "C" erases param types at link time, so
// opaque void* params are ABI-correct (all pointers); xtime layout
// {time_t sec; long nsec} is stable.
struct __wk_xtime { long long sec; long nsec; };
extern "C" int __cdecl _Cnd_timedwait(void*, void*, const __wk_xtime*) noexcept;
extern "C" int __stdcall _Cnd_timedwait_for_unchecked(void* cond, void* mtx, unsigned int ms) noexcept
{
    __wk_xtime xt;
    unsigned long long ft = 0;
    GetSystemTimeAsFileTime(reinterpret_cast<FILETIME*>(&ft)); // 100ns since 1601
    unsigned long long total100ns = ft + (unsigned long long)ms * 10000ULL;
    xt.sec = (long long)(total100ns / 10000000ULL) - 11644473600LL; // to 1970 epoch
    xt.nsec = (long)(total100ns % 10000000ULL) * 100;
    return _Cnd_timedwait(cond, mtx, &xt);
}

// TLS directory, dynamic initializers and destructor registration: crtls.cpp.
extern void __stdcall __dyn_tls_init(void*, unsigned long, void*);
extern void __stdcall wk_tls_destroy(void*, unsigned long, void*);

// --- system_error message helpers (__msvc_system_error_abi.hpp, extern "C").
// FormatMessageA is App-legal; LocalFree pairs the allocate-buffer.
extern "C" unsigned int __stdcall __std_system_error_allocate_message(
    unsigned long _Message_id, char** _Ptr_str) noexcept
{
    char* buf = nullptr;
    unsigned long n = FormatMessageA(0x00000100 /*ALLOCATE_BUFFER*/ | 0x00001000 /*FROM_SYSTEM*/
            | 0x00000200 /*IGNORE_INSERTS*/,
        nullptr, _Message_id, 0, (char*)&buf, 0, nullptr);
    *_Ptr_str = buf;
    return n;
}
extern "C" void __stdcall __std_system_error_deallocate_message(char* _Str) noexcept
{
    if (_Str)
        LocalFree(_Str);
}
extern "C" unsigned int __stdcall __std_get_string_size_without_trailing_whitespace(
    const char* _Str, unsigned int _Size) noexcept
{
    unsigned int n = _Size;
    while (n > 0) {
        char c = _Str[n - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            break;
        --n;
    }
    return n;
}

// --- DLL entry: .CRT init walk, default DllMain, atexit on detach ---
typedef void (__cdecl *_PVFV)(void);
#pragma section(".CRT$XCA", read)
#pragma section(".CRT$XCZ", read)
__declspec(allocate(".CRT$XCA")) _PVFV __xc_a[1] = { 0 };
__declspec(allocate(".CRT$XCZ")) _PVFV __xc_z[1] = { 0 };
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
    if (reason == DLL_PROCESS_ATTACH)
        __dyn_tls_init(hinst, DLL_THREAD_ATTACH, reserved);
    BOOL ok = DllMain(hinst, reason, reserved);
    if (reason == DLL_PROCESS_DETACH) {
        wk_tls_destroy(hinst, reason, reserved);
        wk_run_atexits();
    }
    return ok;
}

} // extern "C"

// --- C++ new/delete nothrow forms + locale facet registry (msvcp pieces
// with no ARM-store binary). Spelling mirrors vcruntime_new.h exactly
// (a <new> include breaks against our force-include order). MUST live after
// the extern "C" block above: these need C++ mangling (class-key matters:
// MSVC declares _Facet_base as class, not struct).
namespace std {
struct nothrow_t { explicit nothrow_t() = default; };
extern nothrow_t const nothrow;
}
::std::nothrow_t const std::nothrow = ::std::nothrow_t();
void* operator new(unsigned int s, ::std::nothrow_t const&) noexcept { return HeapAlloc(GetProcessHeap(), 0, s); }
void operator delete(void* p, ::std::nothrow_t const&) noexcept { if (p) HeapFree(GetProcessHeap(), 0, p); }
void* operator new[](unsigned int s, ::std::nothrow_t const&) noexcept { return HeapAlloc(GetProcessHeap(), 0, s); }
void operator delete[](void* p, ::std::nothrow_t const&) noexcept { if (p) HeapFree(GetProcessHeap(), 0, p); }
namespace std { class _Facet_base; void __cdecl _Facet_Register(_Facet_base*); }
void __cdecl std::_Facet_Register(std::_Facet_base*) { }

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
