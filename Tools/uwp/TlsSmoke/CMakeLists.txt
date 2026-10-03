cmake_minimum_required(VERSION 3.20)
project(WebKitTlsSmoke LANGUAGES CXX)
add_executable(TlsSmokeHost WIN32 TlsSmoke.cpp)
target_compile_features(TlsSmokeHost PRIVATE cxx_std_17)
# _WINSOCKAPI_= keeps windows.h from pulling winsock.h so curl's winsock2.h include is clean.
# Explicit vcpkg paths: this configure has no CMAKE_PREFIX_PATH.
target_compile_definitions(TlsSmokeHost PRIVATE NOMINMAX _WINSOCKAPI_=)
target_include_directories(TlsSmokeHost PRIVATE "C:/vcpkg/installed/arm-uwp-webkit/include")
target_link_libraries(TlsSmokeHost PRIVATE runtimeobject "C:/vcpkg/installed/arm-uwp-webkit/lib/libcurl.lib" "C:/vcpkg/installed/arm-uwp-webkit/lib/libcrypto.lib" "C:/Program Files (x86)/Windows Kits/10/Lib/10.0.22621.0/um/arm/bcrypt.lib")
target_link_options(TlsSmokeHost PRIVATE /ENTRY:WKTlsStartup /SUBSYSTEM:WINDOWS)
string(REPLACE "/FORCE:MULTIPLE" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
get_filename_component(UWP_TOOLS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set_property(TARGET TlsSmokeHost APPEND PROPERTY LINK_DEPENDS "${UWP_TOOLS_DIR}/lib/arm-crtstart.lib")
