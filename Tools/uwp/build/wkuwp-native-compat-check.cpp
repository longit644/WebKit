// Compile-only ABI checks for the SDK declarations restored by the native shim.
// A wrong working-set stride would silently misread the OS-provided buffer.
#include "uwp-desktop-apis.h"
#include <stddef.h>
#include <type_traits>
#include <windows.h>

static_assert(sizeof(PSAPI_WORKING_SET_BLOCK) == sizeof(ULONG_PTR), "SDK working-set entry stride");
static_assert(sizeof(PSAPI_WORKING_SET_INFORMATION) == 2 * sizeof(ULONG_PTR), "SDK working-set header extent");
static_assert(offsetof(PSAPI_WORKING_SET_INFORMATION, WorkingSetInfo) == sizeof(ULONG_PTR), "SDK entries offset");
static_assert(std::is_same<decltype(&GetModuleHandleW), HMODULE(WINAPI*)(LPCWSTR)>::value, "SDK module-handle type");
static_assert(std::is_same<decltype(&LoadLibraryW), HMODULE(WINAPI*)(LPCWSTR)>::value, "SDK loader handle type");
static_assert(std::is_same<decltype(&QueryWorkingSet), BOOL(WINAPI*)(HANDLE, void*, DWORD)>::value, "SDK query signature");
