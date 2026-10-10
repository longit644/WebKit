cmake_minimum_required(VERSION 3.20)
project(WebKitWebViewComponent LANGUAGES CXX)
get_filename_component(WEBKIT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(UWP_API_DIR "${WEBKIT_SOURCE_DIR}/Source/WebKit/UIProcess/API/uwp")
# Historical metadata is generated under a uniquely named build-only header so
# a native prototype cannot accidentally include the new authored CX control.
set(UWP_LEGACY_SDK_ROOT "$ENV{WK_UWP_SDK_ROOT}" CACHE PATH "SDK root for historical MIDLRT generation")
set(UWP_LEGACY_SDK_VERSION "$ENV{WK_UWP_SDK_VERSION}" CACHE STRING "SDK version for historical MIDLRT generation")
find_program(UWP_LEGACY_MIDLRT midlrt HINTS "${UWP_LEGACY_SDK_ROOT}/bin/${UWP_LEGACY_SDK_VERSION}/x64" REQUIRED)
set(UWP_LEGACY_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/legacy-generated")
file(TO_NATIVE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/WebKitWebView.idl" UWP_LEGACY_IDL_NATIVE)
file(TO_NATIVE_PATH "${UWP_LEGACY_GENERATED_DIR}" UWP_LEGACY_OUTPUT_NATIVE)
file(TO_NATIVE_PATH "${CMAKE_CURRENT_BINARY_DIR}/WebKitWebView.winmd" UWP_LEGACY_WINMD_NATIVE)
file(TO_NATIVE_PATH "${UWP_LEGACY_SDK_ROOT}/UnionMetadata/${UWP_LEGACY_SDK_VERSION}" UWP_LEGACY_METADATA_NATIVE)
# Preserved native sample sources can use the historical basename only in this
# generated include directory, never the authored API directory.
file(GENERATE OUTPUT "${UWP_LEGACY_GENERATED_DIR}/WebKitWebView.h"
    CONTENT "#pragma once\n#include \"LegacyWebKitWebView.h\"\n")
add_custom_command(OUTPUT "${UWP_LEGACY_GENERATED_DIR}/LegacyWebKitWebView.h" "${CMAKE_CURRENT_BINARY_DIR}/WebKitWebView.winmd"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${UWP_LEGACY_GENERATED_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E env "PATH=$ENV{WK_UWP_TOOLSET_ROOT}/bin/Hostx64/x64;$ENV{PATH}"
        "${UWP_LEGACY_MIDLRT}" /winrt /nomidl /server none /client none
        /metadata_dir "${UWP_LEGACY_METADATA_NATIVE}"
        /h LegacyWebKitWebView.h /winmd "${UWP_LEGACY_WINMD_NATIVE}"
        /out "${UWP_LEGACY_OUTPUT_NATIVE}" "${UWP_LEGACY_IDL_NATIVE}"
    DEPENDS WebKitWebView.idl VERBATIM)
# Component DLL: WRL projection over the existing wrapper sources. The engine
# (WebCore.dll) stays dynamically bound at runtime; nothing links it here.
add_library(WebKitWebView SHARED WebKitWebViewComponent.cpp
    "${UWP_LEGACY_GENERATED_DIR}/LegacyWebKitWebView.h"
    "${UWP_API_DIR}/WebKitWebViewUWP.cpp"
    "${UWP_API_DIR}/rendering/SwapChainPresenterUWP.cpp"
    "${UWP_API_DIR}/rendering/FrameSchedulerUWP.cpp"
    "${UWP_API_DIR}/rendering/FrameMetricsUWP.cpp"
    "${UWP_API_DIR}/rendering/RenderSubscriptionUWP.cpp"
    "${UWP_API_DIR}/input/ScrollControllerUWP.cpp"
    "${UWP_API_DIR}/input/InputAdapterUWP.cpp")
target_compile_features(WebKitWebView PRIVATE cxx_std_17)
target_compile_definitions(WebKitWebView PRIVATE NOMINMAX _WINSOCKAPI_=)
target_include_directories(WebKitWebView PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${UWP_API_DIR}" "${WEBKIT_SOURCE_DIR}/Source/WebCore/platform/uwp" "C:/vcpkg/installed/arm-uwp-webkit/include")
target_include_directories(WebKitWebView PRIVATE "${WEBKIT_SOURCE_DIR}/Source/WebKit/Shared/uwp")
target_include_directories(WebKitWebView PRIVATE "${UWP_LEGACY_GENERATED_DIR}")
target_link_libraries(WebKitWebView PRIVATE runtimeobject d3d11 dxgi windowscodecs uuid ole32)
# Activation entry points: RoActivateInstance resolves the class through these.
target_link_options(WebKitWebView PRIVATE /EXPORT:DllGetActivationFactory /EXPORT:DllCanUnloadNow)
set_property(TARGET WebKitWebView APPEND PROPERTY LINK_DEPENDS "${WEBKIT_SOURCE_DIR}/Tools/uwp/lib/arm-crtstart.lib")

# XAML identity feasibility probe (browser-free). It is intentionally kept out
# of the shipping target until the standalone C++/CX DLL is linked and tested.
# C++/WinRT's SDK base.h rejects 32-bit ARM; compile XamlProbeComponent.cpp
# with MSVC ARM32 /ZW using the Windows 10 SDK ARM libraries. The source and
# IDL recipe are recorded in README.md.
