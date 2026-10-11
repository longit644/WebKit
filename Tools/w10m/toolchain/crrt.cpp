// WebKitWebView ARM32 Store C/C++ runtime support (v0, DOCUMENTED tradeoffs).
// MSVC 14.4x ships no native ARM32 vcruntime: these live only in msvcurt's
// MANAGED ti_inst.obj, which lld-link cannot parse. Provided here instead.
// First-match-wins: this lib precedes msvcurt.lib on the link line, so the
// managed object is never pulled. See ARM-WALLS.md. C++ file (operator
// delete needs it); C symbols under extern "C". Built by hand into
// arm-crtstart.lib (keeps one lib):
//   clang-cl --target=armv7-unknown-windows-msvc -c crrt.cpp -DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP /MD /GR- /EHs-c-
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <windows.h>

extern "C" {

// CRT ABI values and capacities retained for compatibility with the MSVC runtime.
constexpr uintptr_t kSecurityCookieValue = 0xBB40E64EUL;
constexpr int kInitStateUninitialized = 0;
constexpr int kInitStateInProgress = -1;
constexpr DWORD kNoConditionWaitFlags = 0;
constexpr unsigned kAtexitRegistryCapacity = 32;
constexpr int kAtexitSuccess = 0;
constexpr int kAtexitFailure = -1;
constexpr int kEmptyAtexitCount = 0;
constexpr int kNoCreationPermissionMode = 0;
constexpr int kUnsignedCharMask = 0xFF;
constexpr unsigned long long kFileTimeUnitsPerMillisecond = 10000ULL;
constexpr unsigned long long kFileTimeUnitsPerSecond = 10000000ULL;
constexpr long long kUnixEpochOffsetSeconds = 11644473600LL;
constexpr long kNanosecondsPerFileTimeUnit = 100;
constexpr DWORD kFormatMessageAllocateBuffer = 0x00000100UL;
constexpr DWORD kFormatMessageFromSystem = 0x00001000UL;
constexpr DWORD kFormatMessageIgnoreInserts = 0x00000200UL;
constexpr DWORD kFormatMessageSystemFlags = kFormatMessageAllocateBuffer | kFormatMessageFromSystem | kFormatMessageIgnoreInserts;
constexpr DWORD kFormatMessageLanguageDefault = 0;
constexpr DWORD kFormatMessageNoArguments = 0;
constexpr SIZE_T kHeapAllocationDefaultFlags = 0;
constexpr unsigned kWindowsDirectoryNulCharacters = 1;
constexpr char kPhoneWindowsDirectory[] = "C:\\Windows";
constexpr unsigned kCrtInitArraySentinelCount = 1;
constexpr unsigned long long kZeroFileTime = 0;

// --- 64/32-bit division helpers (MSVC names) -> compiler-rt generics ---
// Division helpers return quotient AND remainder in registers and receive
// denominator first. See crdiv.S; ordinary C wrappers cannot express that ABI.

// --- /GS buffer security (v0: fixed cookie, abort on mismatch) ---
// /GS cookie symbol required by compiler-generated stack-protector checks.
uintptr_t __security_cookie = kSecurityCookieValue;

// Aborts the process if compiler-generated stack-cookie verification fails.
void __security_check_cookie(uintptr_t cookie)
{
    if (cookie != __security_cookie)
        abort();
}

// Compiler guard protocol uses kInitStateUninitialized and kInitStateInProgress;
// all other values are completed epochs. The compiler reads this TLS data.
__declspec(thread) int _Init_thread_epoch = INT_MIN;
static int g_init_epoch = INT_MIN;
static SRWLOCK g_init_lock = SRWLOCK_INIT;
static CONDITION_VARIABLE g_init_changed = CONDITION_VARIABLE_INIT;

// Claims an uninitialized guard or waits for the active initializer to finish.
void _Init_thread_header(int* once)
{
    AcquireSRWLockExclusive(&g_init_lock);
    while (*once == kInitStateInProgress)
        SleepConditionVariableSRW(&g_init_changed, &g_init_lock, INFINITE, kNoConditionWaitFlags);
    if (*once == kInitStateUninitialized)
        *once = kInitStateInProgress;
    else
        _Init_thread_epoch = g_init_epoch;
    ReleaseSRWLockExclusive(&g_init_lock);
}

// Publishes successful guarded initialization and wakes competing threads.
void _Init_thread_footer(int* once)
{
    AcquireSRWLockExclusive(&g_init_lock);
    *once = ++g_init_epoch;
    _Init_thread_epoch = g_init_epoch;
    ReleaseSRWLockExclusive(&g_init_lock);
    WakeAllConditionVariable(&g_init_changed);
}

// Resets a failed guarded initialization and wakes a thread to retry.
void _Init_thread_abort(int* once)
{
    AcquireSRWLockExclusive(&g_init_lock);
    *once = kInitStateUninitialized;
    ReleaseSRWLockExclusive(&g_init_lock);
    WakeAllConditionVariable(&g_init_changed);
}

// --- atexit registry (runs on DLL PROCESS_DETACH, LIFO) ---
static void(__cdecl* g_atexit_fns[kAtexitRegistryCapacity])(void);
static int g_atexit_count = kEmptyAtexitCount;

// Registers one detach-time callback; returns failure when the bounded list fills.
int atexit(void(__cdecl* fn)(void))
{
    if (g_atexit_count >= kAtexitRegistryCapacity)
        return kAtexitFailure;
    g_atexit_fns[g_atexit_count++] = fn;
    return kAtexitSuccess;
}

// Runs registered process/DLL callbacks in reverse registration order.
static void wk_run_atexits(void)
{
    while (g_atexit_count > kEmptyAtexitCount)
        g_atexit_fns[--g_atexit_count]();
}

// --- MinGW-style float conversion aliases (compiler-rt only defines these
// for __MINGW32__, but clang emits them for windows-msvc ARM too; see
// fixsfdi.c/floatdisf.c COMPILER_RT_ALIAS lines). s-toi-64 = single->i64,
// i64-to-s = i64->single. Wrappers (not aliases: keep it simple, tail calls).
extern long long __fixsfdi(float);
extern float __floatdisf(long long);
// Converts single precision to the signed 64-bit integer expected by clang.
extern "C" long long __stoi64(float f) { return __fixsfdi(f); }

// Converts a signed 64-bit integer to single precision.
extern "C" float __i64tos(long long v) { return __floatdisf(v); }

// --- MSVC float conversion helpers -> compiler-rt generics ---
extern double __floatdidf(long long);
// Converts a signed 64-bit integer to double precision.
double __i64tod(long long v) { return __floatdidf(v); }
extern double __floatundidf(unsigned long long);
extern long long __fixdfdi(double);
extern unsigned long long __fixunsdfdi(double);
// Converts an unsigned 64-bit integer to double precision.
double __u64tod(unsigned long long v) { return __floatundidf(v); }

// Converts double precision to a signed 64-bit integer.
long long __dtoi64(double v) { return __fixdfdi(v); }

// Converts double precision to an unsigned 64-bit integer.
unsigned long long __dtou64(double v) { return __fixunsdfdi(v); }

// --- _ReadWriteBarrier: clang has no ARM builtin for it; MemoryBarrier()
// (OneCoreUAP) is the exact equivalent full barrier.
// Implements the compiler's full memory barrier using the OneCore API.
extern "C" void _ReadWriteBarrier(void) { MemoryBarrier(); }

// --- _InterlockedAdd64: clang has no ARM lowering; ldrex/strex via GCC
// sync builtin. Returns the INITIAL value (MSVC InterlockedAdd semantics).
// Returns the initial value while atomically adding, matching MSVC semantics.
long long _InterlockedAdd64(volatile long long* addend, long long value)
{
    return __sync_fetch_and_add_8(addend, value);
}

// --- _onexit: CRT onexit-table entry; ours feeds the atexit registry.
// Exact UCRT signature: _onexit_t _onexit(_onexit_t). Returns func/NULL.
typedef int(__cdecl* _onexit_t)(void);
_onexit_t _onexit(_onexit_t func)
{
    extern int atexit(void(__cdecl*)(void));
    if (!func)
        return nullptr;
    return atexit((void(__cdecl*)(void))func) == kAtexitSuccess ? func : nullptr;
}

// (RTTI root node: see C++-linkage definition after extern "C" below;
// crtvft.S carries the undecorated vftable twin.)

// --- std::call_once backend (EXACT 19.44 signatures from xcall_once.h) ---
int __stdcall __std_init_once_begin_initialize_clr(
    void** lpInitOnce, unsigned long dwFlags, int* fPending, void** lpContext)
{
    return InitOnceBeginInitialize((PINIT_ONCE)lpInitOnce, (DWORD)dwFlags, (PBOOL)fPending, lpContext);
}

// Completes an InitOnce operation and wakes threads waiting on its state.
int __stdcall __std_init_once_complete_clr(void** lpInitOnce, unsigned long dwFlags, void* lpContext)
{
    return InitOnceComplete((PINIT_ONCE)lpInitOnce, (DWORD)dwFlags, lpContext);
}

// Implements the CRT abort-on-alternate-init-once path.
void __stdcall __std_init_once_link_alternate_names_and_abort(void) { abort(); }

// --- POSIX undecorated aliases (ARM ucrt.lib exports only _open/_close/
// _read/_write; desktop msvcrt had both). Matches MSVC's own open().
// Declarations manual: avoids header remap surprises.
// Imports for file-descriptor primitives exported only with underscore names.
extern "C" int _open(const char*, int, ...);

// UCRT descriptor-close import forwarded by close().
extern "C" int _close(int);

// UCRT descriptor-read import forwarded by read().
extern "C" int _read(int, void*, unsigned int);

// UCRT descriptor-write import forwarded by write().
extern "C" int _write(int, const void*, unsigned int);

// Maps POSIX open to the UCRT underscore export; pmode is ignored unless O_CREAT.
extern "C" int open(const char* f, int o, ...)
{
    // _open ignores pmode unless O_CREAT, so always forwarding is safe.
    return _open(f, o, kNoCreationPermissionMode);
}

// POSIX aliases required by WebKit dependencies; each forwards to the UCRT symbol.
// Closes a descriptor using the ARM UCRT implementation.
extern "C" int close(int fd) { return _close(fd); }
// Reads bytes from a descriptor using the ARM UCRT implementation.
extern "C" int read(int fd, void* buf, unsigned int n) { return _read(fd, buf, n); }
// Writes bytes to a descriptor using the ARM UCRT implementation.
extern "C" int write(int fd, const void* buf, unsigned int n) { return _write(fd, buf, n); }

// --- more underscore aliases (same story): UCRT ARM exports _x only ---
// Exact UCRT signatures (stdio.h declares the undecorated names too, but
// the ARM import lib lacks them).
// CRT stream type and primitives used by WebKit's POSIX compatibility layer.
extern "C" struct _iobuf;

// UCRT stream-open import forwarded by fdopen().
extern "C" _iobuf* _fdopen(int, char const*);

// POSIX stdio aliases forwarding to the ARM UCRT underscore exports.
extern "C" _iobuf* fdopen(int fd, char const* mode) { return _fdopen(fd, mode); }

// Returns the CRT descriptor for a stream.
extern "C" int _fileno(_iobuf*);

// POSIX spelling of the CRT stream descriptor query.
extern "C" int fileno(_iobuf* f) { return _fileno(f); }

// Selects the binary/text translation mode for a descriptor.
extern "C" int _setmode(int, int);

// POSIX spelling of the CRT descriptor mode setter.
extern "C" int setmode(int fd, int mode) { return _setmode(fd, mode); }

// Tests whether a descriptor refers to a terminal.
extern "C" int _isatty(int);

// POSIX spelling of the CRT terminal query.
extern "C" int isatty(int fd) { return _isatty(fd); }

// Duplicates a descriptor using the ARM UCRT implementation.
extern "C" int _dup(int);

// POSIX spelling of the CRT descriptor duplication operation.
extern "C" int dup(int fd) { return _dup(fd); }

// Duplicates a descriptor onto a specified descriptor number.
extern "C" int _dup2(int, int);

// POSIX spelling of the CRT descriptor duplication-to operation.
extern "C" int dup2(int a, int b) { return _dup2(a, b); }

// Compares two strings case-insensitively using the UCRT locale behavior.
extern "C" int _stricmp(char const*, char const*);

// POSIX spelling of case-insensitive string comparison.
extern "C" int stricmp(char const* a, char const* b) { return _stricmp(a, b); }

// Compares up to n bytes case-insensitively using the UCRT locale behavior.
extern "C" int _strnicmp(char const*, char const*, unsigned int);

// POSIX spelling of bounded case-insensitive string comparison.
extern "C" int strnicmp(char const* a, char const* b, unsigned int n) { return _strnicmp(a, b, n); }

// --- file/dir POSIX aliases (fontconfig link needs them; ARM ucrt.lib only
// exports the underscore forms). Exact UCRT signatures.
extern "C" int _unlink(char const*);

// Removes a file path using the ARM UCRT implementation.
extern "C" int unlink(char const* f) { return _unlink(f); }

// UCRT empty-directory removal import forwarded by rmdir().
extern "C" int _rmdir(char const*);

// Removes an empty directory using the ARM UCRT implementation.
extern "C" int rmdir(char const* d) { return _rmdir(d); }

// UCRT string-duplication import forwarded by strdup().
extern "C" char* _strdup(char const*);

// Duplicates a narrow string using the ARM UCRT allocator.
extern "C" char* strdup(char const* s) { return _strdup(s); }

// UCRT path-access import forwarded by access().
extern "C" int _access(char const*, int);

// Checks path access using the ARM UCRT implementation.
extern "C" int access(char const* p, int m) { return _access(p, m); }

// UCRT file-mode import forwarded by chmod().
extern "C" int _chmod(char const*, int);

// Changes file mode bits using the ARM UCRT implementation.
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

// Converts an unsigned 64-bit integer to single precision.
float __u64tos(unsigned long long value) { return __floatundisf(value); }

// Converts single precision to an unsigned 64-bit integer.
unsigned long long __stou64(float value) { return __fixunssfdi(value); }

// --- _mbsrchr: reverse multibyte-char search. ARM ucrt.lib lacks it
// (only _mbschr). v0: single-byte walk, no lead-byte tables on W10M;
// correct for ASCII paths, which is all fontconfig uses it for ('\\').
// Finds the final byte match; W10M fontconfig only searches ASCII path separators.
extern "C" unsigned char* __cdecl _mbsrchr(const unsigned char* s, unsigned int c)
{
    const unsigned char* found = nullptr;
    unsigned char ch = (unsigned char)(c & kUnsignedCharMask);
    while (*s) {
        if (*s == ch)
            found = s;
        ++s;
    }
    if (ch == '\0')
        return (unsigned char*)s; // match strrchr: NUL searches hit terminator
    return (unsigned char*)found;
}

// --- GetWindowsDirectoryA: desktop-only; phone Windows dir is C:\Windows.
// fontconfig takes its address as a GetSystemWindowsDirectoryA fallback.
// EXACT SDK semantics: success = chars excl NUL; small buffer = need incl NUL.
extern "C" unsigned int __stdcall GetWindowsDirectoryA(char* buf, unsigned int size)
{
    static const char wdir[] = kPhoneWindowsDirectory;
    unsigned int need = (unsigned int)(sizeof(wdir) - 1);
    if (!size)
        return need + kWindowsDirectoryNulCharacters;
    unsigned int n = need < size - kWindowsDirectoryNulCharacters ? need : size - kWindowsDirectoryNulCharacters;
    for (unsigned int i = 0; i < n; ++i)
        buf[i] = wdir[i];
    buf[n] = '\0';
    if (size <= need)
        return need + kWindowsDirectoryNulCharacters;
    return need;
}

// --- MSVC STL thread primitives missing from ARM-store CRT (xthreads.h
// declares them plain, expecting vcruntime; msvcurt only has the older set).
// _Thrd_sleep_for takes milliseconds (unlike _Thrd_sleep's xtime).
// Implements the MSVC STL relative millisecond sleep primitive.
extern "C" void __stdcall _Thrd_sleep_for(unsigned long ms) noexcept { Sleep(ms); }
// _Cnd_timedwait_for_unchecked takes relative ms; forward to the absolute
// _Cnd_timedwait in msvcurt. extern "C" erases param types at link time, so
// opaque void* params are ABI-correct (all pointers); xtime layout
// {time_t sec; long nsec} is stable.
// Absolute Unix-epoch timestamp passed to msvcurt's condition wait.
struct __wk_xtime {
    // Whole seconds since the Unix epoch.
    long long sec;

    // Nanosecond remainder within the current second.
    long nsec;
};

// Existing ARM UCRT primitive accepting an absolute timeout.
extern "C" int __cdecl _Cnd_timedwait(void*, void*, const __wk_xtime*) noexcept;

// Converts the STL relative timeout to the UCRT absolute xtime representation.
extern "C" int __stdcall _Cnd_timedwait_for_unchecked(void* cond, void* mtx, unsigned int ms) noexcept
{
    __wk_xtime xt;
    unsigned long long ft = kZeroFileTime;
    GetSystemTimeAsFileTime(reinterpret_cast<FILETIME*>(&ft)); // 100ns since 1601
    unsigned long long total100ns = ft + (unsigned long long)ms * kFileTimeUnitsPerMillisecond;
    xt.sec = (long long)(total100ns / kFileTimeUnitsPerSecond) - kUnixEpochOffsetSeconds; // to 1970 epoch
    xt.nsec = (long)(total100ns % kFileTimeUnitsPerSecond) * kNanosecondsPerFileTimeUnit;
    return _Cnd_timedwait(cond, mtx, &xt);
}

// TLS directory, dynamic initializers and destructor registration: crtls.cpp.
// TLS callbacks implemented in crtls.cpp and invoked by this DLL startup.
extern void __stdcall __dyn_tls_init(void*, unsigned long, void*);
extern void __stdcall wk_tls_destroy(void*, unsigned long, void*);

// --- system_error message helpers (__msvc_system_error_abi.hpp, extern "C").
// FormatMessageA is App-legal; LocalFree pairs the allocate-buffer.
extern "C" unsigned int __stdcall __std_system_error_allocate_message(
    unsigned long _Message_id, char** _Ptr_str) noexcept
{
    char* buf = nullptr;
    unsigned long n = FormatMessageA(kFormatMessageSystemFlags,
        nullptr, _Message_id, kFormatMessageLanguageDefault, (char*)&buf, kFormatMessageNoArguments, nullptr);
    *_Ptr_str = buf;
    return n;
}

// Frees the FormatMessageA buffer returned by the allocator above.
extern "C" void __stdcall __std_system_error_deallocate_message(char* _Str) noexcept
{
    if (_Str)
        LocalFree(_Str);
}

// Trims trailing ASCII whitespace from an error message without modifying it.
extern "C" unsigned int __stdcall __std_get_string_size_without_trailing_whitespace(
    const char* _Str, unsigned int _Size) noexcept
{
    unsigned int n = _Size;
    while (n > kEmptyAtexitCount) {
        char c = _Str[n - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            break;
        --n;
    }
    return n;
}

// --- DLL entry: .CRT init walk, default DllMain, atexit on detach ---
typedef void(__cdecl* _PVFV)(void);
#pragma section(".CRT$XCA", read)
#pragma section(".CRT$XCZ", read)
__declspec(allocate(".CRT$XCA")) _PVFV __xc_a[kCrtInitArraySentinelCount] = { nullptr };
__declspec(allocate(".CRT$XCZ")) _PVFV __xc_z[kCrtInitArraySentinelCount] = { nullptr };
// NOTE: the default DllMain lives in crdllmain.c (separate object) so that
// targets defining their own DllMain (e.g. OpenSSL dllmain.c) never collide:
// archive members load on demand, and a direct object definition wins.
extern BOOL __stdcall DllMain(HINSTANCE, DWORD, LPVOID);

// Runs CRT initializers, TLS attach, user DllMain, and process-detach callbacks.
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
// MSVC ABI replacement for std::nothrow_t; the class-key affects mangling.
struct nothrow_t {
    explicit nothrow_t() = default;
};
extern nothrow_t const nothrow;
}
::std::nothrow_t const std::nothrow = ::std::nothrow_t();

// Allocates scalar storage from the process heap and returns null on failure.
void* operator new(unsigned int s, ::std::nothrow_t const&) noexcept { return HeapAlloc(GetProcessHeap(), kHeapAllocationDefaultFlags, s); }

// Releases scalar storage allocated by the nothrow overload.
void operator delete(void* p, ::std::nothrow_t const&) noexcept
{
    if (p)
        HeapFree(GetProcessHeap(), kHeapAllocationDefaultFlags, p);
}

// Allocates array storage from the process heap and returns null on failure.
void* operator new[](unsigned int s, ::std::nothrow_t const&) noexcept { return HeapAlloc(GetProcessHeap(), kHeapAllocationDefaultFlags, s); }

// Releases array storage allocated by the nothrow overload.
void operator delete[](void* p, ::std::nothrow_t const&) noexcept
{
    if (p)
        HeapFree(GetProcessHeap(), kHeapAllocationDefaultFlags, p);
}
namespace std {
class _Facet_base;
void __cdecl _Facet_Register(_Facet_base*);
}
void __cdecl std::_Facet_Register(std::_Facet_base*) { }

// --- operator new/delete via process heap (no ARM32 vcruntime; pairs with
// HeapAlloc-based new that msvcurt would provide - here fully ours). Sized
// forms forward (v0). ---
// Allocates scalar storage from the process heap; this port has no vcruntime allocator.
void* operator new(unsigned int s) { return HeapAlloc(GetProcessHeap(), kHeapAllocationDefaultFlags, s); }

// Releases scalar process-heap storage.
void operator delete(void* p)
{
    if (p)
        HeapFree(GetProcessHeap(), kHeapAllocationDefaultFlags, p);
}

// Sized scalar delete forwards to the unsized process-heap release.
void operator delete(void* p, unsigned int) { operator delete(p); }

// Allocates array storage through the scalar process-heap allocator.
void* operator new[](unsigned int s) { return operator new(s); }

// Releases array storage through the scalar process-heap release.
void operator delete[](void* p) { operator delete(p); }

// Sized array delete forwards to the unsized array release.
void operator delete[](void* p, unsigned int) { operator delete(p); }

// --- RTTI root node: C++ linkage with the EXACT struct name so it mangles to
// ?__type_info_root_node@@3U__type_info_node@@A (extern "C" cannot spell it).
// Same never-dereferenced deal as crtvft.S's vftable (kept there too; harmless
// duplicate-free: only one or the other is referenced... actually both may
// coexist - different symbols, no conflict).
// MSVC RTTI linked-list node layout required by compiler-generated helpers.
struct __type_info_node {
    // Reserved memory pointer retained for ABI layout.
    void* memPtr;

    // Next RTTI node; null because the runtime has no registration chain.
    void* next;
};

// Root RTTI node required by compiler-generated dynamic-cast support.
__type_info_node __type_info_root_node = { nullptr, nullptr };
