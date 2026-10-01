# WebKitView ARM32-UWP clang toolchain (v0.3 — link recipe proven 2026-09-30)
#
# Target: armv7-unknown-windows-msvc, AppContainer (phone 15254 API surface,
# link surface from Win11 SDK 22621 ARM libs which still ship ARM32).
# NOTE: armv7 spelling (not thumbv7): identical codegen (verified COFF-ARM/
# ARMNT, byte-identical objects), but meson maps 'arm' in the target to
# /MACHINE:arm for llvm-lib ('thumbv7' falls through to /MACHINE:thumbv7,
# which llvm-lib rejects).
# Usage: -DCMAKE_TOOLCHAIN_FILE=<this file> with Tools/UWP/arm32-uwp-env.ps1
# sourced first. See Tools/UWP/ARM-WALLS.md for status.

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

set(WK_UWP_CLANG_CL "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/Llvm/bin/clang-cl.exe"
    CACHE FILEPATH "WebKitView ARM32-UWP clang-cl (19.15, MSVC 14.44)")

set(CMAKE_C_COMPILER "${WK_UWP_CLANG_CL}")
set(CMAKE_CXX_COMPILER "${WK_UWP_CLANG_CL}")
set(CMAKE_C_COMPILER_TARGET ${WK_UWP_TRIPLE})
set(CMAKE_CXX_COMPILER_TARGET ${WK_UWP_TRIPLE})

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
set(WK_UWP_C_FLAGS "--target=${WK_UWP_TRIPLE} -BC:/PROGRA~2/MICROS~3/2022/BUILDT~1/VC/Tools/Llvm/bin -fuse-ld=lld /MD -DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP -D_WIN32_WINNT=0x0A00 -D_HAS_EXCEPTIONS=0 -Wno-error=incompatible-function-pointer-types -DU_PLATFORM_HAS_WINUWP_API=1 -DSQLITE_OMIT_SEH -D_WINRT_DLL /FIC:/PROGRA~2/MICROS~3/2022/BUILDT~1/VC/Tools/MSVC/1444~1.352/include/intrin.h -FIC:/Users/Longi/WORKSP~1/WebKit/Tools/uwp/toolchain/uwp-desktop-apis.h /EHs-c- /GR- -imsvcC:/PROGRA~2/MICROS~3/2022/BUILDT~1/VC/Tools/MSVC/1444~1.352/include -imsvcC:/PROGRA~2/WI3CF2~1/10/Include/100226~1.0/ucrt -imsvcC:/PROGRA~2/WI3CF2~1/10/Include/100226~1.0/um -imsvcC:/PROGRA~2/WI3CF2~1/10/Include/100226~1.0/shared -imsvcC:/PROGRA~2/WI3CF2~1/10/Include/100226~1.0/winrt")
set(CMAKE_C_FLAGS_INIT "${WK_UWP_C_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${WK_UWP_C_FLAGS}")

# Compiler checks must not link: no ARM32 CRT (msvcrt/vcruntime) ships with
# MSVC 14.4x, and Debug CRT (msvcrtd/oldnames) does not exist at all.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
# Release CRT only; Debug configs are unsupported on this target.
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set(CMAKE_CONFIGURATION_TYPES Release RelWithDebInfo)

# ARM32 UWP link surface (PROVEN: wmain-arm.exe links, COFF-ARM/ARMNT).
# NOTE: vcpkg's toolchain ignores triplet VCPKG_LINKER_FLAGS — everything
# linker-side MUST live in these *_INIT vars (space-free 8.3 paths only;
# vcpkg re-splits flag strings and drops quoted entries with spaces).
# lld-link required: link.exe has no armv7 target (selected via -fuse-ld
# in WK_UWP_C_FLAGS above; -B dir there makes it resolvable under scrubbed
# PATH). /SUBSYSTEM:WINDOWS (WINDOWSCE is WinCE-era, rejected by lld).
# No ARM32 vcruntime140_app ships: msvcurt (Store CRT) + own crtstart.
# Debug unsupported, Release only.
# Our libs FIRST: first-match-wins keeps lld away from msvcurt's managed
# ti_inst.obj (unparseable). __RTDynamicCast stub + exact-named type_info
# vftable documented in toolchain/crrtti.c + crtvft.S. (An /ALTERNATENAME
# approach was rejected: resolving the alias forced lld to scan ti_inst.)
# Our crrt aliases (strdup/open/...) embed into EVERY DLL (lld exports all
# defined globals) and re-clash on multi-DLL links (cairo hit strdup via
# fontconfig.lib). All copies are semantically identical wrappers, so first-
# wins is safe: /FORCE:MULTIPLE. (Proper fix later: shared wkrt.dll.)
set(WK_UWP_LINK_FLAGS "/APPCONTAINER /MACHINE:ARM /SUBSYSTEM:CONSOLE /FORCE:MULTIPLE /NODEFAULTLIB:libcmt /NODEFAULTLIB:libcmtd /NODEFAULTLIB:msvcrt /NODEFAULTLIB:msvcrtd /NODEFAULTLIB:msvcprt /NODEFAULTLIB:msvcprtd /NODEFAULTLIB:oldnames /LIBPATH:C:/PROGRA~2/WI3CF2~1/10/Lib/100226~1.0/um/arm /LIBPATH:C:/PROGRA~2/WI3CF2~1/10/Lib/100226~1.0/ucrt/arm /LIBPATH:C:/Users/Longi/WORKSP~1/WebKit/Tools/uwp/lib C:/Users/Longi/WORKSP~1/WebKit/Tools/uwp/lib/CLANG_~1.LIB C:/Users/Longi/WORKSP~1/WebKit/Tools/uwp/lib/ARM-CR~1.LIB C:/PROGRA~2/MICROS~3/2022/BUILDT~1/VC/Tools/MSVC/1444~1.352/lib/arm/msvcurt.lib C:/PROGRA~2/WI3CF2~1/10/Lib/100226~1.0/um/arm/OneCoreUAP.lib C:/PROGRA~2/WI3CF2~1/10/Lib/100226~1.0/ucrt/arm/ucrt.lib")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${WK_UWP_LINK_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${WK_UWP_LINK_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${WK_UWP_LINK_FLAGS}")
set(WK_UWP_BUILTINS_ARM "C:/Users/Longi/WORKSP~1/WebKit/Tools/uwp/lib/CLANG_~1.LIB")
