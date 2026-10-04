cmake_minimum_required(VERSION 3.20)
project(WebKitWebViewComponent LANGUAGES CXX)
get_filename_component(WEBKIT_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
set(UWP_API_DIR "${WEBKIT_SOURCE_DIR}/Source/WebKit/UIProcess/API/uwp")
# Component DLL: WRL projection over the existing wrapper sources. The engine
# (WebCore.dll) stays dynamically bound at runtime; nothing links it here.
add_library(WebKitWebView SHARED WebKitWebViewComponent.cpp
    "${UWP_API_DIR}/WebKitWebViewUWP.cpp"
    "${UWP_API_DIR}/WebKitWebViewPresenterUWP.cpp")
target_compile_features(WebKitWebView PRIVATE cxx_std_17)
target_compile_definitions(WebKitWebView PRIVATE NOMINMAX _WINSOCKAPI_=)
target_include_directories(WebKitWebView PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${UWP_API_DIR}" "${WEBKIT_SOURCE_DIR}/Source/WebCore/platform/uwp" "C:/vcpkg/installed/arm-uwp-webkit/include")
target_link_libraries(WebKitWebView PRIVATE runtimeobject d3d11 dxgi windowscodecs uuid ole32)
# Activation entry points: RoActivateInstance resolves the class through these.
target_link_options(WebKitWebView PRIVATE /EXPORT:DllGetActivationFactory /EXPORT:DllCanUnloadNow)
set_property(TARGET WebKitWebView APPEND PROPERTY LINK_DEPENDS "${WEBKIT_SOURCE_DIR}/Tools/uwp/lib/arm-crtstart.lib")

# XAML identity feasibility probe (browser-free). Not wired into the build: both
# C++/WinRT shapes tried so far fail to compile here, and the blocker is now
# narrow and recorded in
# openspec/changes/wkview-m9-uwp-webview-navigation/xaml-probe-findings.md.
# Toolchain facts that do hold: cppwinrt and the C++/WinRT headers ship with the
# SDK; this target needs exceptions and C++20 enabled locally; cppwinrt emits no
# XamlProbeT<T> template for this class.
