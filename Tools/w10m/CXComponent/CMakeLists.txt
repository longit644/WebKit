cmake_minimum_required(VERSION 3.20)
project(WebKitWebViewComponent LANGUAGES CXX)
get_filename_component(WEBKIT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(W10M_API_DIR "${WEBKIT_SOURCE_DIR}/Source/WebKit/UIProcess/API/w10m")
# Historical metadata is generated under a uniquely named build-only header so
# a native prototype cannot accidentally include the new authored CX control.
set(W10M_LEGACY_SDK_ROOT "$ENV{WK_W10M_SDK_ROOT}" CACHE PATH "SDK root for historical MIDLRT generation")
set(W10M_LEGACY_SDK_VERSION "$ENV{WK_W10M_SDK_VERSION}" CACHE STRING "SDK version for historical MIDLRT generation")
find_program(W10M_LEGACY_MIDLRT midlrt HINTS "${W10M_LEGACY_SDK_ROOT}/bin/${W10M_LEGACY_SDK_VERSION}/x64" REQUIRED)
set(W10M_LEGACY_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/legacy-generated")
file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/WebKitWebView.idl" W10M_LEGACY_IDL_NATIVE)
file(TO_NATIVE_PATH "${W10M_LEGACY_GENERATED_DIR}" W10M_LEGACY_OUTPUT_NATIVE)
file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/WebKitWebView.winmd" W10M_LEGACY_WINMD_NATIVE)
file(TO_NATIVE_PATH "${W10M_LEGACY_SDK_ROOT}/UnionMetadata/${W10M_LEGACY_SDK_VERSION}" W10M_LEGACY_METADATA_NATIVE)
# Preserved native sample sources can use the historical basename only in this
# generated include directory, never the authored API directory.
file(GENERATE OUTPUT "${W10M_LEGACY_GENERATED_DIR}/WebKitWebView.h"
    CONTENT "#pragma once\n#include \"LegacyWebKitWebView.h\"\n")
add_custom_command(OUTPUT "${W10M_LEGACY_GENERATED_DIR}/LegacyWebKitWebView.h" "${CMAKE_CURRENT_BINARY_DIR}/WebKitWebView.winmd"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${W10M_LEGACY_GENERATED_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E env "PATH=$ENV{WK_W10M_TOOLSET_ROOT}/bin/Hostx64/x64;$ENV{PATH}"
        "${W10M_LEGACY_MIDLRT}" /winrt /nomidl /server none /client none
        /metadata_dir "${W10M_LEGACY_METADATA_NATIVE}"
        /h LegacyWebKitWebView.h /winmd "${W10M_LEGACY_WINMD_NATIVE}"
        /out "${W10M_LEGACY_OUTPUT_NATIVE}" "${W10M_LEGACY_IDL_NATIVE}"
    DEPENDS WebKitWebView.idl VERBATIM)
# Component DLL: WRL projection over the existing wrapper sources. The engine
# (WebCore.dll) stays dynamically bound at runtime; nothing links it here.
add_library(WebKitWebView SHARED WebKitWebViewComponent.cpp
    "${W10M_LEGACY_GENERATED_DIR}/LegacyWebKitWebView.h"
    "${W10M_API_DIR}/WebKitWebViewW10M.cpp"
    "${W10M_API_DIR}/rendering/SwapChainPresenterW10M.cpp"
    "${W10M_API_DIR}/rendering/FrameSchedulerW10M.cpp"
    "${W10M_API_DIR}/rendering/FrameMetricsW10M.cpp"
    "${W10M_API_DIR}/rendering/RenderSubscriptionW10M.cpp"
    "${W10M_API_DIR}/input/ScrollControllerW10M.cpp"
    "${W10M_API_DIR}/input/InputAdapterW10M.cpp")
target_compile_features(WebKitWebView PRIVATE cxx_std_17)
target_compile_definitions(WebKitWebView PRIVATE NOMINMAX _WINSOCKAPI_=)
target_include_directories(WebKitWebView PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${W10M_API_DIR}" "${WEBKIT_SOURCE_DIR}/Source/WebCore/platform/w10m" "C:/vcpkg/installed/arm-w10m-webkit/include")
target_include_directories(WebKitWebView PRIVATE "${WEBKIT_SOURCE_DIR}/Source/WebKit/Shared/w10m")
target_include_directories(WebKitWebView PRIVATE "${W10M_LEGACY_GENERATED_DIR}")
target_link_libraries(WebKitWebView PRIVATE runtimeobject d3d11 dxgi windowscodecs uuid ole32)
# Activation entry points: RoActivateInstance resolves the class through these.
target_link_options(WebKitWebView PRIVATE /EXPORT:DllGetActivationFactory /EXPORT:DllCanUnloadNow)
set_property(TARGET WebKitWebView APPEND PROPERTY LINK_DEPENDS "${WEBKIT_SOURCE_DIR}/Tools/w10m/lib/arm-crtstart.lib")

# XAML identity feasibility probe (browser-free). It is intentionally kept out
# of the shipping target until the standalone C++/CX DLL is linked and tested.
# C++/WinRT's SDK base.h rejects 32-bit ARM; compile XamlProbeComponent.cpp
# with MSVC ARM32 /ZW using the Windows 10 SDK ARM libraries. The source and
# IDL recipe are recorded in README.md.
