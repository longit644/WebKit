#pragma once
#include <stddef.h>

// Native bridge: all calls for a page belong to its creating engine thread.
// No WebCore/STL ownership crosses this ABI boundary.
typedef void (__stdcall *WKPageLogUWP)(const char*, unsigned long);
struct WKPageStateUWP {
    unsigned width, height;
    int scrollX, scrollY;
    int contentWidth, contentHeight;
};
extern "C" {
#if defined(BUILDING_WebCore)
#define WK_PAGE_UWP_EXPORT __declspec(dllexport)
#else
#define WK_PAGE_UWP_EXPORT
#endif
WK_PAGE_UWP_EXPORT unsigned __stdcall WKInitializeEngineUWP(const wchar_t* fonts, WKPageLogUWP);
WK_PAGE_UWP_EXPORT void* __stdcall WKCreatePageUWP(const wchar_t* fonts, unsigned width, unsigned height, WKPageLogUWP, unsigned* result);
WK_PAGE_UWP_EXPORT unsigned __stdcall WKDestroyPageUWP(void*);
WK_PAGE_UWP_EXPORT unsigned __stdcall WKLoadHTMLUWP(void*, const char* utf8);
WK_PAGE_UWP_EXPORT unsigned __stdcall WKResizePageUWP(void*, unsigned width, unsigned height);
WK_PAGE_UWP_EXPORT unsigned __stdcall WKScrollPageUWP(void*, int dx, int dy);
WK_PAGE_UWP_EXPORT unsigned __stdcall WKPointerPageUWP(void*, unsigned type, double x, double y, unsigned modifiers);
WK_PAGE_UWP_EXPORT unsigned __stdcall WKTickPageUWP(void*);
WK_PAGE_UWP_EXPORT unsigned __stdcall WKPaintPageUWP(void*, unsigned char* bgra, size_t capacity, unsigned stride);
WK_PAGE_UWP_EXPORT unsigned __stdcall WKPaintPageScaledUWP(void*, unsigned char* bgra, size_t capacity, unsigned stride, double scaleX, double scaleY);
WK_PAGE_UWP_EXPORT unsigned __stdcall WKPageNeedsPaintUWP(void*, unsigned* needsPaint);
WK_PAGE_UWP_EXPORT unsigned __stdcall WKGetPageStateUWP(void*, WKPageStateUWP*);
WK_PAGE_UWP_EXPORT unsigned __stdcall WKEnableCompositorUWP(void*, void* nativePanel, double scaleX, double scaleY);
WK_PAGE_UWP_EXPORT unsigned __stdcall WKDisableCompositorUWP(void*);
WK_PAGE_UWP_EXPORT unsigned __stdcall WKRenderCompositorUWP(void*, double overscrollY);
// Returns an HRESULT; dispatcher is an IInspectable exposing ICoreDispatcher.
WK_PAGE_UWP_EXPORT long __stdcall WKInstallClipboardDispatcherUWP(void*, void* dispatcher);
#undef WK_PAGE_UWP_EXPORT
}
