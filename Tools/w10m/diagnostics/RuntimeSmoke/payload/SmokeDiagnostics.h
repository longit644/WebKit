#pragma once

// Declared smoke-payload signatures for the C# RuntimeSmoke host (M10 8.5).
// Migration-map collision 3 required this file: SmokeHost.cpp used to resolve
// WKRenderFirstPageUWP with an inline reinterpret_cast that no header could
// check, so signature drift was invisible. The payload driver below calls the
// engine export only through WKRenderFirstPageUWPType, whose spelling is
// verified against FirstPageW10M.cpp by compare-runtimesmoke-semantics.py.
#include <windows.h>

// Log sink shared by the payload driver and the engine snapshot render.
typedef void(__stdcall* WKSmokeLogUWP)(const char* stage, unsigned long result);

// Smoke-only first-page snapshot render exported by WebCore. Takes the staged
// font directory, the caller BGRA pixels, the snapshot extent and the log
// sink; returns a FirstPageResultUWP code. Zero is success.
typedef unsigned(__stdcall* WKRenderFirstPageUWPType)(
    const wchar_t* fontDirectory,
    unsigned char* pixels,
    unsigned width,
    unsigned height,
    WKSmokeLogUWP log);

#ifdef __cplusplus
extern "C" {
#endif

// Runs the full native diagnostics sequence on the calling thread: CRT probe,
// JavaScriptCore evaluation, WebCore load, one-frame first-page render and
// D3D11 presentation. The caller must be the owning UI/engine thread, matching
// the version 1.0.0.10 single-snapshot discipline. fontDirectory is the staged
// Fonts directory, logPath receives the runtime-smoke.txt protocol lines,
// width and height are the snapshot extent in pixels, and coreWindowUnknown is
// the owning CoreWindow as an IUnknown pointer (may be null, in which case
// D3D presentation is skipped but every earlier stage still runs). Returns 0
// when every required stage succeeded and 1 otherwise.
__declspec(dllexport) unsigned __stdcall WKRunSmokeDiagnostics(
    const wchar_t* fontDirectory,
    const wchar_t* logPath,
    unsigned width,
    unsigned height,
    void* coreWindowUnknown);

#ifdef __cplusplus
} // extern "C"
#endif
