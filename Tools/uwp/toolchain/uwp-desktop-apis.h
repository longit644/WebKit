// WebKitView: desktop-partition Win32 APIs that exist in OneCoreUAP (ARM)
// but whose headers hide them under PC_APP (WINAPI_PARTITION_DESKTOP|SYSTEM).
// Plain-C declarations (no SAL: this header is force-included first).
// Types match the SDK exactly (HANDLE=void*, DWORD=unsigned long, etc.),
// so later real declarations (if any) are benign duplicates.
typedef void* HANDLE;
typedef unsigned long DWORD;
typedef int BOOL;
typedef const char* LPCSTR;
typedef const unsigned short* LPCWSTR;
struct _SECURITY_ATTRIBUTES;
typedef struct _SECURITY_ATTRIBUTES SECURITY_ATTRIBUTES, *PSECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;
typedef const void* LPCVOID;
typedef unsigned long* LPDWORD;

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

DWORD __stdcall GetFileSize(
    HANDLE hFile,
    LPDWORD lpFileSizeHigh
    );

BOOL __stdcall HeapValidate(
    HANDLE hHeap,
    DWORD dwFlags,
    LPCVOID lpMem
    );
