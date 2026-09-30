# WebKitView ARM32-UWP clang toolchain (v0.3 — link recipe proven 2026-09-30)
#
# Target: thumbv7-unknown-windows-msvc, AppContainer (phone 15254 API surface,
# link surface from Win11 SDK 22621 ARM libs which still ship ARM32).
# Usage: -DCMAKE_TOOLCHAIN_FILE=<this file> with WebKitView/arm32-uwp-env.ps1
# sourced first. See WebKitView/ARM-WALLS.md for status.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_SYSTEM_PROCESSOR ARM)

set(WK_UWP_TRIPLE thumbv7-unknown-windows-msvc)

# WebKitView port switch: consumed by PlatformWin.cmake / Curl.cmake guards
# and as the WK_WEBKITVIEW source guard. ON in this toolchain by definition.
set(WK_WEBKITVIEW_UWP ON CACHE BOOL "WebKitView UWP/ARM32 port")
add_compile_definitions(WK_WEBKITVIEW)

set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_C_COMPILER_TARGET ${WK_UWP_TRIPLE})
set(CMAKE_CXX_COMPILER_TARGET ${WK_UWP_TRIPLE})

# AppContainer + no-exceptions (clang thumbv7 cannot lower cleanupret)
add_compile_definitions(
    WINAPI_FAMILY=WINAPI_FAMILY_PC_APP
    _HAS_EXCEPTIONS=0
)
add_compile_options(/EHs-c- /GR-)

# Compiler checks must not link: no ARM32 CRT (msvcrt/vcruntime) ships with
# MSVC 14.4x, and Debug CRT (msvcrtd/oldnames) does not exist at all.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
# Release CRT only; Debug configs are unsupported on this target.
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set(CMAKE_CONFIGURATION_TYPES Release RelWithDebInfo)

# ARM32 UWP link surface (PROVEN: wmain-arm.exe links, COFF-ARM/ARMNT).
# lld-link required: link.exe has no thumbv7 target (use -fuse-ld=lld),
# /MD + NODEFAULTLIB desktop CRTs + Store CRT msvcurt (MSVC lib\arm) +
# OneCoreUAP + ucrt (SDK 22621 arm) + our builtins (thirdparty/).
# /SUBSYSTEM:WINDOWS (WINDOWSCE is WinCE-era, rejected by lld).
# No ARM32 vcruntime140_app ships: executables use /ENTRY:<own>;
# DLLs avoid CRT-startup dependency the same way. Debug unsupported.
set(CMAKE_EXE_LINKER_FLAGS_INIT "/APPCONTAINER /MACHINE:ARM /SUBSYSTEM:WINDOWS")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "/APPCONTAINER /MACHINE:ARM")
set(WK_UWP_BUILTINS_ARM "${CMAKE_CURRENT_LIST_DIR}/thirdparty/clang_rt.builtins-arm.lib")
