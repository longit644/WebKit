// WebKitWebView: desktop-partition Win32 APIs that exist in OneCoreUAP (ARM)
// but whose headers hide them under PC_APP (WINAPI_PARTITION_DESKTOP|SYSTEM).
// Plain-C declarations (no SAL: this header is force-included first).
// Types match the SDK exactly (HANDLE=void*, DWORD=unsigned long, etc.),
// so later real declarations (if any) are benign duplicates.
// GUARD: meson duplicates -FI flags, so this header can be included twice.
#ifndef WK_W10M_DESKTOP_APIS_H
#define WK_W10M_DESKTOP_APIS_H
// ABI-compatible Win32 handle and scalar aliases hidden by WINAPI_FAMILY_PC_APP.
#include <stdint.h>
typedef void* HANDLE;
typedef unsigned long DWORD;
typedef int BOOL;
typedef const char* LPCSTR;
// ARM SDK Toolhelp arrays keep MAX_PATH (260) and a 256-character module name.
enum {
    kToolhelpModuleNameCapacity = 256,
    kToolhelpModulePathCapacity = 260
};
#ifdef __cplusplus
// wchar_t is a distinct builtin in C++; must spell it exactly or later
// real declarations conflict (harfbuzz hit this).
typedef const wchar_t* LPCWSTR;
#else
// In C, UCRT defines wchar_t as unsigned short: identical type, no conflict.
typedef const unsigned short* LPCWSTR;
#endif
struct _SECURITY_ATTRIBUTES;
typedef struct _SECURITY_ATTRIBUTES SECURITY_ATTRIBUTES, *PSECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;
typedef const void* LPCVOID;
typedef unsigned long* LPDWORD;

#ifdef __cplusplus
// C++ TUs mangle undecorated declarations; these must stay C-linked to match
// the import libs (a missing block once produced mangled CreateFileW refs).
extern "C" {
#endif

// Desktop-partition ANSI file open; resolved from the OneCore import library.
HANDLE __stdcall CreateFileA(
    LPCSTR lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile
    );

// Desktop-partition wide file open; resolved from the OneCore import library.
HANDLE __stdcall CreateFileW(
    LPCWSTR lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile
    );

// Reports whether the process' legacy file APIs use the ANSI code page.
BOOL __stdcall AreFileApisANSI(void);

// Module-handle tag forward-declared so the loader declarations below spell
// the SDK's own handle type exactly. The real windows.h merges with this tag
// when it appears later; the type then matches HMODULE/HINSTANCE in both C
// and C++, where a void* return would strand C++ callers without a cast.
struct HINSTANCE__;

// Desktop-partition module handle lookup; resolved from the OneCore import library.
// libpas queries ntdll for RtlDllShutdownInProgress and tolerates a null
// answer, but the App partition headers hide the declaration entirely, which
// newer compilers reject as an implicit declaration.
struct HINSTANCE__* __stdcall GetModuleHandleW(
    LPCWSTR lpModuleName
    );

// Refcount-neutral module-address lookup exported by OneCore but hidden by PC_APP.
BOOL __stdcall GetModuleHandleExW(
    DWORD dwFlags,
    LPCWSTR lpModuleName,
    // Receives the HMODULE without changing its reference count when requested.
    struct HINSTANCE__** phModule
    );
#ifndef GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
// Interpret the module-name argument as an address inside the loaded image.
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS 0x00000004
// Leave the loader reference count unchanged for diagnostic identity lookup.
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT 0x00000002
#endif

// --- PSAPI working-set snapshot (psapi.h hides these under PC_APP behind the
// DESKTOP|SYSTEM|GAMES partition). WTF's footprint probe needs only the block
// union, the information struct and QueryWorkingSet itself; all resolve from
// the OneCore import library. Layout mirrors the SDK exactly: pointer-width
// members (uintptr_t, like the SDK's ULONG_PTR) with the same 32/64-bit
// VirtualPage split, so indexing and the Shared bit land where the engine
// wrote them. Guarded so a real psapi.h still wins.
#ifndef WK_W10M_PSAPI_WORKING_SET_DEFINED
#define WK_W10M_PSAPI_WORKING_SET_DEFINED
#ifdef __cplusplus
// Mirrors the SDK's own warning suppression around the unnamed struct.
#pragma warning(push)
#pragma warning(disable:4201)
#pragma warning(disable:4214)
#endif
typedef union _WK_W10M_WORKING_SET_BLOCK {
    uintptr_t Flags;
    struct {
        uintptr_t Protection : 5;
        uintptr_t ShareCount : 3;
        uintptr_t Shared : 1;
        uintptr_t Reserved : 3;
#if defined(_WIN64)
        uintptr_t VirtualPage : 52;
#else
        uintptr_t VirtualPage : 20;
#endif
    };
} WK_W10M_WORKING_SET_BLOCK;
typedef struct _WK_W10M_WORKING_SET_INFORMATION {
    uintptr_t NumberOfEntries;
    WK_W10M_WORKING_SET_BLOCK WorkingSetInfo[1];
} WK_W10M_WORKING_SET_INFORMATION;
#ifdef __cplusplus
#pragma warning(pop)
#endif
// Same names the probe code uses; defined here because the SDK hides them.
#define PSAPI_WORKING_SET_BLOCK WK_W10M_WORKING_SET_BLOCK
#define PSAPI_WORKING_SET_INFORMATION WK_W10M_WORKING_SET_INFORMATION
// Working-set query; resolved from the OneCore import library.
BOOL __stdcall QueryWorkingSet(
    HANDLE hProcess,
    void* pv,
    DWORD cb
    );
#endif // WK_W10M_PSAPI_WORKING_SET_DEFINED

// LoadLibrary (unsuffixed): the SDK macro mapping to LoadLibraryW lives in the
// desktop partition; WebKit builds UNICODE so map it here. Third-party Windows
// code such as mimalloc calls both spellings directly, including from C++.
struct HINSTANCE__* __stdcall LoadLibraryW(
    LPCWSTR lpLibFileName
    );
#define LoadLibrary LoadLibraryW
// CreateFile (unsuffixed): the SDK macro mapping to CreateFileW lives in the
// desktop partition; WebKit builds UNICODE so map it here.
#define CreateFile CreateFileW
// Returns the low file-size DWORD and optionally writes the high DWORD.
DWORD __stdcall GetFileSize(
    HANDLE hFile,
    LPDWORD lpFileSizeHigh
    );

// Validates a process-heap allocation on SDKs that hide this declaration.
BOOL __stdcall HeapValidate(
    HANDLE hHeap,
    DWORD dwFlags,
    LPCVOID lpMem
    );

// --- _mbsrchr: multibyte reverse-char-search (mbstring.h). Absent from ARM
// ucrt.lib (only _mbschr ships); implemented in crrt.cpp, declared here so
// TUs that don't include <mbstring.h> (fontconfig fcxml.c) still compile.
// Searches backward for a byte in a multibyte string; implemented in crrt.cpp.
unsigned char* __cdecl _mbsrchr(
    const unsigned char* string,
    unsigned int c
    );

// --- GetWindowsDirectoryA: desktop-only (DESKTOP|SYSTEM|GAMES excludes pure
// PC_APP use). Implemented in crrt.cpp (phone Windows dir is C:\Windows);
// fontconfig takes its address as a GetSystemWindowsDirectoryA fallback.
// Returns the phone's Windows directory; implemented in crrt.cpp.
unsigned int __stdcall GetWindowsDirectoryA(
    char* lpBuffer,
    unsigned int uSize
    );

#ifdef __cplusplus
} // extern "C"
#endif

// Non-UNICODE TUs (fontconfig) use the unsuffixed name; the SDK macro that
// maps it is hidden with the declaration, so map it here.
#define GetWindowsDirectory GetWindowsDirectoryA

// CSIDL folder IDs (shlobj.h is desktop-only); real values, used as opaque
// keys now that storageDirectory() is temp-backed on W10M.
// Roaming application data folder identifier.
#define CSIDL_APPDATA 0x001a

// Local application data folder identifier.
#define CSIDL_LOCAL_APPDATA 0x001c

// --- TlHelp32 module snapshot (tlhelp32.h is desktop-partitioned out under
// PC_APP, so MODULEENTRY32/TH32CS_SNAPMODULE vanish). OpenSSL's dso_win32.c
// needs only the struct and the constant: it resolves
// CreateToolhelp32Snapshot/Module32First/Module32Next dynamically via
// GetProcAddress, so nothing here adds link dependencies. Layout mirrors the
// SDK-compatible layout; guarded so a real tlhelp32.h still wins.
#ifndef MODULEENTRY32
// Toolhelp module snapshot flag passed to CreateToolhelp32Snapshot.
#define TH32CS_SNAPMODULE 0x00000008
typedef struct tagMODULEENTRY32 {
    // Caller sets this to sizeof(MODULEENTRY32) before Module32First/Next.
    DWORD dwSize;

    // Toolhelp module identity within the process.
    DWORD th32ModuleID;

    // Owning process identifier.
    DWORD th32ProcessID;

    // Global load reference count; reserved by modern implementations.
    DWORD GlblcntUsage;

    // Process load reference count; reserved by modern implementations.
    DWORD ProccntUsage;

    // Loaded image base address.
    unsigned char* modBaseAddr;

    // Loaded image size in bytes.
    DWORD modBaseSize;

    // Module handle returned by the loader.
    HANDLE hModule;

    // Short module filename.
    char szModule[kToolhelpModuleNameCapacity];

    // Full module path (SDK MAX_PATH layout).
    char szExePath[kToolhelpModulePathCapacity];
} MODULEENTRY32;
typedef MODULEENTRY32* PMODULEENTRY32;
typedef MODULEENTRY32* LPMODULEENTRY32;
#endif

// --- file-mapping: CreateFileMapping (desktop) is absent under PC_APP
// (only ...FromApp, whose 5-arg signature differs). The SDK's own inline
// wrappers (memoryapi.h, _WIN32_WINNT >= 0x0602) cover modern TUs; this stub
// covers stragglers that force an older _WIN32_WINNT (fontconfig's fcwindows.h
// pins Vista). fontconfig (fccache.c) falls back to read() when mapping
// returns NULL, so stub NULL and let the fallback run. MapViewOfFile/
// UnmapViewOfFile/CloseHandle exist in the App partition: use the real ones.
#if defined(_WIN32_WINNT) && (_WIN32_WINNT < 0x0602)
// Mapping is unavailable in this app partition; callers detect null and fall back to read().
static __inline HANDLE CreateFileMapping(
    HANDLE hFile, LPSECURITY_ATTRIBUTES lpAttr, DWORD flProtect,
    DWORD dwMaxHigh, DWORD dwMaxLow, LPCVOID lpName)
{
    (void)hFile; (void)lpAttr; (void)flProtect;
    (void)dwMaxHigh; (void)dwMaxLow; (void)lpName;
    return NULL;
}
#endif

#endif // WK_W10M_DESKTOP_APIS_H
