cmake_minimum_required(VERSION 3.20)
# M10 task 8.6: the FpuProbe application host is C# (see host/). This target
# builds only the unchanged native FPU/ARM payload as an ARM DLL that the C#
# host loads with DllImport. CrtFrameProbe.S keeps both frame-argument variants.
project(WebKitFpuProbe LANGUAGES CXX)
add_library(FpuProbePayload SHARED payload/FpuProbe.cpp payload/FpuProbePayload.def)
set(CRT_FRAME_PROBE_OBJECT "${CMAKE_CURRENT_BINARY_DIR}/CrtFrameProbe.obj")
add_custom_command(OUTPUT "${CRT_FRAME_PROBE_OBJECT}"
    COMMAND "${CMAKE_CXX_COMPILER}" --target=armv7-unknown-windows-msvc -c
        "${CMAKE_CURRENT_SOURCE_DIR}/payload/CrtFrameProbe.S" -o "${CRT_FRAME_PROBE_OBJECT}"
    DEPENDS payload/CrtFrameProbe.S VERBATIM)
set_source_files_properties("${CRT_FRAME_PROBE_OBJECT}" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
target_sources(FpuProbePayload PRIVATE "${CRT_FRAME_PROBE_OBJECT}")
target_compile_features(FpuProbePayload PRIVATE cxx_std_17)
target_compile_definitions(FpuProbePayload PRIVATE NOMINMAX _WINSOCKAPI_=)
# runtimeobject provides RoInitialize/RoGetActivationFactory for the payload's
# LocalState log setup, same as the pre-migration host executable linked.
target_link_libraries(FpuProbePayload PRIVATE runtimeobject)
get_filename_component(UWP_TOOLS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set_property(TARGET FpuProbePayload APPEND PROPERTY LINK_DEPENDS "${UWP_TOOLS_DIR}/lib/arm-crtstart.lib")
