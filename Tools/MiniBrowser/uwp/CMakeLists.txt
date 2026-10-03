cmake_minimum_required(VERSION 3.20)
project(MiniBrowserUWP LANGUAGES CXX)
get_filename_component(WEBKIT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(UWP_API_DIR "${WEBKIT_SOURCE_DIR}/Source/WebKit/UIProcess/API/uwp")
add_executable(MiniBrowserUWP WIN32 MiniBrowserUWP.cpp
    "${UWP_API_DIR}/WebKitWebViewUWP.cpp"
    "${UWP_API_DIR}/WebKitWebViewPresenterUWP.cpp")
target_compile_features(MiniBrowserUWP PRIVATE cxx_std_17)
# _WINSOCKAPI_= keeps windows.h from pulling winsock.h so curl's winsock2.h include is clean.
# Explicit vcpkg paths: the MiniBrowser configure has no CMAKE_PREFIX_PATH.
target_compile_definitions(MiniBrowserUWP PRIVATE NOMINMAX _WINSOCKAPI_=)
target_include_directories(MiniBrowserUWP PRIVATE "${UWP_API_DIR}" "${WEBKIT_SOURCE_DIR}/Source/WebCore/platform/uwp" "C:/vcpkg/installed/arm-uwp-webkit/include")
target_link_libraries(MiniBrowserUWP PRIVATE runtimeobject d3d11 dxgi windowscodecs uuid "C:/vcpkg/installed/arm-uwp-webkit/lib/libcurl.lib")
target_link_options(MiniBrowserUWP PRIVATE /ENTRY:WKMiniBrowserStartup /SUBSYSTEM:WINDOWS)
string(REPLACE "/FORCE:MULTIPLE" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
set_property(TARGET MiniBrowserUWP APPEND PROPERTY LINK_DEPENDS "${WEBKIT_SOURCE_DIR}/Tools/uwp/lib/arm-crtstart.lib")
