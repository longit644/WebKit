cmake_minimum_required(VERSION 3.20)
project(WebKitRuntimeSmoke LANGUAGES CXX)
# Native smoke payload for the C# RuntimeSmoke host (M10 8.5): the CRT probe
# plus the first-page diagnostics driver. The application host retired with
# SmokeHost.cpp; nothing here owns activation, windows or XAML.
add_library(WebKitRuntimeSmoke SHARED payload/RuntimeSmoke.cpp payload/SmokeDiagnostics.cpp)
target_compile_features(WebKitRuntimeSmoke PRIVATE cxx_std_17)
# These probe DLLs must not hide duplicate runtime definitions.
string(REPLACE "/FORCE:MULTIPLE" "" CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")
string(REPLACE "/FORCE:MULTIPLE" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
get_filename_component(W10M_TOOLS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set_property(TARGET WebKitRuntimeSmoke APPEND PROPERTY LINK_DEPENDS "${W10M_TOOLS_DIR}/lib/arm-crtstart.lib")
get_filename_component(WEBKIT_SOURCE_DIR "${W10M_TOOLS_DIR}/../.." ABSOLUTE)
set(W10M_PRESENTER_DIR "${WEBKIT_SOURCE_DIR}/Source/WebKit/UIProcess/API/w10m")
set(W10M_WEBCORE_DIR "${WEBKIT_SOURCE_DIR}/Source/WebCore/platform/w10m")
target_sources(WebKitRuntimeSmoke PRIVATE "${W10M_PRESENTER_DIR}/rendering/SwapChainPresenterW10M.cpp")
target_include_directories(WebKitRuntimeSmoke PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/payload" "${W10M_PRESENTER_DIR}" "${W10M_WEBCORE_DIR}")
target_include_directories(WebKitRuntimeSmoke PRIVATE "${WEBKIT_SOURCE_DIR}/Source/WebKit/Shared/w10m")
target_link_libraries(WebKitRuntimeSmoke PRIVATE runtimeobject d3d11 dxgi)
set_property(TARGET WebKitRuntimeSmoke APPEND PROPERTY LINK_DEPENDS "${W10M_TOOLS_DIR}/lib/arm-crtstart.lib")
