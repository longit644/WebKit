// UWP/ARM32 MSVC-STL supplement (v0) for the WebKitWebViewUWP port.
// The ARM-store CRT ships no STL binary (no msvcprt): std::filesystem is
// header-implemented in MSVC's STL but calls these __std_fs_* primitives,
// plus a few math/intrin helpers. All extern "C" (link names are bare).
// Layouts/values mirror MSVC 14.44 xfilesystem_abi.h exactly (static_asserts
// below); implementations are thin App-partition Win32 wrappers.
// Only what the link demands (+ close siblings); the linker names the rest.
#include <windows.h>
#include <math.h>
#include <stdint.h>

#include "uwp-desktop-apis.h"

// --- mirrored ABI types (xfilesystem_abi.h) ---
enum class __std_win_error : unsigned long {
    _Success = 0,
    _Invalid_function = 1,
    _File_not_found = 2,
    _Path_not_found = 3,
    _Access_denied = 5,
    _Not_enough_memory = 8,
    _No_more_files = 18,
    _Sharing_violation = 32,
    _Not_supported = 50,
    _Error_bad_netpath = 53,
    _Error_netname_deleted = 64,
    _File_exists = 80,
    _Invalid_parameter = 87,
    _Insufficient_buffer = 122,
    _Invalid_name = 123,
    _Directory_not_empty = 145,
    _Already_exists = 183,
    _Filename_exceeds_range = 206,
    _Directory_name_is_invalid = 267,
    _Reparse_tag_invalid = 4393L,
    _Max = ~0UL
};

enum class __std_fs_dir_handle : long { _Invalid = -1 };

enum class __std_fs_file_attr : unsigned long {
    _Readonly = 0x00000001,
    _Hidden = 0x00000002,
    _System = 0x00000004,
    _Directory = 0x00000010,
    _Archive = 0x00000020,
    _Device = 0x00000040,
    _Normal = 0x00000080,
    _Temporary = 0x00000100,
    _Sparse_file = 0x00000200,
    _Reparse_point = 0x00000400,
    _Invalid = 0xFFFFFFFF
};

enum class __std_fs_reparse_tag : unsigned long {
    _None = 0,
    _Mount_point = 0xA0000003L,
    _Symlink = 0xA000000CL
};

struct __std_fs_filetime {
    unsigned long _Low;
    unsigned long _High;
};

struct __std_fs_find_data {
    __std_fs_file_attr _Attributes;
    __std_fs_filetime _Creation_time;
    __std_fs_filetime _Last_access_time;
    __std_fs_filetime _Last_write_time;
    unsigned long _File_size_high;
    unsigned long _File_size_low;
    __std_fs_reparse_tag _Reparse_point_tag;
    unsigned long _Reserved1;
    wchar_t _File_name[260];
    wchar_t _Short_file_name[14];
};

enum class __std_fs_stats_flags : unsigned long {
    _None = 0,
    _Follow_symlinks = 0x01,
    _Attributes = 0x02,
    _Reparse_tag = 0x04,
    _File_size = 0x08,
    _Link_count = 0x10,
    _Last_write_time = 0x20,
    _All_data = 0x3E
};

struct __std_fs_stats {
    long long _Last_write_time;
    unsigned long long _File_size;
    __std_fs_file_attr _Attributes;
    __std_fs_reparse_tag _Reparse_point_tag;
    unsigned long _Link_count;
    __std_fs_stats_flags _Available;
};

struct __std_ulong_and_error {
    unsigned long _Size;
    __std_win_error _Error;
};

enum class __std_code_page : unsigned int { _Acp = 0, _Utf8 = 65001 };

struct __std_fs_convert_result {
    int _Len;
    __std_win_error _Err;
};

enum __std_fs_copy_options {
    _None = 0x0,
    _Existing_mask = 0xF,
    _Skip_existing = 0x1,
    _Overwrite_existing = 0x2,
    _Update_existing = 0x4
};

struct __std_fs_copy_file_result {
    bool _Copied;
    __std_win_error _Error;
};

struct __std_fs_equivalent_result {
    bool _Equivalent;
    __std_win_error _Error;
};

struct __std_fs_create_directory_result {
    bool _Created;
    __std_win_error _Error;
};

struct __std_fs_remove_result {
    bool _Removed;
    __std_win_error _Error;
};

static_assert(sizeof(__std_fs_filetime) == 8, "FILETIME layout");
static_assert(sizeof(__std_fs_find_data) == 592, "WIN32_FIND_DATAW layout");
static_assert(sizeof(__std_fs_stats) == 32, "stats layout");
static_assert(sizeof(__std_ulong_and_error) == 8, "ulong_and_error layout");
static_assert(sizeof(__std_fs_convert_result) == 8, "convert_result layout");
static_assert(sizeof(__std_fs_copy_file_result) == 8, "copy_file_result layout");
static_assert(sizeof(__std_fs_remove_result) == 8, "remove_result layout");

extern "C" {

static __std_win_error wk_last_error(void) { return static_cast<__std_win_error>(GetLastError()); }

// --- code-page conversions ---
__std_fs_convert_result __stdcall __std_fs_convert_narrow_to_wide(__std_code_page _Code_page,
    const char* _Input_str, int _Input_len, wchar_t* _Output_str, int _Output_len) noexcept
{
    unsigned int cp = _Code_page == __std_code_page::_Utf8 ? 65001U : 0U;
    __std_fs_convert_result r = { 0, __std_win_error::_Success };
    int need = MultiByteToWideChar(cp, 0, _Input_str, _Input_len, nullptr, 0);
    if (need == 0) {
        r._Err = wk_last_error();
        return r;
    }
    if (!_Output_str || _Output_len == 0) {
        r._Len = need; // size query
        return r;
    }
    int done = MultiByteToWideChar(cp, 0, _Input_str, _Input_len, _Output_str, _Output_len);
    if (done == 0) {
        r._Err = wk_last_error();
        return r;
    }
    r._Len = done;
    return r;
}

__std_fs_convert_result __stdcall __std_fs_convert_wide_to_narrow(__std_code_page _Code_page,
    const wchar_t* _Input_str, int _Input_len, char* _Output_str, int _Output_len) noexcept
{
    unsigned int cp = _Code_page == __std_code_page::_Utf8 ? 65001U : 0U;
    __std_fs_convert_result r = { 0, __std_win_error::_Success };
    int need = WideCharToMultiByte(cp, 0, _Input_str, _Input_len, nullptr, 0, nullptr, nullptr);
    if (need == 0) {
        r._Err = wk_last_error();
        return r;
    }
    if (!_Output_str || _Output_len == 0) {
        r._Len = need;
        return r;
    }
    int done = WideCharToMultiByte(cp, 0, _Input_str, _Input_len, _Output_str, _Output_len, nullptr, nullptr);
    if (done == 0) {
        r._Err = wk_last_error();
        return r;
    }
    r._Len = done;
    return r;
}

// --- stats (single FindFirstFileW shot + link count via handle) ---
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
    _Stats->_File_size = (static_cast<unsigned long long>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
    _Stats->_Available = static_cast<__std_fs_stats_flags>(
        static_cast<unsigned long>(_Stats->_Available) | static_cast<unsigned long>(__std_fs_stats_flags::_File_size));
    _Stats->_Last_write_time = (static_cast<long long>(fd.ftLastWriteTime.dwHighDateTime) << 32)
        | fd.ftLastWriteTime.dwLowDateTime;
    _Stats->_Available = static_cast<__std_fs_stats_flags>(
        static_cast<unsigned long>(_Stats->_Available) | static_cast<unsigned long>(__std_fs_stats_flags::_Last_write_time));
    if ((static_cast<unsigned long>(_Flags) & static_cast<unsigned long>(__std_fs_stats_flags::_Link_count)) != 0) {
        HANDLE fh = CreateFileW(_Path, 0, 7 /*RWE share*/, nullptr, 3 /*OPEN_EXISTING*/,
            0x02000000 /*BACKUP_SEMANTICS for dirs*/, nullptr);
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

void __stdcall __std_fs_directory_iterator_close(__std_fs_dir_handle _Handle) noexcept
{
    FindClose(reinterpret_cast<HANDLE>(static_cast<long>(_Handle)));
}

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
        if (attrs != 0xFFFFFFFFUL && (attrs & 0x10 /*DIRECTORY*/) != 0)
            return r; // exists as dir: not created, no error
    }
    r._Error = e;
    return r;
}

__std_fs_remove_result __stdcall __std_fs_remove(const wchar_t* _Target) noexcept
{
    __std_fs_remove_result r = { false, __std_win_error::_Success };
    unsigned long attrs = GetFileAttributesW(_Target);
    if (attrs == 0xFFFFFFFFUL) {
        __std_win_error e = wk_last_error();
        if (e == __std_win_error::_File_not_found || e == __std_win_error::_Path_not_found)
            return r; // missing: not removed, no error
        r._Error = e;
        return r;
    }
    bool ok = ((attrs & 0x10 /*DIRECTORY*/) != 0) ? (RemoveDirectoryW(_Target) != 0) : (DeleteFileW(_Target) != 0);
    if (!ok) {
        r._Error = wk_last_error();
        return r;
    }
    r._Removed = true;
    return r;
}

__std_win_error __stdcall __std_fs_rename(const wchar_t* _Source, const wchar_t* _Target) noexcept
{
    if (MoveFileWithProgressW(_Source, _Target, nullptr, nullptr, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED))
        return __std_win_error::_Success;
    return wk_last_error();
}

static __std_win_error wk_copy_file_impl(const wchar_t* s, const wchar_t* t, bool failIfExists)
{
    if (CopyFileW(s, t, failIfExists ? 1 : 0))
        return __std_win_error::_Success;
    return wk_last_error();
}

__std_fs_copy_file_result __stdcall __std_fs_copy_file(
    const wchar_t* _Source, const wchar_t* _Target, __std_fs_copy_options _Options) noexcept
{
    __std_fs_copy_file_result r = { false, __std_win_error::_Success };
    if ((_Options & __std_fs_copy_options::_Skip_existing) != 0) {
        if (GetFileAttributesW(_Target) != 0xFFFFFFFFUL)
            return r; // skipped, no error
    } else if ((_Options & __std_fs_copy_options::_Update_existing) != 0) {
        WIN32_FILE_ATTRIBUTE_DATA sfd, tfd;
        if (GetFileAttributesExW(_Target, GetFileExInfoStandard, &tfd)
            && GetFileAttributesExW(_Source, GetFileExInfoStandard, &sfd)) {
            unsigned long long st = (static_cast<unsigned long long>(sfd.ftLastWriteTime.dwHighDateTime) << 32)
                | sfd.ftLastWriteTime.dwLowDateTime;
            unsigned long long tt = (static_cast<unsigned long long>(tfd.ftLastWriteTime.dwHighDateTime) << 32)
                | tfd.ftLastWriteTime.dwLowDateTime;
            if (tt >= st)
                return r; // target newer: skipped, no error
        }
        r._Error = wk_copy_file_impl(_Source, _Target, false);
    } else {
        bool failIfExists = (_Options & __std_fs_copy_options::_Overwrite_existing) == 0;
        r._Error = wk_copy_file_impl(_Source, _Target, failIfExists);
    }
    if (r._Error == __std_win_error::_Success)
        r._Copied = true;
    return r;
}

__std_win_error __stdcall __std_fs_create_hard_link(
    const wchar_t* _File_name, const wchar_t* _Existing_file_name) noexcept
{
    if (CreateHardLinkW(_File_name, _Existing_file_name, nullptr))
        return __std_win_error::_Success;
    return wk_last_error();
}

__std_win_error __stdcall __std_fs_create_symbolic_link(
    const wchar_t*, const wchar_t*) noexcept
{
    // No linkable CreateSymbolicLinkW on ARM-store (needs desktop + privilege).
    (void)0;
    return __std_win_error::_Not_supported;
}

__std_win_error __stdcall __std_fs_create_directory_symbolic_link(
    const wchar_t*, const wchar_t*) noexcept
{
    (void)0;
    return __std_win_error::_Not_supported;
}

__std_fs_equivalent_result __stdcall __std_fs_equivalent(const wchar_t* _Path1, const wchar_t* _Path2) noexcept
{
    __std_fs_equivalent_result r = { false, __std_win_error::_Success };
    HANDLE h1 = CreateFileW(_Path1, 0, 7, nullptr, 3, 0x02000000, nullptr);
    if (h1 == INVALID_HANDLE_VALUE) {
        r._Error = wk_last_error();
        return r;
    }
    HANDLE h2 = CreateFileW(_Path2, 0, 7, nullptr, 3, 0x02000000, nullptr);
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
enum class __std_fs_volume_name_kind : unsigned long { _Dos = 0, _Guid = 1, _Nt = 2, _None = 4 };
enum class __std_access_rights : unsigned long {
    _Delete = 0x00010000,
    _File_read_attributes = 0x0080,
    _File_write_attributes = 0x0100,
    _File_generic_write = 0x00120116
};
enum class __std_fs_file_flags : unsigned long {
    _None = 0,
    _Backup_semantics = 0x02000000,
    _Open_reparse_point = 0x00200000
};
enum class __std_fs_file_handle : long { _Invalid = -1 };

struct __std_fs_reparse_data_buffer {
    unsigned long _Reparse_tag;
    unsigned short _Reparse_data_length;
    unsigned short _Reserved;
    union {
        struct {
            unsigned short _Substitute_name_offset;
            unsigned short _Substitute_name_length;
            unsigned short _Print_name_offset;
            unsigned short _Print_name_length;
            unsigned long _Flags;
            wchar_t _Path_buffer[1];
        } _Symbolic_link_reparse_buffer;
        struct {
            unsigned short _Substitute_name_offset;
            unsigned short _Substitute_name_length;
            unsigned short _Print_name_offset;
            unsigned short _Print_name_length;
            wchar_t _Path_buffer[1];
        } _Mount_point_reparse_buffer;
        struct {
            unsigned char _Data_buffer[1];
        } _Generic_reparse_buffer;
    };
};

__std_win_error __stdcall __std_fs_open_handle(__std_fs_file_handle* _Handle,
    const wchar_t* _File_name, __std_access_rights _Desired_access, __std_fs_file_flags _Flags) noexcept
{
    // FILE_GENERIC_WRITE shares bits with narrower rights; a bitwise-any
    // test accidentally escalates a READ_ATTRIBUTES request to GENERIC_WRITE.
    unsigned long access = static_cast<unsigned long>(_Desired_access);
    unsigned long flags = 0;
    unsigned long ff = static_cast<unsigned long>(_Flags);
    if ((ff & 0x02000000UL /*BACKUP_SEMANTICS*/) != 0)
        flags |= 0x02000000UL;
    if ((ff & 0x00200000UL /*OPEN_REPARSE_POINT*/) != 0)
        flags |= 0x00200000UL;
    HANDLE h = CreateFileW(_File_name, access, 7, nullptr, 3 /*OPEN_EXISTING*/, flags, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return wk_last_error();
    *_Handle = static_cast<__std_fs_file_handle>(reinterpret_cast<long>(h));
    return __std_win_error::_Success;
}

void __stdcall __std_fs_close_handle(__std_fs_file_handle _Handle) noexcept
{
    CloseHandle(reinterpret_cast<HANDLE>(static_cast<long>(_Handle)));
}

__std_win_error __stdcall __std_fs_get_file_attributes_by_handle(
    __std_fs_file_handle _Handle, unsigned long* _File_attributes) noexcept
{
    BY_HANDLE_FILE_INFORMATION info;
    if (!GetFileInformationByHandle(reinterpret_cast<HANDLE>(static_cast<long>(_Handle)), &info))
        return wk_last_error();
    *_File_attributes = info.dwFileAttributes;
    return __std_win_error::_Success;
}

__std_ulong_and_error __stdcall __std_fs_get_final_path_name_by_handle(__std_fs_file_handle _Handle,
    wchar_t* _Target, unsigned long _Target_size, __std_fs_volume_name_kind) noexcept
{
    __std_ulong_and_error r = { 0, __std_win_error::_Success };
    unsigned long n = GetFinalPathNameByHandleW(
        reinterpret_cast<HANDLE>(static_cast<long>(_Handle)), _Target, _Target_size, 0 /*DOS*/);
    if (n == 0 || n >= _Target_size) {
        r._Error = n == 0 ? wk_last_error() : __std_win_error::_Filename_exceeds_range;
        return r;
    }
    r._Size = n;
    return r;
}

__std_win_error __stdcall __std_fs_read_reparse_data_buffer(__std_fs_file_handle _Handle,
    void* _Buffer, unsigned long _Buffer_size) noexcept
{
    unsigned long returned = 0;
    if (!DeviceIoControl(reinterpret_cast<HANDLE>(static_cast<long>(_Handle)),
            0x000900A8UL /*FSCTL_GET_REPARSE_POINT*/, nullptr, 0, _Buffer, _Buffer_size, &returned, nullptr))
        return wk_last_error();
    return __std_win_error::_Success;
}

__std_win_error __stdcall __std_fs_write_reparse_data_buffer(
    __std_fs_file_handle _Handle, const __std_fs_reparse_data_buffer* _Buffer) noexcept
{
    unsigned long tag = _Buffer->_Reparse_tag;
    unsigned long code = (tag == 0xA000000CL /*SYMLINK*/) ? 0x000900A4UL /*SET_REPARSE_POINT*/ : 0x000900A4UL;
    unsigned long returned = 0;
    unsigned long size = 8 + _Buffer->_Reparse_data_length;
    if (!DeviceIoControl(reinterpret_cast<HANDLE>(static_cast<long>(_Handle)), code,
            (void*)_Buffer, size, nullptr, 0, &returned, nullptr))
        return wk_last_error();
    return __std_win_error::_Success;
}

bool __stdcall __std_fs_is_junction_from_reparse_data_buffer(
    const __std_fs_reparse_data_buffer* _Buffer) noexcept
{
    return _Buffer && _Buffer->_Reparse_tag == 0xA0000003UL /*MOUNT_POINT*/;
}

__std_win_error __stdcall __std_fs_read_name_from_reparse_data_buffer(
    __std_fs_reparse_data_buffer* _Buffer, wchar_t** _Offset, unsigned short* _Length) noexcept
{
    if (!_Buffer)
        return __std_win_error::_Invalid_parameter;
    if (_Buffer->_Reparse_tag == 0xA000000CL /*SYMLINK*/) {
        *_Offset = _Buffer->_Symbolic_link_reparse_buffer._Path_buffer
            + _Buffer->_Symbolic_link_reparse_buffer._Print_name_offset / 2;
        *_Length = _Buffer->_Symbolic_link_reparse_buffer._Print_name_length;
    } else if (_Buffer->_Reparse_tag == 0xA0000003UL /*MOUNT_POINT*/) {
        *_Offset = _Buffer->_Mount_point_reparse_buffer._Path_buffer
            + _Buffer->_Mount_point_reparse_buffer._Print_name_offset / 2;
        *_Length = _Buffer->_Mount_point_reparse_buffer._Print_name_length;
    } else
        return __std_win_error::_Reparse_tag_invalid;
    return __std_win_error::_Success;
}

// --- times / space / paths ---
__std_win_error __stdcall __std_fs_set_last_write_time(long long _Last_write_filetime, const wchar_t* _Path) noexcept
{
    HANDLE h = CreateFileW(_Path, 0x0100 /*WRITE_ATTRIBUTES*/, 7, nullptr, 3, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return wk_last_error();
    FILETIME ft;
    ft.dwLowDateTime = (unsigned long)(_Last_write_filetime & 0xFFFFFFFFLL);
    ft.dwHighDateTime = (unsigned long)((_Last_write_filetime >> 32) & 0xFFFFFFFFLL);
    __std_win_error e = SetFileTime(h, nullptr, nullptr, &ft) ? __std_win_error::_Success : wk_last_error();
    CloseHandle(h);
    return e;
}

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

__std_ulong_and_error __stdcall __std_fs_get_temp_path(wchar_t* _Target) noexcept
{
    __std_ulong_and_error r = { 0, __std_win_error::_Success };
    unsigned long n = GetTempPathW(261, _Target);
    if (n == 0 || n > 261) {
        r._Error = n == 0 ? wk_last_error() : __std_win_error::_Filename_exceeds_range;
        return r;
    }
    r._Size = n;
    return r;
}

__std_ulong_and_error __stdcall __std_fs_get_current_path(
    unsigned long _Target_size, wchar_t* _Target) noexcept
{
    __std_ulong_and_error r = { 0, __std_win_error::_Success };
    unsigned long n = GetCurrentDirectoryW(_Target_size, _Target);
    if (n == 0 || n >= _Target_size) {
        r._Error = n == 0 ? wk_last_error() : __std_win_error::_Filename_exceeds_range;
        return r;
    }
    r._Size = n;
    return r;
}

// --- math helpers (xmath declares them plain: vcruntime pieces) ---
double __stdcall __std_smf_hypot3(double _X, double _Y, double _Z) noexcept
{
    // v0: nested hypot; overflow-scaling refinements omitted.
    return hypot(hypot(_X, _Y), _Z);
}

float __stdcall __std_smf_hypot3f(float _X, float _Y, float _Z) noexcept
{
    return hypotf(hypotf(_X, _Y), _Z);
}

// ARM32 count-leading-zeros are __MACHINEZ (lib calls) here, not intrinsics.
unsigned int __cdecl _CountLeadingZeros(unsigned long _Mask) noexcept
{
    return _Mask ? (unsigned int)__builtin_clz(_Mask) : 32U;
}

unsigned int __cdecl _CountLeadingZeros64(unsigned long long _Mask) noexcept
{
    return _Mask ? (unsigned int)__builtin_clzll(_Mask) : 64U;
}

} // extern "C"
