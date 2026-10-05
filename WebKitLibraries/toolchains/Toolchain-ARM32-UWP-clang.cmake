# WebKitView ARM32-UWP clang toolchain (v0.4 — SDK repinned 2026-10-05)
#
# Target: armv7-unknown-windows-msvc, AppContainer (phone 15254 API surface,
# link surface from Windows 10 SDK 10.0.19041.0 ARM libs). 19041 is pinned
# because it is the newest SDK that still ships um/arm and ucrt/arm; every
# Windows 11 SDK dropped 32-bit ARM, so a newer SDK cannot link this target.
# NOTE: armv7 spelling (not thumbv7): identical codegen (verified COFF-ARM/
# ARMNT, byte-identical objects), but meson maps 'arm' in the target to
# /MACHINE:arm for llvm-lib ('thumbv7' falls through to /MACHINE:thumbv7,
# which llvm-lib rejects).
# Usage: -DCMAKE_TOOLCHAIN_FILE=<this file> with Tools/uwp/arm32-uwp-env.ps1
# sourced first. See Tools/uwp/ARM-WALLS.md for status.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_SYSTEM_PROCESSOR ARM)

set(WK_UWP_TRIPLE armv7-unknown-windows-msvc)

# UWP port switch: consumed by PlatformWin.cmake / Curl.cmake guards (CMake var
# below) and as the PLATFORM(UWP) source guard (compile definition). ON in
# this toolchain by definition. Naming follows the tree convention
# (PLATFORM(WIN)/PLATFORM(WPE)...): UWP is an environment modifier on the
# WIN port, mirroring PLATFORM(MACCATALYST).
set(WK_UWP ON CACHE BOOL "WebKitView UWP/ARM32 port")
add_compile_definitions(WTF_PLATFORM_UWP=1)

# Use the x64 HOST compiler: the sibling Llvm/bin executable is i386 and
# exhausts its address space on WebCore's generated style bundles. Target
# objects remain ARM32 through CMAKE_*_COMPILER_TARGET and --target flags.
set(WK_UWP_CLANG_CL "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"
    CACHE FILEPATH "WebKitWebView ARM32-UWP x64-host clang-cl (19.1.5, MSVC 14.44)")

set(CMAKE_C_COMPILER "${WK_UWP_CLANG_CL}")
set(CMAKE_CXX_COMPILER "${WK_UWP_CLANG_CL}")
set(CMAKE_C_COMPILER_TARGET ${WK_UWP_TRIPLE})
set(CMAKE_CXX_COMPILER_TARGET ${WK_UWP_TRIPLE})

# Resources are host-compiled by the SDK tool. Pin INIT as well as COMPILER:
# Windows-Clang enables its separate clang preprocessing if INIT still names
# llvm-rc, even when COMPILER has been changed to SDK rc.exe. That path fed
# FreeType's version resource preprocessed C/C++ declarations and hung rc.exe.
set(WK_UWP_RC "C:/PROGRA~2/WI3CF2~1/10/bin/100190~1.0/x64/rc.exe")
set(CMAKE_RC_COMPILER_INIT "${WK_UWP_RC}")
set(CMAKE_RC_COMPILER "${WK_UWP_RC}" CACHE FILEPATH "ARM UWP host resource compiler")

# AppContainer + no-exceptions (clang armv7 cannot lower cleanupret).
# _WIN32_WINNT=Win10: exposes the SDK's own App-partition inline wrappers
# (memoryapi.h CreateFileMappingW/MapViewOfFile need >= Win8; fontconfig pins
# Vista otherwise and loses them).
add_compile_definitions(
    WINAPI_FAMILY=WINAPI_FAMILY_PC_APP
    _WIN32_WINNT=0x0A00
    _HAS_EXCEPTIONS=0
)
add_compile_options(/EHs-c- /GR-)

# NOTE: vcpkg port builds harvest flags from CMAKE_C_FLAGS* variables, NOT
# from add_compile_options/COMPILE_OPTIONS. Everything compiler-side MUST go
# through CMAKE_*_FLAGS_INIT here, or ports silently compile for the host.
# -B gives clang a space-free linker search dir (vcpkg scrubs PATH, so a bare
# -fuse-ld=lld would not resolve there).
set(WK_UWP_C_FLAGS "--target=${WK_UWP_TRIPLE} -BC:/PROGRA~2/MICROS~3/2022/BUILDT~1/VC/Tools/Llvm/bin -fuse-ld=lld /MD -DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP -D_WIN32_WINNT=0x0A00 -D_HAS_EXCEPTIONS=0 -Wno-error=incompatible-function-pointer-types -DU_PLATFORM_HAS_WINUWP_API=1 -DSQLITE_OMIT_SEH -D_WINRT_DLL /FIC:/PROGRA~2/MICROS~3/2022/BUILDT~1/VC/Tools/MSVC/1444~1.352/include/intrin.h -FIC:/Users/Longi/WORKSP~1/WebKit/Tools/uwp/toolchain/uwp-desktop-apis.h /EHs-c- /GR- -imsvcC:/PROGRA~2/MICROS~3/2022/BUILDT~1/VC/Tools/MSVC/1444~1.352/include -imsvcC:/PROGRA~2/WI3CF2~1/10/Include/100190~1.0/ucrt -imsvcC:/PROGRA~2/WI3CF2~1/10/Include/100190~1.0/um -imsvcC:/PROGRA~2/WI3CF2~1/10/Include/100190~1.0/shared -imsvcC:/PROGRA~2/WI3CF2~1/10/Include/100190~1.0/winrt")
set(CMAKE_C_FLAGS_INIT "${WK_UWP_C_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${WK_UWP_C_FLAGS}")

# Compiler checks must not link: no ARM32 CRT (msvcrt/vcruntime) ships with
# MSVC 14.4x, and Debug CRT (msvcrtd/oldnames) does not exist at all.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
# Release CRT only; Debug configs are unsupported on this target.
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set(CMAKE_CONFIGURATION_TYPES Release RelWithDebInfo)

# ARM32 UWP link surface (PROVEN: minimal mainCRTStartup exe links, COFF-ARM/ARMNT
# thumb, and wmain-arm.exe previously linked).
# SDK 10.0.19041.0 is the pinned Windows SDK: it is the newest installed SDK that
# still ships um/arm and ucrt/arm, which every later Windows 11 SDK dropped.
# MSVC 14.4x no longer ships the combined Store import library msvcurt.lib that
# this file used to link. Its role is now filled by the three pieces below, which
# is what an MSBuild UWP ARM project links: the SDK UCRT, plus the UWP flavor of
# the MSVC C++ runtime support libraries. Verified by direct link before use.
# NOTE: vcpkg's toolchain ignores triplet VCPKG_LINKER_FLAGS — everything
# linker-side MUST live in these *_INIT vars (space-free 8.3 paths only;
# vcpkg re-splits flag strings and drops quoted entries with spaces).
# lld-link required: link.exe has no armv7 target (selected via -fuse-ld
# in WK_UWP_C_FLAGS above; -B dir there makes it resolvable under scrubbed
# PATH). /SUBSYSTEM:WINDOWS (WINDOWSCE is WinCE-era, rejected by lld).
# Debug unsupported, Release only.
# Our libs FIRST: our crrt aliases (strdup/open/...) embed into EVERY DLL (lld
# exports all defined globals) and re-clash on multi-DLL links (cairo hit strdup
# via fontconfig.lib). All copies are semantically identical wrappers, so first-
# wins is safe: /FORCE:MULTIPLE. (Proper fix later: shared wkrt.dll.)
# __RTDynamicCast stub + exact-named type_info vftable documented in
# toolchain/crrtti.c + crtvft.S.
set(WK_UWP_LINK_FLAGS "/APPCONTAINER /MACHINE:ARM /SUBSYSTEM:CONSOLE /FORCE:MULTIPLE /NODEFAULTLIB:libcmt /NODEFAULTLIB:libcmtd /NODEFAULTLIB:msvcrt /NODEFAULTLIB:msvcrtd /NODEFAULTLIB:msvcprt /NODEFAULTLIB:msvcprtd /NODEFAULTLIB:oldnames /LIBPATH:C:/PROGRA~2/WI3CF2~1/10/Lib/100190~1.0/um/arm /LIBPATH:C:/PROGRA~2/WI3CF2~1/10/Lib/100190~1.0/ucrt/arm /LIBPATH:C:/PROGRA~2/MICROS~3/2022/BUILDT~1/VC/Tools/MSVC/1444~1.352/lib/arm/uwp /LIBPATH:C:/Users/Longi/WORKSP~1/WebKit/Tools/uwp/lib C:/Users/Longi/WORKSP~1/WebKit/Tools/uwp/lib/CLANG_~1.LIB C:/Users/Longi/WORKSP~1/WebKit/Tools/uwp/lib/ARM-CR~1.LIB C:/PROGRA~2/MICROS~3/2022/BUILDT~1/VC/Tools/MSVC/1444~1.352/lib/arm/uwp/vcruntime.lib C:/PROGRA~2/MICROS~3/2022/BUILDT~1/VC/Tools/MSVC/1444~1.352/lib/arm/uwp/msvcprt.lib C:/PROGRA~2/WI3CF2~1/10/Lib/100190~1.0/ucrt/arm/ucrt.lib C:/PROGRA~2/WI3CF2~1/10/Lib/100190~1.0/um/arm/OneCoreUAP.lib")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${WK_UWP_LINK_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${WK_UWP_LINK_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${WK_UWP_LINK_FLAGS}")
set(WK_UWP_BUILTINS_ARM "C:/Users/Longi/WORKSP~1/WebKit/Tools/uwp/lib/CLANG_~1.LIB")
