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

// --- __u64tos: vcruntime int->string helper (absent from ARM-store libs;
 // clang-cl emits calls for %p/%llu-style lowering, e.g. expat debug code).
 // EXACT MSVC semantics: radix 2..36, lowercase digits, no prefix.
 extern "C" char* __cdecl __u64tos(unsigned long long value, char* str, int radix)
 {
     static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
     char* p = str;
     if (radix < 2 || radix > 36) {
         *p = '\0';
         return str;
     }
     // Generate reversed, then flip in place.
     do {
         *p++ = digits[value % (unsigned long long)radix];
         value /= (unsigned long long)radix;
     } while (value);
     *p = '\0';
     for (char* lo = str; lo < --p; ++lo) {
         char t = *lo;
         *lo = *p;
         *p = t;
     }
     return str;
 }

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

// --- MSVC dynamic-TLS hooks (vcruntime normally provides; absent ARM-store).
// v0: our thread_locals are all POD (zero-init suffices). Dynamic thread
// locals (MSVC-STL locale internals referenced by ANGLE entry points) stay
// zero; those paths never execute in v0 (no WebGL). Guard preset to
// initialized so the init call is skipped.
extern "C" unsigned long __tls_guard = 1;
extern "C" void* __cdecl __dyn_tls_on_demand_init(void*, unsigned long, void*) { return 0; }

// --- TLS directory + mimalloc callback anchors (normally vcruntime).
// mimalloc's prim.c forces /INCLUDE:__tls_used/__mi_tls_callback_pre/post
// with x86 cdecl decoration (no-op names on ARM, which has no decoration).
// The REAL callbacks still wire via .CRT$XLB/XLY section merging; these
// satisfy the names. __dyn_tls_init is demanded the same way; nothing calls
// it in our tree (swept all objects), so a TRUE no-op suffices. v0: dynamic
// TLS initializers don't run (see __tls_guard above).
struct __wk_tls_directory {
    unsigned long StartAddressOfRawData;
    unsigned long EndAddressOfRawData;
    unsigned long AddressOfIndex;
    unsigned long AddressOfCallBacks;
    unsigned long SizeOfZeroFill;
    unsigned long Characteristics;
};
extern "C" __wk_tls_directory __tls_used = { 0, 0, 0, 0, 0, 0 };
typedef void (__stdcall *__wk_tls_callback)(void*, unsigned long, void*);
extern "C" __wk_tls_callback __mi_tls_callback_pre[1] = { 0 };
extern "C" __wk_tls_callback __mi_tls_callback_post[1] = { 0 };
extern "C" int __cdecl __dyn_tls_init(void*, unsigned long, void*) { return 1; }

// --- __stou64: vcruntime string->u64 helper (sibling of __u64tos above;
// referenced by MSVC-STL locale number parsing). (nptr, endptr, base),
// exactly strtoull semantics.
extern "C" unsigned long long __cdecl __stou64(const char* s, char** end, int base)
{
    return strtoull(s, end, base);
}

// --- .CRT$XDU walk: per-TU dynamic TLS initializers (clang emits one static
// __tls_init trampoline per TU needing it into .CRT$XDU). No vcruntime here,
// so _DllMainCRTStartup walks them on attach. Markers bracket the group
// lexically ($XDTZZ < $XDU < $XDUZZ); entries take (hinst, reason, reserved)
// NTAPI-style (trampolines forward/ignore regs as needed).
#pragma section(".CRT$XDTZZ", read)
__declspec(allocate(".CRT$XDTZZ")) void* __wk_xdu_start[1] = { 0 };
#pragma section(".CRT$XDUZZ", read)
__declspec(allocate(".CRT$XDUZZ")) void* __wk_xdu_end[1] = { 0 };
typedef void (__stdcall *__wk_tls_cb)(void*, unsigned long, void*);
static void wk_run_xdu(void* hinst, unsigned long reason, void* reserved)
{
    for (void** p = &__wk_xdu_start[1]; p < &__wk_xdu_end[0]; ++p) {
        if (*p)
            ((__wk_tls_cb)*p)(hinst, reason, reserved);
    }
}

// --- _tlregdtor: TLS destructor registry via FLS (App-legal). clang emits
// calls for thread_locals with non-trivial dtors (WTF ThreadSpecific).
struct __wk_tlreg { void (__cdecl *fn)(void*); void* obj; };
struct __wk_tlreg_block { unsigned long count; unsigned long capacity; struct __wk_tlreg entries[1]; };
static volatile long g_fls_index = -1;
static void __stdcall wk_fls_dtor(void* data)
{
    struct __wk_tlreg_block* b = (struct __wk_tlreg_block*)data;
    if (!b)
        return;
    for (unsigned long i = b->count; i > 0; --i) {
        if (b->entries[i - 1].fn)
            b->entries[i - 1].fn(b->entries[i - 1].obj);
    }
    HeapFree(GetProcessHeap(), 0, b);
}
extern "C" void __cdecl __tlregdtor(void (__cdecl *fn)(void*), void* obj)
{
    if (g_fls_index == -1) {
        unsigned long idx = FlsAlloc(wk_fls_dtor);
        if (idx == 0xFFFFFFFFUL)
            return; // no FLS slot: leak the registration rather than crash
        InterlockedCompareExchange(&g_fls_index, (long)idx, -1);
        if ((unsigned long)g_fls_index != idx)
            FlsFree(idx); // lost the race; keep the winner
    }
    struct __wk_tlreg_block* b = (struct __wk_tlreg_block*)FlsGetValue((unsigned long)g_fls_index);
    if (!b || b->count >= b->capacity) {
        unsigned long newcap = b ? b->capacity * 2 : 8;
        unsigned long bytes = sizeof(struct __wk_tlreg_block) + (newcap - 1) * sizeof(struct __wk_tlreg);
        struct __wk_tlreg_block* nb = (struct __wk_tlreg_block*)HeapReAlloc(
            GetProcessHeap(), 0, b, bytes);
        if (!nb)
            return; // v0: leak the dtor registration rather than crash
        if (!b)
            nb->count = 0;
        nb->capacity = newcap;
        b = nb;
        FlsSetValue((unsigned long)g_fls_index, b);
    }
    b->entries[b->count].fn = fn;
    b->entries[b->count].obj = obj;
    ++b->count;
}

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
    if (reason == DLL_PROCESS_ATTACH || reason == DLL_THREAD_ATTACH)
        wk_run_xdu(hinst, reason, reserved);
    BOOL ok = DllMain(hinst, reason, reserved);
    if (reason == DLL_PROCESS_DETACH)
        wk_run_atexits();
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
