#pragma once
#include "PageUWP.h"
#include "WebKitWebViewPresenterUWP.h"
#include <windows.ui.xaml.h>
#include <windows.ui.xaml.controls.h>
#include <windows.ui.xaml.input.h>
#include <windows.ui.xaml.media.h>
#include <memory>
#include <array>

namespace WebKit {

// Reusable native wrapper exposing a XAML UserControl backed by SwapChainPanel.
// Construct, use and dispose on the same XAML/engine UI thread. This initial
// API is native C++; WinMD/custom-XAML-type registration is a later packaging step.
class WebKitWebViewUWP {
public:
    ~WebKitWebViewUWP();
    HRESULT initialize(const wchar_t* fonts, WKPageLogUWP);
    ABI::Windows::UI::Xaml::IUIElement* xamlElement() const { return m_rootElement.Get(); }
    HRESULT loadHTML(const char* utf8);
    HRESULT scrollBy(int dx, int dy);
    HRESULT pageState(WKPageStateUWP&);
    HRESULT repaint();
    HRESULT savePNG(const wchar_t* filename);
private:
    HRESULT resize(unsigned width, unsigned height);
    HRESULT useFallbackPresenter();
    HRESULT pointer(unsigned type, ABI::Windows::UI::Xaml::Input::IPointerRoutedEventArgs*);
    void report(const char*, HRESULT);
    HRESULT advanceFrame();
    HRESULT applyScroll(int dx, int dy, bool elastic, bool kinetic);
    void resetMotion();
    double now() const;
    void recordFrame(double paintMS);
    void recordFramePacing(double started, double scrollFinished, double engineFinished, bool submitted);
    struct EngineAPI {
        decltype(&WKCreatePageUWP) create;
        decltype(&WKDestroyPageUWP) destroy;
        decltype(&WKLoadHTMLUWP) loadHTML;
        decltype(&WKResizePageUWP) resize;
        decltype(&WKScrollPageUWP) scroll;
        decltype(&WKPointerPageUWP) pointer;
        decltype(&WKTickPageUWP) tick;
        decltype(&WKPaintPageUWP) paint;
        decltype(&WKPaintPageScaledUWP) paintScaled;
        decltype(&WKPageNeedsPaintUWP) needsPaint;
        decltype(&WKGetPageStateUWP) state;
        decltype(&WKEnableCompositorUWP) enableCompositor;
        decltype(&WKDisableCompositorUWP) disableCompositor;
        decltype(&WKRenderCompositorUWP) renderCompositor;
        decltype(&WKInstallClipboardDispatcherUWP) installClipboardDispatcher;
    } m_api { };
    HMODULE m_engine { nullptr };
    void* m_page { nullptr };
    DWORD m_owner { 0 };
    WKPageLogUWP m_log { nullptr };
    Microsoft::WRL::ComPtr<ABI::Windows::UI::Xaml::IUIElement> m_rootElement;
    Microsoft::WRL::ComPtr<ABI::Windows::UI::Xaml::IUIElement> m_panelElement;
    Microsoft::WRL::ComPtr<ABI::Windows::UI::Xaml::IUIElement> m_inputElement;
    Microsoft::WRL::ComPtr<ABI::Windows::UI::Xaml::Media::ICompositionTargetStatics> m_composition;
    Microsoft::WRL::ComPtr<ABI::Windows::UI::Xaml::Media::IRectangleGeometry> m_clip;
    EventRegistrationToken m_sizeToken { }, m_pressToken { }, m_moveToken { }, m_releaseToken { }, m_wheelToken { }, m_renderToken { }, m_scaleToken { }, m_cancelToken { };
    WebKitWebViewPresenterUWP m_presenter;
    std::unique_ptr<unsigned char[]> m_pixels;
    unsigned m_width { 1 }, m_height { 1 };
    unsigned m_pixelWidth { 1 }, m_pixelHeight { 1 };
    float m_scaleX { 1 }, m_scaleY { 1 };
    bool m_presenterReady { false }, m_inPaint { false }, m_pointerDown { false }, m_dragging { false };
    bool m_compositorActive { false }, m_compositorAttempted { false }, m_surfaceReady { false };
    double m_startX { 0 }, m_startY { 0 }, m_lastY { 0 };
    double m_pendingScrollX { 0 }, m_pendingScrollY { 0 };
    bool m_redrawPending { true };
    double m_counterFrequency { 1 }, m_lastPointerTime { 0 }, m_lastMotionTime { 0 }, m_lastFrameTime { 0 };
    double m_velocityY { 0 }, m_overscrollY { 0 }, m_springVelocity { 0 };
    bool m_inertia { false }, m_pendingElastic { false };
    unsigned m_profileFrames { 0 };
    double m_profileStart { 0 }, m_paintTotal { 0 }, m_uploadTotal { 0 }, m_copyTotal { 0 }, m_presentTotal { 0 }, m_frameMax { 0 };
    std::array<double, 64> m_frameWorkSamples { }, m_frameIntervalSamples { };
    unsigned m_frameWorkCount { 0 }, m_frameIntervalCount { 0 }, m_frameWarmupCount { 0 };
    unsigned m_frameOverBudget { 0 }, m_frameLongIntervals { 0 };
    bool m_previousFrameSubmitted { false };
    double m_previousFrameStart { 0 }, m_scrollWorkTotal { 0 }, m_engineWorkTotal { 0 }, m_renderWorkTotal { 0 };
};
}
