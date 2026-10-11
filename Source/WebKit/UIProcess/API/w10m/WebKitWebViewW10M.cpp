#include "WebKitWebViewW10M.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <wincodec.h>
#include <windows.ui.input.h>
#include <windows.ui.xaml.markup.h>
#include <windows.ui.xaml.media.dxinterop.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>

namespace WebKit {
using namespace Microsoft::WRL;
using Microsoft::WRL::Wrappers::HStringReference;
namespace Xaml = ABI::Windows::UI::Xaml;
namespace Controls = ABI::Windows::UI::Xaml::Controls;
namespace Input = ABI::Windows::UI::Xaml::Input;
namespace Foundation = ABI::Windows::Foundation;

// Maps the engine's WKPageResultW10M contract to HRESULT for XAML callers.
static HRESULT bridgeResult(unsigned result)
{
    if (result == WKPageResultWrongThreadW10M)
        return RPC_E_WRONG_THREAD;
    if (result == WKPageResultInvalidArgumentW10M)
        return E_INVALIDARG;
    return result ? E_FAIL : S_OK;
}

// Engine module loaded from the package; LoadPackagedLibrary flags are reserved zero.
constexpr wchar_t kEngineModuleName[] = L"WebCore.dll";
// Placeholder extent until the first SizeChanged reports the real control size.
constexpr unsigned kPlaceholderExtent = 1;
// Scale reported in thousandths for integer log lines.
constexpr double kScaleToMilli = 1000.0;
// Smallest logical control size accepted from XAML SizeChanged.
constexpr float kMinimumControlExtent = 1.0f;
// Conversion from copied millisecond metrics to the retained diagnostic microseconds.
constexpr double kMicrosecondsPerMillisecond = 1000.0;
// Neutral DPI scale used before the first composition-scale report arrives.
constexpr float kIdentityCompositionScale = 1.0f;
// XAML surface markup and matching FindName key.
constexpr wchar_t kSurfaceElementName[] = L"Surface";
// Transparent input grid hosting the named composition surface above.
constexpr wchar_t kInputSurfaceMarkup[] = L"<Grid xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' Background='Transparent'><SwapChainPanel x:Name='Surface'/></Grid>";

// Emits one engine-log line; drops it when no log sink is installed.
void WebKitWebViewW10M::report(const char* stage, HRESULT result)
{
    if (m_log)
        m_log(stage, result);
}

// Scroll the bound legacy page by CSS pixels for the input controller.
HRESULT WebKitWebViewW10M::ScrollPortW10M::scroll(int dx, int dy)
{
    if (!view || !view->m_page)
        return RO_E_CLOSED;
    return bridgeResult(view->m_api.scroll(view->m_page, dx, dy));
}

// Snapshot offset/document/viewport heights for scroll-target clamping.
HRESULT WebKitWebViewW10M::ScrollPortW10M::snapshot(ScrollSnapshotW10M& output)
{
    if (!view || !view->m_page)
        return RO_E_CLOSED;
    WKPageStateW10M state { };
    HRESULT result = bridgeResult(view->m_api.state(view->m_page, &state));
    if (FAILED(result))
        return result;
    output = { static_cast<int>(state.scrollY), static_cast<int>(state.contentHeight), static_cast<int>(state.height) };
    return S_OK;
}

// Deliver one tap press through the bound legacy page.
HRESULT WebKitWebViewW10M::TapPortW10M::tapPress(double xCssPixels, double yCssPixels)
{
    if (!view || !view->m_page)
        return RO_E_CLOSED;
    return bridgeResult(view->m_api.pointer(view->m_page, WKPointerEventPressW10M, xCssPixels, yCssPixels, WKPointerModifierNoneW10M));
}

// Deliver the matching tap release through the bound legacy page.
HRESULT WebKitWebViewW10M::TapPortW10M::tapRelease(double xCssPixels, double yCssPixels)
{
    if (!view || !view->m_page)
        return RO_E_CLOSED;
    return bridgeResult(view->m_api.pointer(view->m_page, WKPointerEventReleaseW10M, xCssPixels, yCssPixels, WKPointerModifierNoneW10M));
}

// Subscribe the stored tick handler through the owner's CompositionTarget.
HRESULT WebKitWebViewW10M::RenderPortW10M::subscribe(EventRegistrationToken& token)
{
    if (!view)
        return RO_E_CLOSED;
    return view->m_composition->add_Rendering(tick.Get(), &token);
}

// Revoke one Rendering token through the owner's CompositionTarget.
HRESULT WebKitWebViewW10M::RenderPortW10M::unsubscribe(EventRegistrationToken token)
{
    if (!view)
        return RO_E_CLOSED;
    return view->m_composition->remove_Rendering(token);
}

// Pause or resume Rendering callbacks for app suspension without closing.
HRESULT WebKitWebViewW10M::setSuspended(bool suspended)
{
    if (GetCurrentThreadId() != m_owner)
        return RPC_E_WRONG_THREAD;
    HRESULT result = m_renderHooks.setSuspended(suspended);
    report(suspended ? "control-suspended" : "control-resumed", result);
    return result;
}

// Binds WebCore.dll, creates the page and builds the XAML tree. Fails when
// any entry point, control or subscription is missing; nothing half-built stays.
HRESULT WebKitWebViewW10M::initialize(const wchar_t* fonts, WKPageLogW10M log)
{
    if (m_page || !fonts)
        return E_INVALIDARG;
    m_owner = GetCurrentThreadId();
    LARGE_INTEGER frequency;
    if (!QueryPerformanceFrequency(&frequency))
        return HRESULT_FROM_WIN32(GetLastError());
    m_counterFrequency = static_cast<double>(frequency.QuadPart);
    m_log = log;
    // Route scroll-physics diagnostics through the same engine-log sink.
    m_scroller.setReportSink([this](const char* stage) { report(stage, S_OK); });
    m_scrollPort.view = this;
    m_tapPort.view = this;
    m_engine = LoadPackagedLibrary(kEngineModuleName, 0);
    if (!m_engine)
        return HRESULT_FROM_WIN32(GetLastError());
#define BIND(field, symbol)                                                                   \
    m_api.field = reinterpret_cast<decltype(m_api.field)>(GetProcAddress(m_engine, #symbol)); \
    if (!m_api.field)                                                                         \
    return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND)
    BIND(create, WKCreatePageW10M);
    BIND(destroy, WKDestroyPageW10M);
    BIND(loadHTML, WKLoadHTMLW10M);
    BIND(navigate, WKNavigatePageW10M);
    BIND(goBack, WKGoBackPageW10M);
    BIND(goForward, WKGoForwardPageW10M);
    BIND(reload, WKReloadPageW10M);
    BIND(stop, WKStopPageW10M);
    BIND(setPolicyHandler, WKSetNavigationPolicyHandlerPageW10M);
    BIND(resize, WKResizePageW10M);
    BIND(scroll, WKScrollPageW10M);
    BIND(pointer, WKPointerPageW10M);
    BIND(tick, WKTickPageW10M);
    BIND(paint, WKPaintPageW10M);
    BIND(paintScaled, WKPaintPageScaledW10M);
    BIND(needsPaint, WKPageNeedsPaintW10M);
    BIND(state, WKGetPageStateW10M);
    BIND(enableCompositor, WKEnableCompositorW10M);
    BIND(disableCompositor, WKDisableCompositorW10M);
    BIND(renderCompositor, WKRenderCompositorW10M);
    BIND(installClipboardDispatcher, WKInstallClipboardDispatcherW10M);
#undef BIND
    unsigned result;
    m_page = m_api.create(fonts, kPlaceholderExtent, kPlaceholderExtent, m_log, &result);
    if (!m_page)
        return bridgeResult(result ? result : WKPageResultInvalidArgumentW10M);
    // Render ticks are page-local; bind process service only through the negotiated table.
    auto discover = reinterpret_cast<decltype(&WKW10MGetEngineAPI)>(GetProcAddress(m_engine, "WKW10MGetEngineAPI"));
    if (!discover) return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    WKW10MEngineAPI serviceAPI { };
    serviceAPI.header = { sizeof(serviceAPI), WKW10M_STRUCTURE_VERSION };
    auto negotiated = discover(1, 0, 0, &serviceAPI);
    if (negotiated != WKW10MResultOK) return E_FAIL;
    std::shared_ptr<OwnerThreadServicePumpW10M> servicePump;
    auto serviceResult = OwnerThreadServicePumpW10M::acquire(&serviceAPI, servicePump);
    if (FAILED(serviceResult) || FAILED(serviceResult = m_frameScheduler.attach(servicePump))) return serviceResult;
    m_frameScheduler.setPresentationEnabled(true);

    ComPtr<Controls::IUserControlFactory> controlFactory;
    ComPtr<Controls::IUserControl> control;
    ComPtr<IInspectable> inner;
    HRESULT hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_UI_Xaml_Controls_UserControl).Get(), IID_PPV_ARGS(&controlFactory));
    if (FAILED(hr) || FAILED(hr = controlFactory->CreateInstance(nullptr, &inner, &control))
        || FAILED(hr = control.As(&m_rootElement)))
        return hr;
    ComPtr<Xaml::Markup::IXamlReaderStatics> reader;
    ComPtr<Controls::ISwapChainPanel> panel;
    ComPtr<IInspectable> panelObject;
    ComPtr<IInspectable> inputObject;
    ComPtr<Xaml::IFrameworkElement> inputFramework;
    // Single UI template: transparent input grid over the engine surface.
    // FindName below must match the markup's x:Name.
    hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_UI_Xaml_Markup_XamlReader).Get(), IID_PPV_ARGS(&reader));
    if (FAILED(hr) || FAILED(hr = reader->Load(HStringReference(kInputSurfaceMarkup).Get(), &inputObject))
        || FAILED(hr = inputObject.As(&inputFramework))
        || FAILED(hr = inputFramework->FindName(HStringReference(kSurfaceElementName).Get(), &panelObject))
        || FAILED(hr = panelObject.As(&panel))
        || FAILED(hr = panel.As(&m_panelElement)) || FAILED(hr = inputObject.As(&m_inputElement)))
        return hr;
    if (FAILED(hr = control->put_Content(m_inputElement.Get())))
        return hr;
    ComPtr<Xaml::IDependencyObject> dependency;
    ComPtr<ABI::Windows::UI::Core::ICoreDispatcher> clipboardDispatcher;
    ComPtr<IInspectable> clipboardDispatcherObject;
    if (FAILED(hr = m_rootElement.As(&dependency))
        || FAILED(hr = dependency->get_Dispatcher(&clipboardDispatcher))
        || FAILED(hr = clipboardDispatcher.As(&clipboardDispatcherObject)))
        return hr;
    hr = m_api.installClipboardDispatcher(m_page, clipboardDispatcherObject.Get());
    report("control-clipboard-dispatcher", hr);
    if (FAILED(hr))
        return hr;
    ComPtr<IInspectable> clip;
    if (FAILED(hr = RoActivateInstance(HStringReference(RuntimeClass_Windows_UI_Xaml_Media_RectangleGeometry).Get(), &clip))
        || FAILED(hr = clip.As(&m_clip))
        || FAILED(hr = m_inputElement->put_Clip(m_clip.Get())))
        return hr;
    report("control-panel-attached", S_OK);
    ComPtr<Xaml::IFrameworkElement> element;
    if (FAILED(hr = panel.As(&element)))
        return hr;
    auto sizeChanged = Callback<Xaml::ISizeChangedEventHandler>([this](IInspectable*, Xaml::ISizeChangedEventArgs* args) -> HRESULT {
        Foundation::Size size;
        HRESULT hr = args->get_NewSize(&size);
        if (FAILED(hr) || size.Width < kMinimumControlExtent || size.Height < kMinimumControlExtent)
            return hr;
        hr = resize(static_cast<unsigned>(size.Width), static_cast<unsigned>(size.Height));
        report("control-resize", hr);
        return hr;
    });
    if (FAILED(hr = element->add_SizeChanged(sizeChanged.Get(), &m_sizeToken)))
        return hr;
    auto scaleChanged = Callback<Foundation::ITypedEventHandler<Controls::SwapChainPanel*, IInspectable*>>([this](Controls::ISwapChainPanel*, IInspectable*) -> HRESULT {
        HRESULT hr = resize(m_width, m_height);
        report("control-dpi-change", hr);
        return hr;
    });
    if (FAILED(hr = panel->add_CompositionScaleChanged(scaleChanged.Get(), &m_scaleToken)))
        return hr;
    auto press = Callback<Input::IPointerEventHandler>([this](IInspectable*, Input::IPointerRoutedEventArgs* args) -> HRESULT { return pointer(WKPointerEventPressW10M, args); });
    auto move = Callback<Input::IPointerEventHandler>([this](IInspectable*, Input::IPointerRoutedEventArgs* args) -> HRESULT { return pointer(WKPointerEventMoveW10M, args); });
    auto release = Callback<Input::IPointerEventHandler>([this](IInspectable*, Input::IPointerRoutedEventArgs* args) -> HRESULT { return pointer(WKPointerEventReleaseW10M, args); });
    auto wheel = Callback<Input::IPointerEventHandler>([this](IInspectable*, Input::IPointerRoutedEventArgs* args) -> HRESULT {
        ComPtr<ABI::Windows::UI::Input::IPointerPoint> point;
        ComPtr<ABI::Windows::UI::Input::IPointerPointProperties> properties;
        INT32 delta = 0;
        HRESULT hr = args->GetCurrentPoint(m_inputElement.Get(), &point);
        if (FAILED(hr) || FAILED(hr = point->get_Properties(&properties)) || FAILED(hr = properties->get_MouseWheelDelta(&delta)))
            return hr;
        args->put_Handled(true);
        hr = m_scroller.addWheelDelta(InputAdapterW10M::wheelDeltaToCssPixels(delta));
        if (FAILED(hr))
            return hr;
        m_redrawPending = true;
        return S_OK;
    });
    auto cancel = Callback<Input::IPointerEventHandler>([this](IInspectable*, Input::IPointerRoutedEventArgs*) -> HRESULT {
        return m_input.abort(m_scroller);
    });
    if (FAILED(hr = m_inputElement->add_PointerPressed(press.Get(), &m_pressToken))
        || FAILED(hr = m_inputElement->add_PointerMoved(move.Get(), &m_moveToken))
        || FAILED(hr = m_inputElement->add_PointerReleased(release.Get(), &m_releaseToken))
        || FAILED(hr = m_inputElement->add_PointerWheelChanged(wheel.Get(), &m_wheelToken))
        || FAILED(hr = m_inputElement->add_PointerCanceled(cancel.Get(), &m_cancelToken)))
        return hr;
    hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_UI_Xaml_Media_CompositionTarget).Get(), IID_PPV_ARGS(&m_composition));
    if (FAILED(hr))
        return hr;
    auto tick = Callback<Foundation::IEventHandler<IInspectable*>>([this](IInspectable*, IInspectable* args) -> HRESULT {
        if (!m_page || m_inPaint)
            return S_OK;
        ComPtr<Xaml::Media::IRenderingEventArgs> rendering;
        Foundation::TimeSpan time { };
        HRESULT hr = args ? args->QueryInterface(IID_PPV_ARGS(&rendering)) : E_INVALIDARG;
        if (SUCCEEDED(hr)) hr = rendering->get_RenderingTime(&time);
        if (SUCCEEDED(hr)) hr = m_frameScheduler.beginFrame(time.Duration);
        if (hr == S_FALSE) return S_OK;
        if (SUCCEEDED(hr)) hr = advanceFrame();
        if (FAILED(hr)) {
            report("control-frame-failed", hr);
            m_renderHooks.disableAfterFailure();
        }
        return S_OK;
    });
    m_renderPort.view = this;
    m_renderPort.tick = tick;
    if (FAILED(hr = m_renderHooks.attach(&m_renderPort)))
        return hr;
    ComPtr<Xaml::IFrameworkElement> rootFramework;
    if (FAILED(hr = control.As(&rootFramework)))
        return hr;
    auto loaded = Callback<Xaml::IRoutedEventHandler>([this](IInspectable*, Xaml::IRoutedEventArgs*) -> HRESULT {
        HRESULT hr = m_renderHooks.onLoaded();
        report("control-loaded", hr);
        return hr;
    });
    auto unloaded = Callback<Xaml::IRoutedEventHandler>([this](IInspectable*, Xaml::IRoutedEventArgs*) -> HRESULT {
        HRESULT hr = m_renderHooks.onUnloaded();
        report("control-unloaded", hr);
        return hr;
    });
    if (FAILED(hr = rootFramework->add_Loaded(loaded.Get(), &m_loadedToken))
        || FAILED(hr = rootFramework->add_Unloaded(unloaded.Get(), &m_unloadedToken)))
        return hr;
    return S_OK;
}

// Rebuilds the backing store, clip and presenter for the new control size.
// Restarts the compositor once on scale changes; no-ops when nothing moved.
HRESULT WebKitWebViewW10M::resize(unsigned width, unsigned height)
{
    if (GetCurrentThreadId() != m_owner)
        return RPC_E_WRONG_THREAD;
    if (!width || !height || width > W10MMaxPageDimension || height > W10MMaxPageDimension)
        return E_INVALIDARG;
    if (m_clip)
        m_clip->put_Rect({ 0, 0, static_cast<float>(width), static_cast<float>(height) });
    ComPtr<Controls::ISwapChainPanel> panel;
    float scaleX = kIdentityCompositionScale, scaleY = kIdentityCompositionScale;
    HRESULT hr = m_panelElement.As(&panel);
    if (FAILED(hr) || FAILED(hr = panel->get_CompositionScaleX(&scaleX)) || FAILED(hr = panel->get_CompositionScaleY(&scaleY)))
        return hr;
    if (!std::isfinite(scaleX) || !std::isfinite(scaleY) || scaleX <= 0 || scaleY <= 0 || scaleX > W10MMaxDeviceScale || scaleY > W10MMaxDeviceScale)
        return E_INVALIDARG;
    unsigned pixelWidth = static_cast<unsigned>(std::ceil(width * scaleX));
    unsigned pixelHeight = static_cast<unsigned>(std::ceil(height * scaleY));
    if (pixelWidth > W10MMaxPaintDimension || pixelHeight > W10MMaxPaintDimension)
        return E_INVALIDARG;
    if (m_surfaceReady && width == m_width && height == m_height && scaleX == m_scaleX && scaleY == m_scaleY)
        return S_OK;
    resetMotion();
    if (m_compositorActive && (scaleX != m_scaleX || scaleY != m_scaleY)) {
        hr = bridgeResult(m_api.disableCompositor(m_page));
        if (FAILED(hr))
            return hr;
        m_compositorActive = false;
    }
    hr = bridgeResult(m_api.resize(m_page, width, height));
    if (FAILED(hr))
        return hr;
    m_width = width;
    m_height = height;
    m_pixels.reset();
    m_pixelWidth = pixelWidth;
    m_pixelHeight = pixelHeight;
    m_scaleX = scaleX;
    m_scaleY = scaleY;
    if (!m_compositorAttempted) {
        m_compositorAttempted = true;
        // CPU attachment must be released before an engine-owned surface can attach.
        hr = m_presenter.detach();
        if (FAILED(hr)) return hr;
        m_presenterReady = false;
        ComPtr<IInspectable> nativePanel;
        hr = m_panelElement.As(&nativePanel);
        if (SUCCEEDED(hr))
            hr = bridgeResult(m_api.enableCompositor(m_page, nativePanel.Get(), scaleX, scaleY));
        m_compositorActive = SUCCEEDED(hr);
        report("control-compositor-initialize", hr);
    }
    if (!m_compositorActive && FAILED(hr = useFallbackPresenter()))
        return hr;
    m_surfaceReady = true;
    report("control-viewport-width", width);
    report("control-viewport-height", height);
    report("control-pixel-width", pixelWidth);
    report("control-pixel-height", pixelHeight);
    report("control-scale-x-milli", static_cast<unsigned>(scaleX * kScaleToMilli));
    // ANGLE receives its own SizeChanged notification after this handler returns.
    // Present on the next SDK Rendering callback, once every panel listener has
    // installed the new geometry, rather than painting into the old surface here.
    m_redrawPending = true;
    return S_OK;
}

// Drops to the software presenter when the compositor cannot run.
HRESULT WebKitWebViewW10M::useFallbackPresenter()
{
    HRESULT hr = bridgeResult(m_api.disableCompositor(m_page));
    if (FAILED(hr))
        return hr;
    m_compositorActive = false;
    m_pixels = std::make_unique<unsigned char[]>(static_cast<size_t>(m_pixelWidth) * m_pixelHeight * W10MBytesPerBGRA);
    if (m_presenterReady)
        hr = m_presenter.resize(m_pixelWidth, m_pixelHeight);
    else
        hr = m_presenter.initializeForSwapChainPanel(m_panelElement.Get(), m_pixelWidth, m_pixelHeight);
    if ((hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) && m_presenterReady) {
        // A retained dead chain reports removal on resize; rebuild it on the
        // retained panel and geometry, then continue with scale setup below.
        hr = m_presenter.recoverDevice();
        report("control-presenter-recover", hr);
    }
    if (SUCCEEDED(hr))
        hr = m_presenter.setCompositionScale(m_scaleX, m_scaleY);
    if (SUCCEEDED(hr))
        hr = m_presenter.setCompositionOffset(0, static_cast<float>(m_scroller.overscrollY()));
    m_presenterReady = SUCCEEDED(hr);
    report("control-cairo-fallback", hr);
    return hr;
}

// Shows app-supplied markup and resets gesture, profiler and present state.
HRESULT WebKitWebViewW10M::loadHTML(const char* utf8)
{
    if (GetCurrentThreadId() != m_owner)
        return RPC_E_WRONG_THREAD;
    HRESULT hr = bridgeResult(m_api.loadHTML(m_page, utf8));
    resetMotion();
    if (m_presenterReady)
        m_presenter.setCompositionOffset(0, 0);
    m_redrawPending = true;
    m_frameMetrics.resetPacing();
    return hr;
}

// Starts an async remote navigation and resets gesture, profiler and present state.
HRESULT WebKitWebViewW10M::navigateRemote(const char* utf8Url, const wchar_t* caPath)
{
    if (GetCurrentThreadId() != m_owner)
        return RPC_E_WRONG_THREAD;
    if (!utf8Url || !caPath)
        return E_INVALIDARG;
    HRESULT hr = bridgeResult(m_api.navigate(m_page, utf8Url, caPath));
    resetMotion();
    if (m_presenterReady)
        m_presenter.setCompositionOffset(0, 0);
    m_redrawPending = true;
    m_frameMetrics.resetPacing();
    report("control-navigate-remote", hr);
    return hr;
}

// Steps back in session history and re-arms the presenter.
HRESULT WebKitWebViewW10M::goBack()
{
    if (GetCurrentThreadId() != m_owner)
        return RPC_E_WRONG_THREAD;
    HRESULT hr = bridgeResult(m_api.goBack(m_page));
    resetMotion();
    m_redrawPending = true;
    report("control-go-back", hr);
    return hr;
}

// Steps forward in session history and re-arms the presenter.
HRESULT WebKitWebViewW10M::goForward()
{
    if (GetCurrentThreadId() != m_owner)
        return RPC_E_WRONG_THREAD;
    HRESULT hr = bridgeResult(m_api.goForward(m_page));
    resetMotion();
    m_redrawPending = true;
    report("control-go-forward", hr);
    return hr;
}

// Re-requests the current document and re-arms the presenter.
HRESULT WebKitWebViewW10M::reload()
{
    if (GetCurrentThreadId() != m_owner)
        return RPC_E_WRONG_THREAD;
    HRESULT hr = bridgeResult(m_api.reload(m_page));
    resetMotion();
    m_redrawPending = true;
    report("control-reload", hr);
    return hr;
}

// Cancels the in-flight load; a committed document stays on screen.
HRESULT WebKitWebViewW10M::stop()
{
    if (GetCurrentThreadId() != m_owner)
        return RPC_E_WRONG_THREAD;
    HRESULT hr = bridgeResult(m_api.stop(m_page));
    report("control-stop", hr);
    return hr;
}

// Installs the host veto consulted synchronously at navigation policy time.
HRESULT WebKitWebViewW10M::setNavigationPolicyHandler(WKNavigationPolicyCallback callback, void* context)
{
    if (GetCurrentThreadId() != m_owner)
        return RPC_E_WRONG_THREAD;
    return bridgeResult(m_api.setPolicyHandler(m_page, callback, context));
}

// Scrolls by CSS pixels through the engine and reports the new offset.
HRESULT WebKitWebViewW10M::scrollBy(int dx, int dy)
{
    if (GetCurrentThreadId() != m_owner)
        return RPC_E_WRONG_THREAD;
    resetMotion();
    if (m_presenterReady)
        m_presenter.setCompositionOffset(0, 0);
    HRESULT hr = bridgeResult(m_api.scroll(m_page, dx, dy));
    if (SUCCEEDED(hr)) {
        WKPageStateW10M state;
        if (m_api.state(m_page, &state) == WKPageResultOkW10M)
            report("control-scroll-y", state.scrollY);
    }
    m_redrawPending = true;
    return hr;
}

// Snapshots scroll offsets, content size and history availability.
HRESULT WebKitWebViewW10M::pageState(WKPageStateW10M& state)
{
    if (GetCurrentThreadId() != m_owner)
        return RPC_E_WRONG_THREAD;
    return bridgeResult(m_api.state(m_page, &state));
}

// Presents one frame via compositor or software paint; S_FALSE when nothing is ready.
HRESULT WebKitWebViewW10M::repaint()
{
    if (GetCurrentThreadId() != m_owner)
        return RPC_E_WRONG_THREAD;
    if (!m_surfaceReady || m_inPaint)
        return S_FALSE;
    m_inPaint = true;
    if (m_compositorActive) {
        HRESULT hr = bridgeResult(m_api.renderCompositor(m_page, m_scroller.overscrollY()));
        if (SUCCEEDED(hr)) {
            m_redrawPending = false;
            m_inPaint = false;
            return hr;
        }
        report("control-compositor-render-failed", hr);
        hr = useFallbackPresenter();
        if (FAILED(hr)) {
            m_inPaint = false;
            return hr;
        }
    }
    double start = now();
    HRESULT hr = bridgeResult(m_api.paintScaled(m_page, m_pixels.get(), static_cast<size_t>(m_pixelWidth) * m_pixelHeight * W10MBytesPerBGRA, m_pixelWidth * W10MBytesPerBGRA, m_scaleX, m_scaleY));
    double paintMS = (now() - start) * millisecondsPerSecondW10M;
    if (SUCCEEDED(hr))
        hr = m_presenter.present(m_pixels.get(), m_pixelWidth * W10MBytesPerBGRA);
    if ((hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) && m_presenterReady) {
        // The device died between frames; rebuild it once and re-present the
        // already-painted pixels instead of dropping the frame.
        HRESULT recovered = m_presenter.recoverDevice();
        report("control-presenter-recover", recovered);
        if (SUCCEEDED(recovered))
            hr = m_presenter.present(m_pixels.get(), m_pixelWidth * W10MBytesPerBGRA);
        else
            hr = recovered;
        report("control-presenter-recovered", hr);
    }
    if (SUCCEEDED(hr))
        m_redrawPending = false;
    if (SUCCEEDED(hr))
        recordFrame(paintMS);
    m_inPaint = false;
    return hr;
}

// Writes the last presented pixels to a PNG file for on-device diagnosis.
HRESULT WebKitWebViewW10M::savePNG(const wchar_t* filename)
{
    if (GetCurrentThreadId() != m_owner)
        return RPC_E_WRONG_THREAD;
    if (!filename || !m_surfaceReady)
        return E_INVALIDARG;
    if (!m_pixels)
        m_pixels = std::make_unique<unsigned char[]>(static_cast<size_t>(m_pixelWidth) * m_pixelHeight * W10MBytesPerBGRA);
    HRESULT snapshot = bridgeResult(m_api.paintScaled(m_page, m_pixels.get(), static_cast<size_t>(m_pixelWidth) * m_pixelHeight * W10MBytesPerBGRA, m_pixelWidth * W10MBytesPerBGRA, m_scaleX, m_scaleY));
    if (FAILED(snapshot)) {
        report("capture-page-snapshot", snapshot);
        return snapshot;
    }
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> options;
    ComPtr<IWICBitmap> bitmap;
    ComPtr<IWICFormatConverter> converter;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr) || FAILED(hr = factory->CreateStream(&stream))
        || FAILED(hr = stream->InitializeFromFilename(filename, GENERIC_WRITE))
        || FAILED(hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))
        || FAILED(hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))
        || FAILED(hr = encoder->CreateNewFrame(&frame, &options))
        || FAILED(hr = frame->Initialize(options.Get()))
        || FAILED(hr = frame->SetSize(m_pixelWidth, m_pixelHeight)))
        return hr;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(hr = frame->SetPixelFormat(&format))
        || FAILED(hr = factory->CreateBitmapFromMemory(m_pixelWidth, m_pixelHeight, GUID_WICPixelFormat32bppPBGRA, m_pixelWidth * W10MBytesPerBGRA, m_pixelWidth * m_pixelHeight * W10MBytesPerBGRA, m_pixels.get(), &bitmap))
        || FAILED(hr = factory->CreateFormatConverter(&converter))
        || FAILED(hr = converter->Initialize(bitmap.Get(), format, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))
        || FAILED(hr = frame->WriteSource(converter.Get(), nullptr))
        || FAILED(hr = frame->Commit()))
        return hr;
    return encoder->Commit();
}

// Forces CPU fallback, presents, rebuilds the D3D device and presents again;
// diagnostic-only lost-device drill reporting each stage. Leaves the
// compositor disabled; rotation/matrix checks must run first.
HRESULT WebKitWebViewW10M::recoverPresenterForDiagnostics()
{
    if (GetCurrentThreadId() != m_owner)
        return RPC_E_WRONG_THREAD;
    if (!m_surfaceReady)
        return E_ILLEGAL_METHOD_CALL;
    report("control-presenter-reason-before", m_presenter.deviceRemovedReason());
    HRESULT hr = S_OK;
    if (m_compositorActive) {
        hr = bridgeResult(m_api.disableCompositor(m_page));
        report("control-presenter-fallback-disable", hr);
        if (FAILED(hr))
            return hr;
        m_compositorActive = false;
    }
    if (!m_presenterReady) {
        hr = useFallbackPresenter();
        report("control-presenter-fallback-setup", hr);
        if (FAILED(hr))
            return hr;
    }
    double start = now();
    hr = bridgeResult(m_api.paintScaled(m_page, m_pixels.get(), static_cast<size_t>(m_pixelWidth) * m_pixelHeight * W10MBytesPerBGRA, m_pixelWidth * W10MBytesPerBGRA, m_scaleX, m_scaleY));
    if (SUCCEEDED(hr))
        hr = m_presenter.present(m_pixels.get(), m_pixelWidth * W10MBytesPerBGRA);
    report("control-presenter-before", hr);
    if (FAILED(hr))
        return hr;
    hr = m_presenter.recoverDevice();
    report("control-presenter-recover", hr);
    report("control-presenter-recoveries", m_presenter.deviceRecoveries());
    if (FAILED(hr))
        return hr;
    start = now();
    hr = bridgeResult(m_api.paintScaled(m_page, m_pixels.get(), static_cast<size_t>(m_pixelWidth) * m_pixelHeight * W10MBytesPerBGRA, m_pixelWidth * W10MBytesPerBGRA, m_scaleX, m_scaleY));
    if (SUCCEEDED(hr))
        hr = m_presenter.present(m_pixels.get(), m_pixelWidth * W10MBytesPerBGRA);
    double paintMS = (now() - start) * millisecondsPerSecondW10M;
    if (SUCCEEDED(hr))
        recordFrame(paintMS);
    report("control-presenter-after", hr);
    report("control-presenter-reason-after", m_presenter.deviceRemovedReason());
    return hr;
}

// Routes a XAML pointer event to press/move/release handling and the engine tap path.
HRESULT WebKitWebViewW10M::pointer(unsigned type, Input::IPointerRoutedEventArgs* args)
{
    ComPtr<ABI::Windows::UI::Input::IPointerPoint> point;
    Foundation::Point position;
    HRESULT hr = args->GetCurrentPoint(m_inputElement.Get(), &point);
    if (FAILED(hr) || FAILED(hr = point->get_Position(&position)))
        return hr;
    args->put_Handled(true);
    // GetCurrentPoint relative to a XAML element returns device-independent
    // coordinates, matching the WebCore viewport. Composition scale applies
    // to the backing surface, not to pointer positions.
    // Diagnostic tap-slop log value; gesture classification owns the real constant.
    constexpr double kTapSlopLogicalPx = 10.0;
    double slopY = kTapSlopLogicalPx;
    if (type == WKPointerEventPressW10M) {
        report("control-pointer-pressed", S_OK);
        report("ptr-press-x", static_cast<unsigned>(position.X));
        report("ptr-press-y", static_cast<unsigned>(position.Y));
        report("ptr-scale-milli", static_cast<unsigned>(m_scaleX * kScaleToMilli));
        report("ptr-slop", static_cast<unsigned>(slopY));
        hr = m_input.press(position.X, position.Y, now(), m_scroller);
        if (FAILED(hr))
            return hr;
        ComPtr<Input::IPointer> pointer;
        boolean captured;
        if (SUCCEEDED(args->get_Pointer(&pointer)))
            m_inputElement->CapturePointer(pointer.Get(), &captured);
    } else if (type == WKPointerEventMoveW10M) {
        hr = m_input.move(position.X, position.Y, now(), m_scroller);
        if (FAILED(hr))
            return hr;
    } else if (type == WKPointerEventReleaseW10M) {
        bool wasDown = m_input.pointerDown();
        bool wasDragging = m_input.dragging();
        double startY = m_input.startY();
        report("control-pointer-released", S_OK);
        report("ptr-release-x", static_cast<unsigned>(position.X));
        report("ptr-release-y", static_cast<unsigned>(position.Y));
        report("ptr-start-y", static_cast<unsigned>(startY));
        report("ptr-down", wasDown ? 1 : 0);
        report("ptr-dragging", wasDragging ? 1 : 0);
        PointerReleaseW10M outcome { };
        hr = m_input.release(position.X, position.Y, now(), m_scroller, m_tapPort, outcome);
        if (FAILED(hr))
            return hr;
        if (outcome.tapped)
            m_redrawPending = true;
        m_inputElement->ReleasePointerCaptures();
        WKPageStateW10M state;
        if (m_api.state(m_page, &state) == WKPageResultOkW10M)
            report("control-scroll-y", state.scrollY);
    }

    return hr;
}

// Seconds since boot from the performance counter for gesture physics.
double WebKitWebViewW10M::now() const
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / m_counterFrequency;
}

// Clears gesture and inertia state on pointer release or navigation.
void WebKitWebViewW10M::resetMotion()
{
    m_scroller.reset(now());
}

// Advances inertia, ticks the engine and presents when dirty; one Rendering callback.
HRESULT WebKitWebViewW10M::advanceFrame()
{
    // Presentation-only offset epsilon; scroll physics owns its own thresholds.
    constexpr double kOffsetChangeEpsilon = 0.001;
    double time = now();
    double previousOffset = m_scroller.overscrollY();
    bool scrolled = false;
    HRESULT motion = m_scroller.advance(time, m_input.pointerDown(), m_scrollPort, scrolled);
    if (FAILED(motion))
        return motion;
    if (scrolled)
        m_redrawPending = true;
    bool offsetChanged = std::abs(previousOffset - m_scroller.overscrollY()) > kOffsetChangeEpsilon;
    if (offsetChanged && m_presenterReady && !m_compositorActive) {
        HRESULT hr = m_presenter.setCompositionOffset(0, static_cast<float>(m_scroller.overscrollY()));
        if (FAILED(hr))
            return hr;
    }
    double scrollFinished = now();
    HRESULT hr = bridgeResult(m_api.tick(m_page));
    if (FAILED(hr) && m_compositorActive) {
        report("control-compositor-tick-failed", hr);
        hr = useFallbackPresenter();
        if (SUCCEEDED(hr))
            hr = bridgeResult(m_api.tick(m_page));
    }
    unsigned dirty = 0;
    if (SUCCEEDED(hr))
        hr = bridgeResult(m_api.needsPaint(m_page, &dirty));
    double engineFinished = now();
    bool submitted = false;
    if (SUCCEEDED(hr) && (dirty || m_redrawPending || (offsetChanged && m_compositorActive))) {
        submitted = m_surfaceReady;
        hr = repaint();
    } else if (SUCCEEDED(hr) && offsetChanged && m_presenterReady) {
        submitted = true;
        hr = m_presenter.presentCachedFrame();
        if ((hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)) {
            HRESULT recovered = m_presenter.recoverDevice();
            report("control-presenter-recover", recovered);
            if (SUCCEEDED(recovered))
                hr = m_presenter.presentCachedFrame();
            else
                hr = recovered;
            report("control-presenter-recovered", hr);
        }
    }
    recordFramePacing(time, scrollFinished, engineFinished, submitted && SUCCEEDED(hr));
    return hr;
}

// Splits one frame into scroll/engine/render cost and reports percentiles per window.
void WebKitWebViewW10M::recordFramePacing(double started, double scrollFinished, double engineFinished, bool submitted)
{
    FramePacingSummaryW10M metrics;
    if (!m_frameMetrics.recordPacing(started, scrollFinished, engineFinished, now(), submitted, metrics)) return;
    char summary[frameProfileMessageCapacity];
    if (!FrameMetricsW10M::formatPacing(metrics, summary, sizeof(summary))) return;
    // Callback cadence is not proof of scan-out; this includes engine and swap
    // work, but excludes time spent reporting this summary and OS input delivery.
    report(summary, S_OK);
}

// Accumulates paint/upload/copy/present cost and reports averages per window.
void WebKitWebViewW10M::recordFrame(double paintMS)
{
    FramePaintSummaryW10M metrics;
    if (!m_frameMetrics.recordPaint(now(), paintMS, m_presenter.frameTiming(), metrics)) return;
    report("perf-paint-average-us", static_cast<unsigned>(metrics.paintMS * kMicrosecondsPerMillisecond));
    report("perf-upload-average-us", static_cast<unsigned>(metrics.uploadMS * kMicrosecondsPerMillisecond));
    report("perf-copy-submit-average-us", static_cast<unsigned>(metrics.copyMS * kMicrosecondsPerMillisecond));
    report("perf-present-average-us", static_cast<unsigned>(metrics.presentMS * kMicrosecondsPerMillisecond));
    report("perf-frame-max-us", static_cast<unsigned>(metrics.maximumMS * kMicrosecondsPerMillisecond));
    report("perf-paint-sample-count", metrics.count);
}

// Revokes every XAML subscription, detaches the swap chain and destroys the page.
WebKitWebViewW10M::~WebKitWebViewW10M()
{
    m_frameScheduler.close();
    m_renderHooks.close();
    ComPtr<Xaml::IFrameworkElement> rootFramework;
    if (m_rootElement.Get() && SUCCEEDED(m_rootElement.As(&rootFramework))) {
        rootFramework->remove_Loaded(m_loadedToken);
        rootFramework->remove_Unloaded(m_unloadedToken);
    }
    if (m_panelElement) {
        ComPtr<Xaml::IFrameworkElement> element;
        if (SUCCEEDED(m_panelElement.As(&element)))
            element->remove_SizeChanged(m_sizeToken);
        ComPtr<Controls::ISwapChainPanel> scale;
        if (SUCCEEDED(m_panelElement.As(&scale)))
            scale->remove_CompositionScaleChanged(m_scaleToken);
        ComPtr<ISwapChainPanelNative> panel;
        if (m_presenter.mode() == PresentationModeW10M::CPU)
            m_presenter.detach();
        else if (SUCCEEDED(m_panelElement.As(&panel)))
            panel->SetSwapChain(nullptr);
    }
    if (m_inputElement) {
        m_inputElement->remove_PointerPressed(m_pressToken);
        m_inputElement->remove_PointerMoved(m_moveToken);
        m_inputElement->remove_PointerReleased(m_releaseToken);
        m_inputElement->remove_PointerWheelChanged(m_wheelToken);
        m_inputElement->remove_PointerCanceled(m_cancelToken);
    }
    if (m_page)
        m_api.destroy(m_page);
    // Keep WebCore loaded: other controls or pending engine tasks may use it.
}
}


