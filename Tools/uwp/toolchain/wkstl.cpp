// UWP/ARM32 MSVC-STL supplement (v0) for the WebKitWebViewUWP port.
// The ARM-store CRT ships no STL binary (no msvcprt): std::filesystem is
// header-implemented in MSVC's STL but calls these __std_fs_* primitives,
// plus a few math/intrin helpers. All extern "C" (link names are bare).
// Layouts/values mirror MSVC 14.44 xfilesystem_abi.h exactly (static_asserts
// below); implementations are thin App-partition Win32 wrappers.
// Only what the link demands (+ close siblings); the linker names the rest.
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <windows.h>

#include "uwp-desktop-apis.h"

// File API details missing from older AppContainer header partitions.
constexpr DWORD kDirectoryOpenFlags = FILE_FLAG_BACKUP_SEMANTICS;
constexpr DWORD kReadWriteDeleteSharing = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
constexpr DWORD kOpenExistingDisposition = OPEN_EXISTING;
constexpr DWORD kNoFileAccessRights = 0;
constexpr DWORD kNoFileCreationFlags = 0;
constexpr DWORD kNoFlagBits = 0;
constexpr int kNoConvertedCharacters = 0;
constexpr unsigned kTempPathBufferCapacity = MAX_PATH + 1;
constexpr unsigned kWriteFileAttributesAccess = FILE_WRITE_ATTRIBUTES;
constexpr DWORD kWindowsConversionNoFlags = 0;
constexpr int kConversionSizeQueryOnly = 0;
constexpr DWORD kFsctlGetReparsePoint = 0x000900A8UL;
constexpr DWORD kFsctlSetReparsePoint = 0x000900A4UL;
constexpr size_t kReparseBufferHeaderBytes = 8;
constexpr size_t kFlexibleArrayPlaceholderCount = 1;
constexpr unsigned kStlLongFilenameCapacity = MAX_PATH;
constexpr unsigned kStlShortFilenameCapacity = 14;
constexpr unsigned kFileIndexHighShift = 32;
constexpr unsigned long long kFileTimeLowMask = 0xFFFFFFFFLL;
constexpr DWORD kMoveReplaceAndCopyFlags = MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED;
constexpr DWORD kDosVolumeNameFlag = VOLUME_NAME_DOS;
constexpr DWORD kNoIoControlBufferBytes = 0;
constexpr unsigned kReparseNameByteOffsetDivisor = sizeof(wchar_t);

// --- mirrored ABI types (xfilesystem_abi.h) ---
// Every numeric value and layout below must match MSVC 14.44 exactly.
enum class __std_win_error : unsigned long {
    // No error.
    _Success = 0,

    // The requested operation is not implemented.
    _Invalid_function = 1,

    // The requested file does not exist.
    _File_not_found = 2,

    // The requested directory does not exist.
    _Path_not_found = 3,

    // Access was denied by the OS or package boundary.
    _Access_denied = 5,

    // The process could not allocate required memory.
    _Not_enough_memory = 8,

    // Directory enumeration has no further entries.
    _No_more_files = 18,

    // Another process or handle prevents the requested sharing mode.
    _Sharing_violation = 32,

    // The operation is unsupported by the UWP implementation.
    _Not_supported = 50,

    // The requested network path is unavailable.
    _Error_bad_netpath = 53,

    // The network name was removed.
    _Error_netname_deleted = 64,

    // A destination file already exists.
    _File_exists = 80,

    // An argument or path parameter is invalid.
    _Invalid_parameter = 87,

    // The caller-provided output buffer is too small.
    _Insufficient_buffer = 122,

    // The supplied filename is invalid.
    _Invalid_name = 123,

    // A directory that must be empty still contains entries.
    _Directory_not_empty = 145,

    // The requested destination already exists.
    _Already_exists = 183,

    // The filesystem path exceeds the supported length.
    _Filename_exceeds_range = 206,

    // The supplied directory name is invalid.
    _Directory_name_is_invalid = 267,

    // A reparse tag is not recognized by the filesystem wrapper.
    _Reparse_tag_invalid = 4393L,

    // Sentinel used by the MSVC STL when no specific value applies.
    _Max = ~0UL
};

// Invalid sentinel for the CRT filesystem directory handle.
// Filesystem directory-iterator handle sentinel mirrored from the STL ABI.
enum class __std_fs_dir_handle : long {
    // No live FindFirstFileW handle is stored.
    _Invalid = -1
};

// DOS-compatible filesystem attributes mirrored from MSVC's STL ABI.
enum class __std_fs_file_attr : unsigned long {
    // File is read-only.
    _Readonly = 0x00000001,

    // File is hidden from normal directory listings.
    _Hidden = 0x00000002,

    // File is marked as an operating-system component.
    _System = 0x00000004,

    // Entry is a directory.
    _Directory = 0x00000010,

    // Entry is marked for archival backup.
    _Archive = 0x00000020,

    // Reserved device attribute.
    _Device = 0x00000040,

    // No other file attributes are set.
    _Normal = 0x00000080,

    // File is intended for temporary storage.
    _Temporary = 0x00000100,

    // File is a sparse file.
    _Sparse_file = 0x00000200,

    // Entry is a reparse point.
    _Reparse_point = 0x00000400,

    // Invalid-attribute sentinel returned by filesystem queries.
    _Invalid = 0xFFFFFFFF
};

// Windows reparse tags recognized by the filesystem adapter.
enum class __std_fs_reparse_tag : unsigned long {
    // Entry is not a reparse point.
    _None = 0,

    // Entry is a mount point/junction.
    _Mount_point = 0xA0000003L,

    // Entry is a symbolic link.
    _Symlink = 0xA000000CL
};

// Two-DWORD Windows FILETIME representation mirrored by MSVC's STL ABI.
struct __std_fs_filetime {
    // Low-order FILETIME bits.
    unsigned long _Low;

    // High-order FILETIME bits.
    unsigned long _High;
};

// Directory-entry record layout expected by MSVC filesystem iterators.
struct __std_fs_find_data {
    // DOS-compatible attributes for the entry.
    __std_fs_file_attr _Attributes;

    // Entry creation time.
    __std_fs_filetime _Creation_time;

    // Most recent access time.
    __std_fs_filetime _Last_access_time;

    // Most recent write time.
    __std_fs_filetime _Last_write_time;

    // High-order file-size bits.
    unsigned long _File_size_high;

    // Low-order file-size bits.
    unsigned long _File_size_low;

    // Reparse-point tag when the entry is a link or junction.
    __std_fs_reparse_tag _Reparse_point_tag;

    // Reserved by the MSVC ABI.
    unsigned long _Reserved1;

    // Long filename from the directory query.
    wchar_t _File_name[kStlLongFilenameCapacity];

    // Optional short 8.3 filename.
    wchar_t _Short_file_name[kStlShortFilenameCapacity];
};

// Selects the optional fields requested in __std_fs_stats.
enum class __std_fs_stats_flags : unsigned long {
    // No optional stat field was requested.
    _None = 0,

    // Follow a symbolic link instead of reporting the link itself.
    _Follow_symlinks = 0x01,

    // Request file attributes.
    _Attributes = 0x02,

    // Request the reparse-point tag.
    _Reparse_tag = 0x04,

    // Request the file size.
    _File_size = 0x08,

    // Request the hard-link count.
    _Link_count = 0x10,

    // Request the last-write timestamp.
    _Last_write_time = 0x20,

    // Request every optional stat field supported by this ABI.
    _All_data = 0x3E
};

// Filesystem status payload returned to MSVC STL callers.
struct __std_fs_stats {
    // Last-write time in the STL's 100-nanosecond epoch representation.
    long long _Last_write_time;

    // Logical file size in bytes.
    unsigned long long _File_size;

    // Current DOS-compatible attributes.
    __std_fs_file_attr _Attributes;

    // Current reparse tag or _None.
    __std_fs_reparse_tag _Reparse_point_tag;

    // Number of hard links known to the filesystem.
    unsigned long _Link_count;

    // Mask identifying which requested fields were populated.
    __std_fs_stats_flags _Available;
};

// Result pair used by filesystem APIs that return a size and an error.
struct __std_ulong_and_error {
    // Computed byte count or required output size.
    unsigned long _Size;

    // Filesystem status for the operation.
    __std_win_error _Error;
};

// Code pages supported by the path conversion helpers below.
enum class __std_code_page : unsigned int {
    // Windows active ANSI code page.
    _Acp = 0,

    // UTF-8 code page identifier.
    _Utf8 = 65001
};

// Result of converting a path between narrow and wide encodings.
struct __std_fs_convert_result {
    // Converted or required output length in code units.
    int _Len;

    // Conversion status from the mirrored Win32 error enum.
    __std_win_error _Err;
};

// Copy-file policy bits mirrored from the MSVC STL ABI.
enum __std_fs_copy_options {
    // No copy behavior flags.
    _None = 0x0,

    // Mask selecting mutually exclusive existing-destination policies.
    _Existing_mask = 0xF,

    // Keep an existing destination unchanged.
    _Skip_existing = 0x1,

    // Replace an existing destination.
    _Overwrite_existing = 0x2,

    // Replace only when the source is newer.
    _Update_existing = 0x4
};

// Tests one MSVC copy option without scattering the ABI's zero sentinel.
static bool has_copy_option(__std_fs_copy_options options, __std_fs_copy_options option)
{
    return (options & option) != _None;
}

// Copy-file outcome and associated OS error.
struct __std_fs_copy_file_result {
    // True when a copy operation created or replaced the destination.
    bool _Copied;

    // OS status for the attempted operation.
    __std_win_error _Error;
};

// Filesystem equivalence result and associated OS error.
struct __std_fs_equivalent_result {
    // True when both paths identify the same filesystem object.
    bool _Equivalent;

    // OS status for the equivalence query.
    __std_win_error _Error;
};

// Directory creation outcome and associated OS error.
struct __std_fs_create_directory_result {
    // True only when a new directory was created.
    bool _Created;

    // OS status for the creation attempt.
    __std_win_error _Error;
};

// Filesystem removal outcome and associated OS error.
struct __std_fs_remove_result {
    // True when an entry was removed.
    bool _Removed;

    // OS status for the removal attempt.
    __std_win_error _Error;
};

// Expected byte layouts from MSVC 14.44 xfilesystem_abi.h.
constexpr size_t kExpectedFiletimeBytes = 8;
constexpr size_t kExpectedFindDataBytes = 592;
constexpr size_t kExpectedStatsBytes = 32;
constexpr size_t kExpectedSizeErrorBytes = 8;
constexpr size_t kExpectedConvertResultBytes = 8;
constexpr size_t kExpectedCopyResultBytes = 8;
constexpr size_t kExpectedRemoveResultBytes = 8;
static_assert(sizeof(__std_fs_filetime) == kExpectedFiletimeBytes, "FILETIME layout");
static_assert(sizeof(__std_fs_find_data) == kExpectedFindDataBytes, "WIN32_FIND_DATAW layout");
static_assert(sizeof(__std_fs_stats) == kExpectedStatsBytes, "stats layout");
static_assert(sizeof(__std_ulong_and_error) == kExpectedSizeErrorBytes, "ulong_and_error layout");
static_assert(sizeof(__std_fs_convert_result) == kExpectedConvertResultBytes, "convert_result layout");
static_assert(sizeof(__std_fs_copy_file_result) == kExpectedCopyResultBytes, "copy_file_result layout");
static_assert(sizeof(__std_fs_remove_result) == kExpectedRemoveResultBytes, "remove_result layout");

extern "C" {

// Maps Win32's thread-local last-error value to the mirrored STL enum.
static __std_win_error wk_last_error(void) { return static_cast<__std_win_error>(GetLastError()); }

// Tests whether the requested Win32 attribute/flag bit is present.
static bool wk_has_flag(unsigned long value, unsigned long flag)
{
    return (value & flag) != kNoFlagBits;
}

// Converts a narrow path to the requested wide code page; null output queries its size.
__std_fs_convert_result __stdcall __std_fs_convert_narrow_to_wide(__std_code_page _Code_page,
    const char* _Input_str, int _Input_len, wchar_t* _Output_str, int _Output_len) noexcept
{
    unsigned int cp = _Code_page == __std_code_page::_Utf8 ? CP_UTF8 : CP_ACP;
    __std_fs_convert_result r = { kNoConvertedCharacters, __std_win_error::_Success };
    int need = MultiByteToWideChar(cp, kWindowsConversionNoFlags, _Input_str, _Input_len, nullptr, kConversionSizeQueryOnly);
    if (!need) {
        r._Err = wk_last_error();
        return r;
    }
    if (!_Output_str || _Output_len == kConversionSizeQueryOnly) {
        r._Len = need; // size query
        return r;
    }
    int done = MultiByteToWideChar(cp, kWindowsConversionNoFlags, _Input_str, _Input_len, _Output_str, _Output_len);
    if (!done) {
        r._Err = wk_last_error();
        return r;
    }
    r._Len = done;
    return r;
}

// Converts a wide path to the requested narrow code page; null output queries its size.
__std_fs_convert_result __stdcall __std_fs_convert_wide_to_narrow(__std_code_page _Code_page,
    const wchar_t* _Input_str, int _Input_len, char* _Output_str, int _Output_len) noexcept
{
    unsigned int cp = _Code_page == __std_code_page::_Utf8 ? CP_UTF8 : CP_ACP;
    __std_fs_convert_result r = { kNoConvertedCharacters, __std_win_error::_Success };
    int need = WideCharToMultiByte(cp, kWindowsConversionNoFlags, _Input_str, _Input_len, nullptr, kConversionSizeQueryOnly, nullptr, nullptr);
    if (!need) {
        r._Err = wk_last_error();
        return r;
    }
    if (!_Output_str || _Output_len == kConversionSizeQueryOnly) {
        r._Len = need;
        return r;
    }
    int done = WideCharToMultiByte(cp, kWindowsConversionNoFlags, _Input_str, _Input_len, _Output_str, _Output_len, nullptr, nullptr);
    if (!done) {
        r._Err = wk_last_error();
        return r;
    }
    r._Len = done;
    return r;
}

// --- stats (single FindFirstFileW shot + link count via handle) ---
// Collects requested file attributes, size, write time and optional link count.
__std_win_error __stdcall __std_fs_get_stats(const wchar_t* _Path, __std_fs_stats* _Stats,
    __std_fs_stats_flags _Flags, __std_fs_file_attr) noexcept
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(_Path, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return wk_last_error();
    FindClose(h);
    _Stats->_Available = __std_fs_stats_flags::_None;
    _Stats->_Attributes = static_cast<__std_fs_file_attr>(fd.dwFileAttributes);
    _Stats->_Available = static_cast<__std_fs_stats_flags>(
        static_cast<unsigned long>(_Stats->_Available) | static_cast<unsigned long>(__std_fs_stats_flags::_Attributes));
    _Stats->_Reparse_point_tag = static_cast<__std_fs_reparse_tag>(fd.dwReserved0);
    _Stats->_File_size = (static_cast<unsigned long long>(fd.nFileSizeHigh) << kFileIndexHighShift) | fd.nFileSizeLow;
    _Stats->_Available = static_cast<__std_fs_stats_flags>(
        static_cast<unsigned long>(_Stats->_Available) | static_cast<unsigned long>(__std_fs_stats_flags::_File_size));
    _Stats->_Last_write_time = (static_cast<long long>(fd.ftLastWriteTime.dwHighDateTime) << kFileIndexHighShift)
        | fd.ftLastWriteTime.dwLowDateTime;
    _Stats->_Available = static_cast<__std_fs_stats_flags>(
        static_cast<unsigned long>(_Stats->_Available) | static_cast<unsigned long>(__std_fs_stats_flags::_Last_write_time));
    if ((static_cast<unsigned long>(_Flags) & static_cast<unsigned long>(__std_fs_stats_flags::_Link_count)) != static_cast<unsigned long>(__std_fs_stats_flags::_None)) {
        HANDLE fh = CreateFileW(_Path, kNoFileAccessRights, kReadWriteDeleteSharing, nullptr, kOpenExistingDisposition,
            kDirectoryOpenFlags, nullptr);
        if (fh != INVALID_HANDLE_VALUE) {
            BY_HANDLE_FILE_INFORMATION info;
            if (GetFileInformationByHandle(fh, &info)) {
                _Stats->_Link_count = info.nNumberOfLinks;
                _Stats->_Available = static_cast<__std_fs_stats_flags>(
                    static_cast<unsigned long>(_Stats->_Available)
                    | static_cast<unsigned long>(__std_fs_stats_flags::_Link_count));
            }
            CloseHandle(fh);
        }
    }
    (void)_Flags;
    return __std_win_error::_Success;
}

// --- directory iteration (find-data layout is identical: fill directly) ---
// Opens a directory enumeration and copies its first SDK-layout entry.
__std_win_error __stdcall __std_fs_directory_iterator_open(const wchar_t* _Path_spec,
    __std_fs_dir_handle* _Handle, __std_fs_find_data* _Results) noexcept
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(_Path_spec, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return wk_last_error();
    *_Handle = static_cast<__std_fs_dir_handle>(reinterpret_cast<long>(h));
    static_assert(sizeof(*_Results) == sizeof(fd), "find-data layout");
    __builtin_memcpy(_Results, &fd, sizeof(fd));
    return __std_win_error::_Success;
}

// Closes the Win32 find handle stored in the STL directory handle.
void __stdcall __std_fs_directory_iterator_close(__std_fs_dir_handle _Handle) noexcept
{
    FindClose(reinterpret_cast<HANDLE>(static_cast<long>(_Handle)));
}

// Retrieves the next directory entry, returning the thread last-error on exhaustion.
__std_win_error __stdcall __std_fs_directory_iterator_advance(
    __std_fs_dir_handle _Handle, __std_fs_find_data* _Results) noexcept
{
    WIN32_FIND_DATAW fd;
    if (!FindNextFileW(reinterpret_cast<HANDLE>(static_cast<long>(_Handle)), &fd))
        return wk_last_error();
    __builtin_memcpy(_Results, &fd, sizeof(fd));
    return __std_win_error::_Success;
}

// --- create / remove / rename / copy / links ---
// Creates a directory; an existing directory is reported as a successful no-op.
__std_fs_create_directory_result __stdcall __std_fs_create_directory(const wchar_t* _New_directory) noexcept
{
    __std_fs_create_directory_result r = { false, __std_win_error::_Success };
    if (CreateDirectoryW(_New_directory, nullptr)) {
        r._Created = true;
        return r;
    }
    __std_win_error e = wk_last_error();
    if (e == __std_win_error::_Already_exists) {
        unsigned long attrs = GetFileAttributesW(_New_directory);
        if (attrs != INVALID_FILE_ATTRIBUTES && wk_has_flag(attrs, FILE_ATTRIBUTE_DIRECTORY))
            return r; // exists as dir: not created, no error
    }
    r._Error = e;
    return r;
}

// Removes a file or empty directory; missing targets are a successful no-op.
__std_fs_remove_result __stdcall __std_fs_remove(const wchar_t* _Target) noexcept
{
    __std_fs_remove_result r = { false, __std_win_error::_Success };
    unsigned long attrs = GetFileAttributesW(_Target);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        __std_win_error e = wk_last_error();
        if (e == __std_win_error::_File_not_found || e == __std_win_error::_Path_not_found)
            return r; // missing: not removed, no error
        r._Error = e;
        return r;
    }
    bool ok = wk_has_flag(attrs, FILE_ATTRIBUTE_DIRECTORY) ? (RemoveDirectoryW(_Target) != FALSE) : (DeleteFileW(_Target) != FALSE);
    if (!ok) {
        r._Error = wk_last_error();
        return r;
    }
    r._Removed = true;
    return r;
}

// Renames or moves a path, allowing replacement and cross-volume copy fallback.
__std_win_error __stdcall __std_fs_rename(const wchar_t* _Source, const wchar_t* _Target) noexcept
{
    if (MoveFileWithProgressW(_Source, _Target, nullptr, nullptr, kMoveReplaceAndCopyFlags))
        return __std_win_error::_Success;
    return wk_last_error();
}

// Wraps CopyFileW and returns its error without changing the MSVC error mapping.
static __std_win_error wk_copy_file_impl(const wchar_t* s, const wchar_t* t, bool failIfExists)
{
    if (CopyFileW(s, t, failIfExists ? TRUE : FALSE))
        return __std_win_error::_Success;
    return wk_last_error();
}

// Applies the requested existing-destination policy and copies one file.
__std_fs_copy_file_result __stdcall __std_fs_copy_file(
    const wchar_t* _Source, const wchar_t* _Target, __std_fs_copy_options _Options) noexcept
{
    __std_fs_copy_file_result r = { false, __std_win_error::_Success };
    if (has_copy_option(_Options, __std_fs_copy_options::_Skip_existing)) {
        if (GetFileAttributesW(_Target) != INVALID_FILE_ATTRIBUTES)
            return r; // skipped, no error
    } else if (has_copy_option(_Options, __std_fs_copy_options::_Update_existing)) {
        WIN32_FILE_ATTRIBUTE_DATA sfd, tfd;
        if (GetFileAttributesExW(_Target, GetFileExInfoStandard, &tfd)
            && GetFileAttributesExW(_Source, GetFileExInfoStandard, &sfd)) {
            unsigned long long st = (static_cast<unsigned long long>(sfd.ftLastWriteTime.dwHighDateTime) << kFileIndexHighShift)
                | sfd.ftLastWriteTime.dwLowDateTime;
            unsigned long long tt = (static_cast<unsigned long long>(tfd.ftLastWriteTime.dwHighDateTime) << kFileIndexHighShift)
                | tfd.ftLastWriteTime.dwLowDateTime;
            if (tt >= st)
                return r; // target newer: skipped, no error
        }
        r._Error = wk_copy_file_impl(_Source, _Target, false);
    } else {
        bool failIfExists = !has_copy_option(_Options, __std_fs_copy_options::_Overwrite_existing);
        r._Error = wk_copy_file_impl(_Source, _Target, failIfExists);
    }
    if (r._Error == __std_win_error::_Success)
        r._Copied = true;
    return r;
}

// Creates a hard link to an existing file.
__std_win_error __stdcall __std_fs_create_hard_link(
    const wchar_t* _File_name, const wchar_t* _Existing_file_name) noexcept
{
    if (CreateHardLinkW(_File_name, _Existing_file_name, nullptr))
        return __std_win_error::_Success;
    return wk_last_error();
}

// Symbolic links are unavailable on this ARM Store port; returns not-supported.
__std_win_error __stdcall __std_fs_create_symbolic_link(
    const wchar_t*, const wchar_t*) noexcept
{
    // No linkable CreateSymbolicLinkW on ARM-store (needs desktop + privilege).
    return __std_win_error::_Not_supported;
}

// Directory symbolic links are unavailable on this ARM Store port.
__std_win_error __stdcall __std_fs_create_directory_symbolic_link(
    const wchar_t*, const wchar_t*) noexcept
{
    return __std_win_error::_Not_supported;
}

// Compares volume serial and file-index identity for two paths.
__std_fs_equivalent_result __stdcall __std_fs_equivalent(const wchar_t* _Path1, const wchar_t* _Path2) noexcept
{
    __std_fs_equivalent_result r = { false, __std_win_error::_Success };
    HANDLE h1 = CreateFileW(_Path1, kNoFileAccessRights, kReadWriteDeleteSharing, nullptr, kOpenExistingDisposition, kDirectoryOpenFlags, nullptr);
    if (h1 == INVALID_HANDLE_VALUE) {
        r._Error = wk_last_error();
        return r;
    }
    HANDLE h2 = CreateFileW(_Path2, kNoFileAccessRights, kReadWriteDeleteSharing, nullptr, kOpenExistingDisposition, kDirectoryOpenFlags, nullptr);
    if (h2 == INVALID_HANDLE_VALUE) {
        r._Error = wk_last_error();
        CloseHandle(h1);
        return r;
    }
    BY_HANDLE_FILE_INFORMATION i1, i2;
    if (GetFileInformationByHandle(h1, &i1) && GetFileInformationByHandle(h2, &i2)) {
        r._Equivalent = i1.dwVolumeSerialNumber == i2.dwVolumeSerialNumber
            && i1.nFileIndexHigh == i2.nFileIndexHigh && i1.nFileIndexLow == i2.nFileIndexLow;
    } else
        r._Error = wk_last_error();
    CloseHandle(h2);
    CloseHandle(h1);
    return r;
}

// --- handle-based ops + reparse points ---
// Selects the path prefix emitted by GetFinalPathNameByHandleW.
enum class __std_fs_volume_name_kind : unsigned long {
    // Return a DOS-style path such as C:\path.
    _Dos = 0,

    // Return a volume-GUID path.
    _Guid = 1,

    // Return an NT device path.
    _Nt = 2,

    // Omit volume-name information.
    _None = 4
};

// File access-mask values consumed by __std_fs_open_handle.
enum class __std_access_rights : unsigned long {
    // Delete access required for filesystem removal.
    _Delete = 0x00010000,

    // Read file attributes from an open handle.
    _File_read_attributes = 0x0080,

    // Set file attributes or timestamps on an open handle.
    _File_write_attributes = 0x0100,

    // Composite generic write access required by MSVC's ABI.
    _File_generic_write = 0x00120116
};

// CreateFileW flags supported by the MSVC filesystem adapter.
enum class __std_fs_file_flags : unsigned long {
    // Open without special file flags.
    _None = 0,

    // Permit opening a directory handle.
    _Backup_semantics = 0x02000000,

    // Open the reparse point itself rather than following it.
    _Open_reparse_point = 0x00200000
};

// Filesystem file-handle sentinel mirrored from the STL ABI.
enum class __std_fs_file_handle : long {
    // No live CreateFileW handle is stored.
    _Invalid = -1
};

// Variable-sized reparse buffer layout required by FSCTL_GET/SET_REPARSE_POINT.
struct __std_fs_reparse_data_buffer {
    // Tag selecting symbolic-link, mount-point or generic payload format.
    unsigned long _Reparse_tag;

    // Payload byte count following the fixed header.
    unsigned short _Reparse_data_length;

    // Reserved header field required by the Win32 ABI.
    unsigned short _Reserved;
    union {
        struct {
            // Byte offsets and lengths of substitute and display names.
            unsigned short _Substitute_name_offset;
            unsigned short _Substitute_name_length;
            unsigned short _Print_name_offset;
            unsigned short _Print_name_length;
            unsigned long _Flags;
            // Names are stored consecutively in this trailing buffer.
            wchar_t _Path_buffer[kFlexibleArrayPlaceholderCount];
        } _Symbolic_link_reparse_buffer;
        struct {
            // Byte offsets and lengths of substitute and display names.
            unsigned short _Substitute_name_offset;
            unsigned short _Substitute_name_length;
            unsigned short _Print_name_offset;
            unsigned short _Print_name_length;
            // Names are stored consecutively in this trailing buffer.
            wchar_t _Path_buffer[kFlexibleArrayPlaceholderCount];
        } _Mount_point_reparse_buffer;
        struct {
            // Opaque payload for tags not represented by the named variants.
            unsigned char _Data_buffer[kFlexibleArrayPlaceholderCount];
        } _Generic_reparse_buffer;
    };
};

// Opens a filesystem handle with only the caller-requested access and flags.
__std_win_error __stdcall __std_fs_open_handle(__std_fs_file_handle* _Handle,
    const wchar_t* _File_name, __std_access_rights _Desired_access, __std_fs_file_flags _Flags) noexcept
{
    // FILE_GENERIC_WRITE shares bits with narrower rights; a bitwise-any
    // test accidentally escalates a READ_ATTRIBUTES request to GENERIC_WRITE.
    unsigned long access = static_cast<unsigned long>(_Desired_access);
    unsigned long flags = static_cast<unsigned long>(__std_fs_file_flags::_None);
    unsigned long ff = static_cast<unsigned long>(_Flags);
    if (wk_has_flag(ff, static_cast<unsigned long>(__std_fs_file_flags::_Backup_semantics)))
        flags |= static_cast<unsigned long>(__std_fs_file_flags::_Backup_semantics);
    if (wk_has_flag(ff, static_cast<unsigned long>(__std_fs_file_flags::_Open_reparse_point)))
        flags |= static_cast<unsigned long>(__std_fs_file_flags::_Open_reparse_point);
    HANDLE h = CreateFileW(_File_name, access, kReadWriteDeleteSharing, nullptr, kOpenExistingDisposition, flags, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return wk_last_error();
    *_Handle = static_cast<__std_fs_file_handle>(reinterpret_cast<long>(h));
    return __std_win_error::_Success;
}

// Closes the Win32 HANDLE stored in the MSVC file-handle wrapper.
void __stdcall __std_fs_close_handle(__std_fs_file_handle _Handle) noexcept
{
    CloseHandle(reinterpret_cast<HANDLE>(static_cast<long>(_Handle)));
}

// Retrieves DOS-compatible attributes from an already-open filesystem handle.
__std_win_error __stdcall __std_fs_get_file_attributes_by_handle(
    __std_fs_file_handle _Handle, unsigned long* _File_attributes) noexcept
{
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(reinterpret_cast<HANDLE>(static_cast<long>(_Handle)), &info))
        return wk_last_error();
    *_File_attributes = info.dwFileAttributes;
    return __std_win_error::_Success;
}

// Resolves an open handle to a DOS-style final path in the caller's buffer.
__std_ulong_and_error __stdcall __std_fs_get_final_path_name_by_handle(__std_fs_file_handle _Handle,
    wchar_t* _Target, unsigned long _Target_size, __std_fs_volume_name_kind) noexcept
{
    __std_ulong_and_error r = { 0, __std_win_error::_Success };
    unsigned long n = GetFinalPathNameByHandleW(
        reinterpret_cast<HANDLE>(static_cast<long>(_Handle)), _Target, _Target_size, kDosVolumeNameFlag);
    if (!n || n >= _Target_size) {
        r._Error = !n ? wk_last_error() : __std_win_error::_Filename_exceeds_range;
        return r;
    }
    r._Size = n;
    return r;
}

// Reads the variable-sized reparse payload using FSCTL_GET_REPARSE_POINT.
__std_win_error __stdcall __std_fs_read_reparse_data_buffer(__std_fs_file_handle _Handle,
    void* _Buffer, unsigned long _Buffer_size) noexcept
{
    unsigned long returned = 0;
    if (!DeviceIoControl(reinterpret_cast<HANDLE>(static_cast<long>(_Handle)),
            kFsctlGetReparsePoint, nullptr, kNoIoControlBufferBytes, _Buffer, _Buffer_size, &returned, nullptr))
        return wk_last_error();
    return __std_win_error::_Success;
}

// Writes a reparse payload using FSCTL_SET_REPARSE_POINT.
__std_win_error __stdcall __std_fs_write_reparse_data_buffer(
    __std_fs_file_handle _Handle, const __std_fs_reparse_data_buffer* _Buffer) noexcept
{
    unsigned long code = kFsctlSetReparsePoint;
    unsigned long returned = 0;
    unsigned long size = kReparseBufferHeaderBytes + _Buffer->_Reparse_data_length;
    if (!DeviceIoControl(reinterpret_cast<HANDLE>(static_cast<long>(_Handle)), code,
            (void*)_Buffer, size, nullptr, kNoIoControlBufferBytes, &returned, nullptr))
        return wk_last_error();
    return __std_win_error::_Success;
}

// Tests whether a reparse payload is a mount-point/junction.
bool __stdcall __std_fs_is_junction_from_reparse_data_buffer(
    const __std_fs_reparse_data_buffer* _Buffer) noexcept
{
    return _Buffer && _Buffer->_Reparse_tag == static_cast<unsigned long>(__std_fs_reparse_tag::_Mount_point);
}

// Returns the display-name span for a symbolic link or mount point.
__std_win_error __stdcall __std_fs_read_name_from_reparse_data_buffer(
    __std_fs_reparse_data_buffer* _Buffer, wchar_t** _Offset, unsigned short* _Length) noexcept
{
    if (!_Buffer)
        return __std_win_error::_Invalid_parameter;
    if (_Buffer->_Reparse_tag == static_cast<unsigned long>(__std_fs_reparse_tag::_Symlink)) {
        *_Offset = _Buffer->_Symbolic_link_reparse_buffer._Path_buffer
            + _Buffer->_Symbolic_link_reparse_buffer._Print_name_offset / kReparseNameByteOffsetDivisor;
        *_Length = _Buffer->_Symbolic_link_reparse_buffer._Print_name_length;
    } else if (_Buffer->_Reparse_tag == static_cast<unsigned long>(__std_fs_reparse_tag::_Mount_point)) {
        *_Offset = _Buffer->_Mount_point_reparse_buffer._Path_buffer
            + _Buffer->_Mount_point_reparse_buffer._Print_name_offset / kReparseNameByteOffsetDivisor;
        *_Length = _Buffer->_Mount_point_reparse_buffer._Print_name_length;
    } else
        return __std_win_error::_Reparse_tag_invalid;
    return __std_win_error::_Success;
}

// --- times / space / paths ---
// Applies the supplied FILETIME to a path using write-attributes access.
__std_win_error __stdcall __std_fs_set_last_write_time(long long _Last_write_filetime, const wchar_t* _Path) noexcept
{
    HANDLE h = CreateFileW(_Path, kWriteFileAttributesAccess, kReadWriteDeleteSharing, nullptr, kOpenExistingDisposition, kNoFileCreationFlags, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return wk_last_error();
    FILETIME ft;
    ft.dwLowDateTime = (unsigned long)(_Last_write_filetime & kFileTimeLowMask);
    ft.dwHighDateTime = (unsigned long)((_Last_write_filetime >> kFileIndexHighShift) & kFileTimeLowMask);
    __std_win_error e = SetFileTime(h, nullptr, nullptr, &ft) ? __std_win_error::_Success : wk_last_error();
    CloseHandle(h);
    return e;
}

// Returns total, free and caller-available disk space for a target path.
__std_win_error __stdcall __std_fs_space(const wchar_t* _Target, unsigned long long* _Available,
    unsigned long long* _Total_bytes, unsigned long long* _Free_bytes) noexcept
{
    ULARGE_INTEGER avail, total, freeb;
    if (!GetDiskFreeSpaceExW(_Target, &avail, &total, &freeb))
        return wk_last_error();
    *_Available = avail.QuadPart;
    *_Total_bytes = total.QuadPart;
    *_Free_bytes = freeb.QuadPart;
    return __std_win_error::_Success;
}

// Copies GetTempPathW output into the MSVC size/error result pair.
__std_ulong_and_error __stdcall __std_fs_get_temp_path(wchar_t* _Target) noexcept
{
    __std_ulong_and_error r = { 0, __std_win_error::_Success };
    unsigned long n = GetTempPathW(kTempPathBufferCapacity, _Target);
    if (!n || n > kTempPathBufferCapacity) {
        r._Error = !n ? wk_last_error() : __std_win_error::_Filename_exceeds_range;
        return r;
    }
    r._Size = n;
    return r;
}

// Copies GetCurrentDirectoryW output into the caller's sized buffer.
__std_ulong_and_error __stdcall __std_fs_get_current_path(
    unsigned long _Target_size, wchar_t* _Target) noexcept
{
    __std_ulong_and_error r = { 0, __std_win_error::_Success };
    unsigned long n = GetCurrentDirectoryW(_Target_size, _Target);
    if (!n || n >= _Target_size) {
        r._Error = !n ? wk_last_error() : __std_win_error::_Filename_exceeds_range;
        return r;
    }
    r._Size = n;
    return r;
}

// --- math helpers (xmath declares them plain: vcruntime pieces) ---
// Computes the 3D vector magnitude using nested hypot scaling.
double __stdcall __std_smf_hypot3(double _X, double _Y, double _Z) noexcept
{
    // v0: nested hypot; overflow-scaling refinements omitted.
    return hypot(hypot(_X, _Y), _Z);
}

// Float-precision form of the 3D vector magnitude helper.
float __stdcall __std_smf_hypot3f(float _X, float _Y, float _Z) noexcept
{
    return hypotf(hypotf(_X, _Y), _Z);
}

// ARM32 count-leading-zeros are __MACHINEZ (lib calls) here, not intrinsics.
unsigned int __cdecl _CountLeadingZeros(unsigned long _Mask) noexcept
{
    constexpr unsigned kUnsignedLongBitCount = sizeof(unsigned long) * CHAR_BIT;
    return _Mask ? (unsigned int)__builtin_clz(_Mask) : kUnsignedLongBitCount;
}

// Returns the number of leading zero bits in a 64-bit word.
unsigned int __cdecl _CountLeadingZeros64(unsigned long long _Mask) noexcept
{
    constexpr unsigned kUnsignedLongLongBitCount = sizeof(unsigned long long) * CHAR_BIT;
    return _Mask ? (unsigned int)__builtin_clzll(_Mask) : kUnsignedLongLongBitCount;
}

} // extern "C"
