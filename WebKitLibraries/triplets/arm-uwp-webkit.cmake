# vcpkg overlay triplet: ARM32-UWP WebKitView (armv7, AppContainer).
# Usage: vcpkg install <port> --overlay-triplets=<repo>/WebKitLibraries/triplets
#   --triplet arm-uwp-webkit   (classic C:\vcpkg instance)
# Chainloads WebKitLibraries/toolchains/Toolchain-ARM32-UWP-clang.cmake (clang-cl 19.15,
# static try-compile, Release CRT, Store link surface). See ARM-WALLS.md.

set(VCPKG_TARGET_ARCHITECTURE arm)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

# WindowsStore: first-class UWP in vcpkg ports (curl drops SCHANNEL, icu gets
# WINUWP_API+tzset handling). Same as community arm-uwp. The chainloaded
# toolchain file keeps CMAKE_SYSTEM_NAME=Windows for the actual build.
# NO VCPKG_CMAKE_SYSTEM_NAME=WindowsStore (deliberate, 2026-09-30): it makes
# libjpeg-turbo's portfile export _CL_="-DNO_GETENV -DNO_PUTENV", which breaks
# CMake's ABI check under clang-cl ("multiple source files"), and switches
# CMake to the WindowsStore platform. What WindowsStore mode would buy is
# covered manually instead: curl schannel-off via explicit openssl features,
# ICU UWP via -DU_PLATFORM_HAS_WINUWP_API=1 -DSQLITE_OMIT_SEH -D_WINRT_DLL in flags, OpenSSL already built
# as VC-WIN32-ARM-UWP. See ARM-WALLS.md. Revisit per-port if a future dep
# truly needs IS_UWP.

# Debug CRT does not exist for this target (no msvcrtd/oldnames for ARM32).
set(VCPKG_BUILD_TYPE release)

set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../toolchains/Toolchain-ARM32-UWP-clang.cmake")

# Compiler wrapper (NOT raw clang-cl): vcpkg/libtool wrap every flag in
# -Xcompiler/-Xlinker escapes that clang-cl rejects, silently dropping them.
# The wrapper strips escapes and maps -lfoo to foo.lib. Live copy outside the
# repo (msys needs an extensionless path): C:/Users/Longi/llvm-tools/clang-cl-arm
# Master: Tools/UWP/toolchain/clang-cl-arm.sh (KEEP IN SYNC).
# Full short paths: vcpkg scrubs PATH for port builds. No spaces, so msys sh
# word-splits correctly.
# Shim dir FIRST on PATH so our `link` (lld, not msys coreutils) wins for
# ICU configure's linker sanity check. SDK bin second: host tools the build
# needs (rc.exe). No spaces (msys-safe).
set(ENV{PATH} "C:/Users/Longi/llvm-tools;C:/PROGRA~2/WI3CF2~1/10/bin/100226~1.0/x64;$ENV{PATH}")
# INCLUDE for native host tools that don't take our flags (rc.exe needs the
# um headers; short paths, semicolon-separated as Windows expects).
set(ENV{INCLUDE} "C:/PROGRA~2/MICROS~3/2022/BUILDT~1/VC/Tools/MSVC/1444~1.352/include;C:/PROGRA~2/WI3CF2~1/10/Include/100226~1.0/ucrt;C:/PROGRA~2/WI3CF2~1/10/Include/100226~1.0/um;C:/PROGRA~2/WI3CF2~1/10/Include/100226~1.0/shared;C:/PROGRA~2/WI3CF2~1/10/Include/100226~1.0/winrt")
# NMAKE: vcpkg's find_program misses it in scrubbed env; preset (arch-neutral
# driver; actual compile/link still ours). Harmless for non-nmake ports.
set(NMAKE "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/nmake.exe" CACHE FILEPATH "nmake for vcpkg ports")
# msys converts /FOO args to POSIX paths (eating linker flags AND /FI paths)
# at EVERY sh spawn, not just inside our wrapper. Export the exclusion list
# globally so configure/make/wrapper children are all protected. Dash-args
# are never converted; C:/... drive paths pass through; /tmp/... converts.
set(ENV{MSYS2_ARG_CONV_EXCL} "/link;/APPCONTAINER;/MACHINE;/machine;/SUBSYSTEM;/subsystem;/NODEFAULTLIB;/nodefaultlib;/LIBPATH;/libpath;/ENTRY;/entry;/INCREMENTAL;/incremental;/MANIFEST;/manifest;/DYNAMICBASE;/dynamicbase;/NXCOMPAT;/nxcompat;/ALTERNATENAME;/alternatename;/DLL;/dll;/NOENTRY;/noentry;/IMPLIB;/implib;/DEF;/def;/OUT;/out;/FI;/MD;/MT;/LD")
set(ENV{CC} "C:/Users/Longi/llvm-tools/clang-cl-arm")
set(ENV{CXX} "C:/Users/Longi/llvm-tools/clang-cl-arm")

# Autotools/meson ports never see the CMake toolchain: drive everything via
# flags. lld-link by absolute path (vcpkg scrubs PATH), Store CRT recipe.
set(_WK_LLD "lld")
set(ENV{CPPFLAGS} "--target=armv7-unknown-windows-msvc")
set(_WK_BUILTINS "C:/Users/Longi/WORKSP~1/WebKit/Tools/uwp/lib/CLANG_~1.LIB")
set(_WK_MSCVCRT "C:/PROGRA~2/MICROS~3/2022/BUILDT~1/VC/Tools/MSVC/1444~1.352/lib/arm/msvcurt.lib")
set(_WK_UAP "C:/PROGRA~2/WI3CF2~1/10/Lib/100226~1.0/um/arm/OneCoreUAP.lib")
set(_WK_UCRT "C:/PROGRA~2/WI3CF2~1/10/Lib/100226~1.0/ucrt/arm/ucrt.lib")
set(_WK_CRTSTART "C:/Users/Longi/WORKSP~1/WebKit/Tools/uwp/lib/ARM-CR~1.LIB")
# Own CRT startup (no ARM32 vcpkg140_app ships; see toolchain/arm-crtstart.c).
# No /ENTRY override: default per-subsystem entries resolve from this lib.
set(VCPKG_C_FLAGS "/MD -DWINAPI_FAMILY=WINAPI_FAMILY_PC_APP -D_WIN32_WINNT=0x0A00 -D_HAS_EXCEPTIONS=0 -DU_PLATFORM_HAS_WINUWP_API=1 -DSQLITE_OMIT_SEH -D_WINRT_DLL /FIC:/PROGRA~2/MICROS~3/2022/BUILDT~1/VC/Tools/MSVC/1444~1.352/include/intrin.h -FIC:/Users/Longi/WORKSP~1/WebKit/Tools/uwp/toolchain/uwp-desktop-apis.h -imsvcC:/PROGRA~2/MICROS~3/2022/BUILDT~1/VC/Tools/MSVC/1444~1.352/include -imsvcC:/PROGRA~2/WI3CF2~1/10/Include/100226~1.0/ucrt -imsvcC:/PROGRA~2/WI3CF2~1/10/Include/100226~1.0/um -imsvcC:/PROGRA~2/WI3CF2~1/10/Include/100226~1.0/shared -imsvcC:/PROGRA~2/WI3CF2~1/10/Include/100226~1.0/winrt")
set(VCPKG_CXX_FLAGS "${VCPKG_C_FLAGS}")
# --target/-fuse-ld/-B ride the toolchain file (detection input); CPPFLAGS
# is the one caller-ENV channel vcpkg preserves into configure (see
# vcpkg_configure_make.cmake:836), kept as insurance for autotools ports.
set(_WK_LIBPATH_UM "C:/PROGRA~2/WI3CF2~1/10/Lib/100226~1.0/um/arm")
set(_WK_LIBPATH_UCRT "C:/PROGRA~2/WI3CF2~1/10/Lib/100226~1.0/ucrt/arm")
set(VCPKG_LINKER_FLAGS "/APPCONTAINER /MACHINE:ARM /SUBSYSTEM:CONSOLE")
# NOTE: this vcpkg's toolchain ignores VCPKG_LINKER_FLAGS; the real recipe
# lives in CMAKE_*_LINKER_FLAGS_INIT in the chainloaded toolchain file.
