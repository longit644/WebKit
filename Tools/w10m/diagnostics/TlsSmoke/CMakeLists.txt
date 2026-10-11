cmake_minimum_required(VERSION 3.20)
project(WebKitTlsProbe LANGUAGES CXX)
add_library(WebKitTlsProbe SHARED TlsProbe.cpp)
target_compile_features(WebKitTlsProbe PRIVATE cxx_std_17)
# The shared ARM32 environment supplies -B<llvm-bin>, which clang-cl 19 treats
# as an unknown driver argument. Suppress that toolchain-only notice locally;
# the build gate can then reject every warning in the payload build output.
target_compile_options(WebKitTlsProbe PRIVATE -Wno-unknown-argument)
# These probe DLLs must not hide duplicate runtime definitions.
string(REPLACE "/FORCE:MULTIPLE" "" CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
string(REPLACE "/FORCE:MULTIPLE" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
# _WINSOCKAPI_= keeps windows.h from pulling winsock.h so curl's winsock2.h include is clean.
target_compile_definitions(WebKitTlsProbe PRIVATE NOMINMAX _WINSOCKAPI_=)
# Discovered vcpkg prefix: pass -DVCPKG_INSTALLED_DIR=<vcpkg>/installed/arm-w10m-webkit
# (or set VCPKG_ROOT); no machine-local absolute paths live in this file.
if(NOT DEFINED VCPKG_INSTALLED_DIR)
  if(DEFINED ENV{VCPKG_ROOT})
    set(VCPKG_INSTALLED_DIR "$ENV{VCPKG_ROOT}/installed/arm-w10m-webkit")
  else()
    set(VCPKG_INSTALLED_DIR "C:/vcpkg/installed/arm-w10m-webkit")
  endif()
endif()
target_include_directories(WebKitTlsProbe PRIVATE "${VCPKG_INSTALLED_DIR}/include")
# bcrypt resolves through the ARM UWP toolchain's um/arm library path, so the
# SDK version stays discovered (arm32-w10m-env.ps1) instead of hard-coded here.
target_link_libraries(WebKitTlsProbe PRIVATE runtimeobject "${VCPKG_INSTALLED_DIR}/lib/libcurl.lib" "${VCPKG_INSTALLED_DIR}/lib/libcrypto.lib" bcrypt)
get_filename_component(W10M_TOOLS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set_property(TARGET WebKitTlsProbe APPEND PROPERTY LINK_DEPENDS "${W10M_TOOLS_DIR}/lib/arm-crtstart.lib")
