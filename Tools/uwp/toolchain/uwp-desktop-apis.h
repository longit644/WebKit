// WebKitWebView: desktop-partition Win32 APIs that exist in OneCoreUAP (ARM)
// but whose headers hide them under PC_APP (WINAPI_PARTITION_DESKTOP|SYSTEM).
// Plain-C declarations (no SAL: this header is force-included first).
// Types match the SDK exactly (HANDLE=void*, DWORD=unsigned long, etc.),
// so later real declarations (if any) are benign duplicates.
// GUARD: meson duplicates -FI flags, so this header can be included twice.
#ifndef WK_UWP_DESKTOP_APIS_H
#define WK_UWP_DESKTOP_APIS_H
typedef void* HANDLE;
typedef unsigned long DWORD;
typedef int BOOL;
typedef const char* LPCSTR;
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

HANDLE __stdcall CreateFileA(
    LPCSTR lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile
    );

HANDLE __stdcall CreateFileW(
    LPCWSTR lpFileName,
    DWORD dwDesiredAccess,
    DWORD dwShareMode,
    LPSECURITY_ATTRIBUTES lpSecurityAttributes,
    DWORD dwCreationDisposition,
    DWORD dwFlagsAndAttributes,
    HANDLE hTemplateFile
    );

BOOL __stdcall AreFileApisANSI(void);
// CreateFile (unsuffixed): the SDK macro mapping to CreateFileW lives in the
// desktop partition; WebKit builds UNICODE so map it here.
#define CreateFile CreateFileW
DWORD __stdcall GetFileSize(
    HANDLE hFile,
    LPDWORD lpFileSizeHigh
    );

BOOL __stdcall HeapValidate(
    HANDLE hHeap,
    DWORD dwFlags,
    LPCVOID lpMem
    );

// --- _mbsrchr: multibyte reverse-char-search (mbstring.h). Absent from ARM
// ucrt.lib (only _mbschr ships); implemented in crrt.cpp, declared here so
// TUs that don't include <mbstring.h> (fontconfig fcxml.c) still compile.
unsigned char* __cdecl _mbsrchr(
    const unsigned char* string,
    unsigned int c
    );

// --- GetWindowsDirectoryA: desktop-only (DESKTOP|SYSTEM|GAMES excludes pure
// PC_APP use). Implemented in crrt.cpp (phone Windows dir is C:\Windows);
// fontconfig takes its address as a GetSystemWindowsDirectoryA fallback.
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
// keys now that storageDirectory() is temp-backed on UWP.
#define CSIDL_APPDATA 0x001a
#define CSIDL_LOCAL_APPDATA 0x001c

// --- TlHelp32 module snapshot (tlhelp32.h is desktop-partitioned out under
// PC_APP, so MODULEENTRY32/TH32CS_SNAPMODULE vanish). OpenSSL's dso_win32.c
// needs only the struct and the constant: it resolves
// CreateToolhelp32Snapshot/Module32First/Module32Next dynamically via
// GetProcAddress, so nothing here adds link dependencies. Layout mirrors the
// SDK (MAX_PATH is 260); guarded so a real tlhelp32.h still wins.
#ifndef MODULEENTRY32
#define TH32CS_SNAPMODULE 0x00000008
typedef struct tagMODULEENTRY32 {
    DWORD dwSize;
    DWORD th32ModuleID;
    DWORD th32ProcessID;
    DWORD GlblcntUsage;
    DWORD ProccntUsage;
    unsigned char* modBaseAddr;
    DWORD modBaseSize;
    HANDLE hModule;
    char szModule[256];
    char szExePath[260];
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
static __inline HANDLE CreateFileMapping(
    HANDLE hFile, LPSECURITY_ATTRIBUTES lpAttr, DWORD flProtect,
    DWORD dwMaxHigh, DWORD dwMaxLow, LPCVOID lpName)
{
    (void)hFile; (void)lpAttr; (void)flProtect;
    (void)dwMaxHigh; (void)dwMaxLow; (void)lpName;
    return (HANDLE)0;
}
#endif

#endif // WK_UWP_DESKTOP_APIS_H
