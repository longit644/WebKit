cmake_minimum_required(VERSION 3.20)
project(WebKitFpuProbe LANGUAGES CXX)
add_executable(FpuProbeHost WIN32 FpuProbe.cpp)
set(CRT_FRAME_PROBE_OBJECT "${CMAKE_CURRENT_BINARY_DIR}/CrtFrameProbe.obj")
add_custom_command(OUTPUT "${CRT_FRAME_PROBE_OBJECT}"
    COMMAND "${CMAKE_CXX_COMPILER}" --target=armv7-unknown-windows-msvc -c
        "${CMAKE_CURRENT_SOURCE_DIR}/CrtFrameProbe.S" -o "${CRT_FRAME_PROBE_OBJECT}"
    DEPENDS CrtFrameProbe.S VERBATIM)
set_source_files_properties("${CRT_FRAME_PROBE_OBJECT}" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
target_sources(FpuProbeHost PRIVATE "${CRT_FRAME_PROBE_OBJECT}")
target_compile_features(FpuProbeHost PRIVATE cxx_std_17)
target_compile_definitions(FpuProbeHost PRIVATE NOMINMAX _WINSOCKAPI_=)
# wWinMain + the arm-crtstart.lib provided CRT entry stubs (see toolchain/crtls).
target_link_libraries(FpuProbeHost PRIVATE runtimeobject)
target_link_options(FpuProbeHost PRIVATE /ENTRY:WKFpuProbeStartup /SUBSYSTEM:WINDOWS)
# Keep the toolchain's first-definition-wins policy: the frame adapter defines
# __intrinsic_setjmp, while the CRT import member also provides its IAT symbol.
get_filename_component(UWP_TOOLS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set_property(TARGET FpuProbeHost APPEND PROPERTY LINK_DEPENDS "${UWP_TOOLS_DIR}/lib/arm-crtstart.lib")
