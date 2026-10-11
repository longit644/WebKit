#pragma once
#include "PageW10M.h"
#include "WebKitWebViewPresenterW10M.h"
#include "input/InputAdapterW10M.h"
#include "rendering/FrameSchedulerW10M.h"
#include "rendering/RenderSubscriptionW10M.h"
#include <memory>
#include <windows.ui.xaml.controls.h>
#include <windows.ui.xaml.h>
#include <windows.ui.xaml.input.h>
#include <windows.ui.xaml.media.h>

namespace WebKit {

// Reusable native wrapper exposing a XAML UserControl backed by SwapChainPanel.
// Construct, use and dispose on the same XAML/engine UI thread. This initial
// API is native C++; WinMD/custom-XAML-type registration is a later packaging step.
class WebKitWebViewW10M {
public:
    // Tears down the page, revokes XAML subscriptions and unloads the engine module.
    ~WebKitWebViewW10M();

    // Binds WebCore.dll, creates the page and loads the font directory; same UI thread throughout.
    HRESULT initialize(const wchar_t* fonts, WKPageLogW10M);

    // Root element for the host to parent; null until initialize succeeds.
    ABI::Windows::UI::Xaml::IUIElement* xamlElement() const { return m_rootElement.Get(); }

    // Shows app-supplied markup; never touches history and never shows the blocked page.
    HRESULT loadHTML(const char* utf8);

    // Starts an async http(s) navigation verified against the packaged CA bundle.
    HRESULT navigateRemote(const char* utf8Url, const wchar_t* caPath);

    // Steps back in session history; S_FALSE when already at the oldest entry.
    HRESULT goBack();

    // Steps forward in session history; S_FALSE when already at the newest entry.
    HRESULT goForward();

    // Re-requests the current document while keeping history and scroll intent.
    HRESULT reload();

    // Cancels the in-flight load; a committed document stays on screen.
    HRESULT stop();

    // Installs the host veto consulted synchronously at navigation policy time.
    HRESULT setNavigationPolicyHandler(WKNavigationPolicyCallback, void* context);

    // Scrolls by CSS pixels; negative values move up or left.
    HRESULT scrollBy(int dx, int dy);

    // Snapshots scroll offsets, content size and Back/Forward availability.
    HRESULT pageState(WKPageStateW10M&);

    // Requests one engine tick plus present on the next Rendering callback.
    HRESULT repaint();

    // Pause or resume Rendering callbacks for app suspension without closing.
    HRESULT setSuspended(bool suspended);

    // Writes the last presented pixels to a file for on-device diagnosis.
    HRESULT savePNG(const wchar_t* filename);

    // Forces CPU fallback, presents, rebuilds the D3D device and presents
    // again; diagnostic-only lost-device drill reporting each stage. Leaves
    // the compositor disabled; rotation/matrix checks must run first.
    HRESULT recoverPresenterForDiagnostics();

private:
    // Rebuilds the backing store and clip when the control size changes.
    HRESULT resize(unsigned width, unsigned height);

    // Falls back to the software presenter when the compositor cannot start.
    HRESULT useFallbackPresenter();

    // Routes a XAML pointer event (type selects press/move/release/wheel) to the engine.
    HRESULT pointer(unsigned type, ABI::Windows::UI::Xaml::Input::IPointerRoutedEventArgs*);

    // Emits one engine-log line; drops it when no log sink is installed.
    void report(const char*, HRESULT);

    // Advances inertia, ticks the engine and presents when dirty.
    HRESULT advanceFrame();

    // Clears gesture and inertia state on pointer release or navigation.
    void resetMotion();

    // Seconds since boot from the performance counter for gesture physics.
    double now() const;

    // Records one paint duration for the rolling frame profile.
    void recordFrame(double paintMS);

    // Splits one frame into scroll/engine/render cost to find over-budget stages.
    void recordFramePacing(double started, double scrollFinished, double engineFinished, bool submitted);

    // Dynamically bound WebCore entry points; keeps the DLL boundary stable across engine rebuilds.
    struct EngineAPI {

        // Creates the backing page object owned by this wrapper.
        decltype(&WKCreatePageW10M) create;

        // Destroys the backing page created above.
        decltype(&WKDestroyPageW10M) destroy;

        // Loads app-supplied markup into the page.
        decltype(&WKLoadHTMLW10M) loadHTML;

        // Starts an async remote navigation with the given CA bundle.
        decltype(&WKNavigatePageW10M) navigate;

        // Steps the page back in session history.
        decltype(&WKGoBackPageW10M) goBack;

        // Steps the page forward in session history.
        decltype(&WKGoForwardPageW10M) goForward;

        // Re-requests the current document.
        decltype(&WKReloadPageW10M) reload;

        // Cancels the in-flight load.
        decltype(&WKStopPageW10M) stop;

        // Installs the navigation policy callback and its context.
        decltype(&WKSetNavigationPolicyHandlerPageW10M) setPolicyHandler;

        // Rebuilds the backing store at the new size.
        decltype(&WKResizePageW10M) resize;

        // Scrolls the page by the given delta.
        decltype(&WKScrollPageW10M) scroll;

        // Delivers a pointer event to the page.
        decltype(&WKPointerPageW10M) pointer;

        // Runs timers, inertia and network pump for one frame.
        decltype(&WKTickPageW10M) tick;

        // Paints the page into the presenter surface.
        decltype(&WKPaintPageW10M) paint;

        // Paints with a DPI scale applied for high-density displays.
        decltype(&WKPaintPageScaledW10M) paintScaled;

        // Reports whether a repaint is pending.
        decltype(&WKPageNeedsPaintW10M) needsPaint;

        // Snapshots scroll, content and history state.
        decltype(&WKGetPageStateW10M) state;

        // Starts the GPU compositor path.
        decltype(&WKEnableCompositorW10M) enableCompositor;

        // Stops the GPU compositor path.
        decltype(&WKDisableCompositorW10M) disableCompositor;

        // Presents one compositor frame.
        decltype(&WKRenderCompositorW10M) renderCompositor;

        // Lets the page service copy/cut/paste through the host dispatcher.
        decltype(&WKInstallClipboardDispatcherW10M) installClipboardDispatcher;
    } m_api {};

    // Loaded WebCore.dll handle; null until initialize binds it.
    HMODULE m_engine { nullptr };

    // Opaque backing page from EngineAPI::create; null when torn down.
    void* m_page { nullptr };

    // UI thread that owns this wrapper; guards against cross-thread use.
    DWORD m_owner { 0 };

    // Engine-log sink installed by initialize; null means quiet.
    WKPageLogW10M m_log { nullptr };

    // Hosted XAML root handed out by xamlElement.
    Microsoft::WRL::ComPtr<ABI::Windows::UI::Xaml::IUIElement> m_rootElement;

    // SwapChainPanel surface the engine paints into.
    Microsoft::WRL::ComPtr<ABI::Windows::UI::Xaml::IUIElement> m_panelElement;

    // Transparent overlay that captures pointer input above the surface.
    Microsoft::WRL::ComPtr<ABI::Windows::UI::Xaml::IUIElement> m_inputElement;

    // Composition statics used to drive the Rendering callback.
    Microsoft::WRL::ComPtr<ABI::Windows::UI::Xaml::Media::ICompositionTargetStatics> m_composition;

    // Clips painting to the control bounds during resize.
    Microsoft::WRL::ComPtr<ABI::Windows::UI::Xaml::Media::IRectangleGeometry> m_clip;

    // XAML subscriptions in size/press/move/release/wheel/loaded/unloaded/scale/cancel order; revoked on teardown.
    EventRegistrationToken m_sizeToken {}, m_pressToken {}, m_moveToken {}, m_releaseToken {}, m_wheelToken {}, m_loadedToken {}, m_unloadedToken {}, m_scaleToken {}, m_cancelToken {};

    // Adapts CompositionTarget subscription to the render-subscription state machine.
    struct RenderPortW10M final : RenderHookW10M {
        // Subscribe the stored tick handler, writing exactly one token.
        HRESULT subscribe(EventRegistrationToken&) override;
        // Revoke one previously issued Rendering token.
        HRESULT unsubscribe(EventRegistrationToken) override;
        // Owning wrapper; null until initialize wires the port.
        WebKitWebViewW10M* view { nullptr };
        // Tick callback kept alive for the subscription lifetime.
        Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IEventHandler<IInspectable*>> tick;
    };

    // Exactly-once Rendering subscription across unload/load, suspension and Close.
    RenderSubscriptionW10M m_renderHooks;
    // Bound CompositionTarget port; its view back-pointer is set by initialize.
    RenderPortW10M m_renderPort;

    // Owns the SwapChainPanel and its DirectX swap chain.
    WebKitWebViewPresenterW10M m_presenter;

    // Last presented pixels for savePNG readback.
    std::unique_ptr<unsigned char[]> m_pixels;

    // Control size in CSS pixels driving layout.
    unsigned m_width { 1 }, m_height { 1 };

    // Backing-store size in physical pixels after DPI scaling.
    unsigned m_pixelWidth { 1 }, m_pixelHeight { 1 };

    // DPI scale applied on top of the CSS size.
    float m_scaleX { 1 }, m_scaleY { 1 };

    // Presenter up, paint reentrancy guard.
    bool m_presenterReady { false }, m_inPaint { false };

    // Per-control pointer gesture classification; physics lives in m_scroller.
    InputAdapterW10M m_input;

    // Per-control kinetic/edge scroll physics behind the gesture classification.
    ScrollControllerW10M m_scroller;

    // Narrow engine ports adapting the bound legacy table to the input classes.
    struct ScrollPortW10M final : ScrollEngineW10M {
        // Scroll by CSS pixels through the bound page.
        HRESULT scroll(int dx, int dy) override;
        // Snapshot offset, document and viewport heights from the bound page.
        HRESULT snapshot(ScrollSnapshotW10M&) override;
        // Owning wrapper; null until initialize binds the engine table.
        WebKitWebViewW10M* view { nullptr };
    };

    // Tap port delivering press/release pairs to the bound page.
    struct TapPortW10M final : TapEngineW10M {
        // Deliver a tap press at viewport CSS pixels.
        HRESULT tapPress(double xCssPixels, double yCssPixels) override;
        // Deliver the matching tap release.
        HRESULT tapRelease(double xCssPixels, double yCssPixels) override;
        // Owning wrapper; null until initialize binds the engine table.
        WebKitWebViewW10M* view { nullptr };
    };

    // Bound scroll/tap ports; their view back-pointer is set by initialize.
    ScrollPortW10M m_scrollPort;
    TapPortW10M m_tapPort;

    // Compositor running, compositor tried once, surface ready for present.
    bool m_compositorActive { false }, m_compositorAttempted { false }, m_surfaceReady { false };

    // Forces a repaint even when nothing reports dirty.
    bool m_redrawPending { true };

    // Performance-counter frequency for gesture physics and frame pacing.
    double m_counterFrequency { 1 };

    // Instance-owned extracted pacing and CPU submission windows with explicit units.
    FrameMetricsW10M m_frameMetrics;
    // Per-view eligibility backed by the process-shared negotiated service slot.
    FrameSchedulerW10M m_frameScheduler;
};
}


