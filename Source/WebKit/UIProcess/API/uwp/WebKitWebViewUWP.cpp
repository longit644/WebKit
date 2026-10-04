#include "WebKitWebViewUWP.h"
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

// Maps the engine's WKPageResultUWP contract to HRESULT for XAML callers.
static HRESULT bridgeResult(unsigned result)
{
    if (result == WKPageResultWrongThreadUWP)
        return RPC_E_WRONG_THREAD;
    if (result == WKPageResultInvalidArgumentUWP)
        return E_INVALIDARG;
    return result ? E_FAIL : S_OK;
}

// Engine module loaded from the package; LoadPackagedLibrary flags are reserved zero.
constexpr wchar_t kEngineModuleName[] = L"WebCore.dll";
// Placeholder extent until the first SizeChanged reports the real control size.
constexpr unsigned kPlaceholderExtent = 1;
// Scale reported in thousandths for integer log lines.
constexpr double kScaleToMilli = 1000.0;
// Edge displacement below which the overscroll is considered settled.
constexpr double kEdgeRestThreshold = 0.1;
// Smallest logical control size accepted from XAML SizeChanged.
constexpr float kMinimumControlExtent = 1.0f;
// WHEEL_DELTA units (120 per notch) mapped to a logical scroll distance.
constexpr int kWheelDeltaPerNotch = 120;
constexpr double kWheelPixelsPerNotch = 80.0;
// Conversion from elapsed seconds to reported timing units.
constexpr double kMillisecondsPerSecond = 1000.0;
constexpr double kMicrosecondsPerMillisecond = 1000.0;
// Rank constants for p95 frame-profile calculation.
constexpr unsigned kPercentileBase = 100;
constexpr unsigned kP95Percentile = 95;
constexpr unsigned kP95RoundingBias = kPercentileBase - 1;
constexpr size_t kFrameProfileMessageCapacity = 512;
constexpr double kZeroElapsedSeconds = 0.0;
constexpr double kZeroScrollDelta = 0.0;
constexpr double kZeroFrameIntervalMS = 0.0;
constexpr double kNoMeasuredIntervalMS = 0.0;
constexpr double kSpringIntegrationHz = 120.0;
constexpr double kMilliHertzPerMillisecond = 1000000.0;
constexpr float kIdentityCompositionScale = 1.0f;
// XAML surface markup and matching FindName key.
constexpr wchar_t kSurfaceElementName[] = L"Surface";
constexpr wchar_t kInputSurfaceMarkup[] = L"<Grid xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' Background='Transparent'><SwapChainPanel x:Name='Surface'/></Grid>";

// Emits one engine-log line; drops it when no log sink is installed.
void WebKitWebViewUWP::report(const char* stage, HRESULT result)
{
    if (m_log)
        m_log(stage, result);
}

// Binds WebCore.dll, creates the page and builds the XAML tree. Fails when
// any entry point, control or subscription is missing; nothing half-built stays.
HRESULT WebKitWebViewUWP::initialize(const wchar_t* fonts, WKPageLogUWP log)
{
    if (m_page || !fonts)
        return E_INVALIDARG;
    m_owner = GetCurrentThreadId();
    LARGE_INTEGER frequency;
    if (!QueryPerformanceFrequency(&frequency))
        return HRESULT_FROM_WIN32(GetLastError());
    m_counterFrequency = static_cast<double>(frequency.QuadPart);
    m_log = log;
    m_engine = LoadPackagedLibrary(kEngineModuleName, 0);
    if (!m_engine)
        return HRESULT_FROM_WIN32(GetLastError());
#define BIND(field, symbol)                                                                   \
    m_api.field = reinterpret_cast<decltype(m_api.field)>(GetProcAddress(m_engine, #symbol)); \
    if (!m_api.field)                                                                         \
    return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND)
    BIND(create, WKCreatePageUWP);
    BIND(destroy, WKDestroyPageUWP);
    BIND(loadHTML, WKLoadHTMLUWP);
    BIND(navigate, WKNavigatePageUWP);
    BIND(goBack, WKGoBackPageUWP);
    BIND(goForward, WKGoForwardPageUWP);
    BIND(reload, WKReloadPageUWP);
    BIND(stop, WKStopPageUWP);
    BIND(setPolicyHandler, WKSetNavigationPolicyHandlerPageUWP);
    BIND(resize, WKResizePageUWP);
    BIND(scroll, WKScrollPageUWP);
    BIND(pointer, WKPointerPageUWP);
    BIND(tick, WKTickPageUWP);
    BIND(paint, WKPaintPageUWP);
    BIND(paintScaled, WKPaintPageScaledUWP);
    BIND(needsPaint, WKPageNeedsPaintUWP);
    BIND(state, WKGetPageStateUWP);
    BIND(enableCompositor, WKEnableCompositorUWP);
    BIND(disableCompositor, WKDisableCompositorUWP);
    BIND(renderCompositor, WKRenderCompositorUWP);
    BIND(installClipboardDispatcher, WKInstallClipboardDispatcherUWP);
#undef BIND
    unsigned result;
    m_page = m_api.create(fonts, kPlaceholderExtent, kPlaceholderExtent, m_log, &result);
    if (!m_page)
        return bridgeResult(result ? result : WKPageResultInvalidArgumentUWP);

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
    auto press = Callback<Input::IPointerEventHandler>([this](IInspectable*, Input::IPointerRoutedEventArgs* args) -> HRESULT { return pointer(WKPointerEventPressUWP, args); });
    auto move = Callback<Input::IPointerEventHandler>([this](IInspectable*, Input::IPointerRoutedEventArgs* args) -> HRESULT { return pointer(WKPointerEventMoveUWP, args); });
    auto release = Callback<Input::IPointerEventHandler>([this](IInspectable*, Input::IPointerRoutedEventArgs* args) -> HRESULT { return pointer(WKPointerEventReleaseUWP, args); });
    auto wheel = Callback<Input::IPointerEventHandler>([this](IInspectable*, Input::IPointerRoutedEventArgs* args) -> HRESULT {
        ComPtr<ABI::Windows::UI::Input::IPointerPoint> point;
        ComPtr<ABI::Windows::UI::Input::IPointerPointProperties> properties;
        INT32 delta = 0;
        HRESULT hr = args->GetCurrentPoint(m_inputElement.Get(), &point);
        if (FAILED(hr) || FAILED(hr = point->get_Properties(&properties)) || FAILED(hr = properties->get_MouseWheelDelta(&delta)))
            return hr;
        args->put_Handled(true);
        m_pendingScrollY -= delta * kWheelPixelsPerNotch / kWheelDeltaPerNotch;
        m_inertia = false;
        m_velocityY = 0;
        m_pendingElastic = false;
        m_redrawPending = true;
        return S_OK;
    });
    auto cancel = Callback<Input::IPointerEventHandler>([this](IInspectable*, Input::IPointerRoutedEventArgs*) -> HRESULT {
        m_pointerDown = false;
        m_inertia = false;
        m_velocityY = 0;
        m_pendingScrollX = m_pendingScrollY = 0;
        return S_OK;
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
    auto tick = Callback<Foundation::IEventHandler<IInspectable*>>([this](IInspectable*, IInspectable*) -> HRESULT {
        if (!m_page || m_inPaint)
            return S_OK;
        HRESULT hr = advanceFrame();
        if (FAILED(hr)) {
            report("control-frame-failed", hr);
            m_composition->remove_Rendering(m_renderToken);
        }
        return S_OK;
    });
    return m_composition->add_Rendering(tick.Get(), &m_renderToken);
}

// Rebuilds the backing store, clip and presenter for the new control size.
// Restarts the compositor once on scale changes; no-ops when nothing moved.
HRESULT WebKitWebViewUWP::resize(unsigned width, unsigned height)
{
    if (GetCurrentThreadId() != m_owner)
        return RPC_E_WRONG_THREAD;
    if (!width || !height || width > UWPMaxPageDimension || height > UWPMaxPageDimension)
        return E_INVALIDARG;
    if (m_clip)
        m_clip->put_Rect({ 0, 0, static_cast<float>(width), static_cast<float>(height) });
    ComPtr<Controls::ISwapChainPanel> panel;
    float scaleX = kIdentityCompositionScale, scaleY = kIdentityCompositionScale;
    HRESULT hr = m_panelElement.As(&panel);
    if (FAILED(hr) || FAILED(hr = panel->get_CompositionScaleX(&scaleX)) || FAILED(hr = panel->get_CompositionScaleY(&scaleY)))
        return hr;
    if (!std::isfinite(scaleX) || !std::isfinite(scaleY) || scaleX <= 0 || scaleY <= 0 || scaleX > UWPMaxDeviceScale || scaleY > UWPMaxDeviceScale)
        return E_INVALIDARG;
    unsigned pixelWidth = static_cast<unsigned>(std::ceil(width * scaleX));
    unsigned pixelHeight = static_cast<unsigned>(std::ceil(height * scaleY));
    if (pixelWidth > UWPMaxPaintDimension || pixelHeight > UWPMaxPaintDimension)
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
    return repaint();
}

// Drops to the software presenter when the compositor cannot run.
HRESULT WebKitWebViewUWP::useFallbackPresenter()
{
    HRESULT hr = bridgeResult(m_api.disableCompositor(m_page));
    if (FAILED(hr))
        return hr;
    m_compositorActive = false;
    m_pixels = std::make_unique<unsigned char[]>(static_cast<size_t>(m_pixelWidth) * m_pixelHeight * UWPBytesPerBGRA);
    if (m_presenterReady)
        hr = m_presenter.resize(m_pixelWidth, m_pixelHeight);
    else
        hr = m_presenter.initializeForSwapChainPanel(m_panelElement.Get(), m_pixelWidth, m_pixelHeight);
    if (SUCCEEDED(hr))
        hr = m_presenter.setCompositionScale(m_scaleX, m_scaleY);
    if (SUCCEEDED(hr))
        hr = m_presenter.setCompositionOffset(0, static_cast<float>(m_overscrollY));
    m_presenterReady = SUCCEEDED(hr);
    report("control-cairo-fallback", hr);
    return hr;
}

// Shows app-supplied markup and resets gesture, profiler and present state.
HRESULT WebKitWebViewUWP::loadHTML(const char* utf8)
{
    if (GetCurrentThreadId() != m_owner)
        return RPC_E_WRONG_THREAD;
    HRESULT hr = bridgeResult(m_api.loadHTML(m_page, utf8));
    resetMotion();
    if (m_presenterReady)
        m_presenter.setCompositionOffset(0, 0);
    m_redrawPending = true;
    m_frameWarmupCount = m_frameWorkCount = m_frameIntervalCount = 0;
    m_frameOverBudget = m_frameLongIntervals = 0;
    m_scrollWorkTotal = m_engineWorkTotal = m_renderWorkTotal = 0;
    m_previousFrameSubmitted = false;
    return hr;
}

// Starts an async remote navigation and resets gesture, profiler and present state.
HRESULT WebKitWebViewUWP::navigateRemote(const char* utf8Url, const wchar_t* caPath)
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
    m_frameWarmupCount = m_frameIntervalCount = 0;
    m_frameOverBudget = m_frameLongIntervals = 0;
    m_scrollWorkTotal = m_engineWorkTotal = m_renderWorkTotal = 0;
    m_previousFrameSubmitted = false;
    report("control-navigate-remote", hr);
    return hr;
}

// Steps back in session history and re-arms the presenter.
HRESULT WebKitWebViewUWP::goBack()
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
HRESULT WebKitWebViewUWP::goForward()
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
HRESULT WebKitWebViewUWP::reload()
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
HRESULT WebKitWebViewUWP::stop()
{
    if (GetCurrentThreadId() != m_owner)
        return RPC_E_WRONG_THREAD;
    HRESULT hr = bridgeResult(m_api.stop(m_page));
    report("control-stop", hr);
    return hr;
}

// Installs the host veto consulted synchronously at navigation policy time.
HRESULT WebKitWebViewUWP::setNavigationPolicyHandler(WKNavigationPolicyCallback callback, void* context)
{
    if (GetCurrentThreadId() != m_owner)
        return RPC_E_WRONG_THREAD;
    return bridgeResult(m_api.setPolicyHandler(m_page, callback, context));
}

// Scrolls by CSS pixels through the engine and reports the new offset.
HRESULT WebKitWebViewUWP::scrollBy(int dx, int dy)
{
    if (GetCurrentThreadId() != m_owner)
        return RPC_E_WRONG_THREAD;
    resetMotion();
    if (m_presenterReady)
        m_presenter.setCompositionOffset(0, 0);
    HRESULT hr = bridgeResult(m_api.scroll(m_page, dx, dy));
    if (SUCCEEDED(hr)) {
        WKPageStateUWP state;
        if (m_api.state(m_page, &state) == WKPageResultOkUWP)
            report("control-scroll-y", state.scrollY);
    }
    m_redrawPending = true;
    return hr;
}

// Snapshots scroll offsets, content size and history availability.
HRESULT WebKitWebViewUWP::pageState(WKPageStateUWP& state)
{
    if (GetCurrentThreadId() != m_owner)
        return RPC_E_WRONG_THREAD;
    return bridgeResult(m_api.state(m_page, &state));
}

// Presents one frame via compositor or software paint; S_FALSE when nothing is ready.
HRESULT WebKitWebViewUWP::repaint()
{
    if (GetCurrentThreadId() != m_owner)
        return RPC_E_WRONG_THREAD;
    if (!m_surfaceReady || m_inPaint)
        return S_FALSE;
    m_inPaint = true;
    if (m_compositorActive) {
        HRESULT hr = bridgeResult(m_api.renderCompositor(m_page, m_overscrollY));
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
    HRESULT hr = bridgeResult(m_api.paintScaled(m_page, m_pixels.get(), static_cast<size_t>(m_pixelWidth) * m_pixelHeight * UWPBytesPerBGRA, m_pixelWidth * UWPBytesPerBGRA, m_scaleX, m_scaleY));
    double paintMS = (now() - start) * kMillisecondsPerSecond;
    if (SUCCEEDED(hr))
        hr = m_presenter.present(m_pixels.get(), m_pixelWidth * UWPBytesPerBGRA);
    if (SUCCEEDED(hr))
        m_redrawPending = false;
    if (SUCCEEDED(hr))
        recordFrame(paintMS);
    m_inPaint = false;
    return hr;
}

// Writes the last presented pixels to a PNG file for on-device diagnosis.
HRESULT WebKitWebViewUWP::savePNG(const wchar_t* filename)
{
    if (GetCurrentThreadId() != m_owner)
        return RPC_E_WRONG_THREAD;
    if (!filename || !m_surfaceReady)
        return E_INVALIDARG;
    if (!m_pixels)
        m_pixels = std::make_unique<unsigned char[]>(static_cast<size_t>(m_pixelWidth) * m_pixelHeight * UWPBytesPerBGRA);
    HRESULT snapshot = bridgeResult(m_api.paintScaled(m_page, m_pixels.get(), static_cast<size_t>(m_pixelWidth) * m_pixelHeight * UWPBytesPerBGRA, m_pixelWidth * UWPBytesPerBGRA, m_scaleX, m_scaleY));
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
        || FAILED(hr = factory->CreateBitmapFromMemory(m_pixelWidth, m_pixelHeight, GUID_WICPixelFormat32bppPBGRA, m_pixelWidth * UWPBytesPerBGRA, m_pixelWidth * m_pixelHeight * UWPBytesPerBGRA, m_pixels.get(), &bitmap))
        || FAILED(hr = factory->CreateFormatConverter(&converter))
        || FAILED(hr = converter->Initialize(bitmap.Get(), format, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))
        || FAILED(hr = frame->WriteSource(converter.Get(), nullptr))
        || FAILED(hr = frame->Commit()))
        return hr;
    return encoder->Commit();
}

// Routes a XAML pointer event to press/move/release handling and the engine tap path.
HRESULT WebKitWebViewUWP::pointer(unsigned type, Input::IPointerRoutedEventArgs* args)
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
    double logicalX = position.X;
    double logicalY = position.Y;
    // Drag threshold in logical pixels before a press becomes a scroll.
    constexpr double kTapSlopLogicalPx = 10.0;
    double slopX = kTapSlopLogicalPx;
    double slopY = kTapSlopLogicalPx;
    if (type == WKPointerEventPressUWP) {
        report("control-pointer-pressed", S_OK);
        report("ptr-press-x", static_cast<unsigned>(position.X));
        report("ptr-press-y", static_cast<unsigned>(position.Y));
        report("ptr-scale-milli", static_cast<unsigned>(m_scaleX * kScaleToMilli));
        report("ptr-slop", static_cast<unsigned>(slopY));
        m_pointerDown = true;
        m_dragging = false;
        m_inertia = false;
        m_velocityY = 0;
        m_springVelocity = 0;
        m_lastPointerTime = m_lastMotionTime = now();
        m_startX = position.X;
        m_startY = m_lastY = position.Y;
        ComPtr<Input::IPointer> pointer;
        boolean captured;
        if (SUCCEEDED(args->get_Pointer(&pointer)))
            m_inputElement->CapturePointer(pointer.Get(), &captured);
    } else if (type == WKPointerEventMoveUWP && m_pointerDown) {
        // Velocity sampling window, motion threshold and blend time constant for fling physics.
        constexpr double kVelocitySampleWindowS = 0.2;
        constexpr double kMinMotionDeltaPx = 0.2;
        constexpr double kMaxFlingVelocity = 5000.0;
        constexpr double kVelocityBlendTauS = 0.035;
        m_dragging = m_dragging || std::abs(position.Y - m_startY) > slopY || std::abs(position.X - m_startX) > slopX;
        double time = now(), elapsed = time - m_lastPointerTime;
        double delta = m_lastY - position.Y;
        if (m_dragging) {
            m_pendingScrollY += delta;
            m_pendingElastic = true;
            if (elapsed > kZeroElapsedSeconds && elapsed < kVelocitySampleWindowS) {
                double sample = std::clamp(delta / elapsed, -kMaxFlingVelocity, kMaxFlingVelocity);
                double blend = 1 - std::exp(-elapsed / kVelocityBlendTauS);
                m_velocityY += blend * (sample - m_velocityY);
            }
            if (std::abs(delta) > kMinMotionDeltaPx)
                m_lastMotionTime = time;
        }
        m_lastPointerTime = time;
        m_lastY = position.Y;
    } else if (type == WKPointerEventReleaseUWP) {
        report("control-pointer-released", S_OK);
        report("ptr-release-x", static_cast<unsigned>(position.X));
        report("ptr-release-y", static_cast<unsigned>(position.Y));
        report("ptr-start-y", static_cast<unsigned>(m_startY));
        report("ptr-down", m_pointerDown ? 1 : 0);
        report("ptr-dragging", m_dragging ? 1 : 0);
        if (m_pointerDown && !m_dragging
            && std::abs(position.Y - m_startY) <= slopY && std::abs(position.X - m_startX) <= slopX) {
            hr = bridgeResult(m_api.pointer(m_page, WKPointerEventPressUWP, logicalX, logicalY - m_overscrollY, WKPointerModifierNoneUWP));
            if (SUCCEEDED(hr))
                hr = bridgeResult(m_api.pointer(m_page, WKPointerEventReleaseUWP, logicalX, logicalY - m_overscrollY, WKPointerModifierNoneUWP));
            m_redrawPending = true;
        }

        // Fling needs a live drag, a settled edge, enough velocity and a recent motion sample.
        constexpr double kMinFlingVelocity = 60.0;
        constexpr double kFlingRecencyWindowS = 0.1;
        constexpr double kFlingBoostFactor = 1.12;
        constexpr double kMaxBoostedVelocity = 5500.0;
        m_pointerDown = false;
        m_inertia = m_dragging && std::abs(m_overscrollY) < kEdgeRestThreshold && std::abs(m_velocityY) >= kMinFlingVelocity && now() - m_lastMotionTime < kFlingRecencyWindowS;
        if (m_inertia) {
            m_velocityY = std::clamp(m_velocityY * kFlingBoostFactor, -kMaxBoostedVelocity, kMaxBoostedVelocity);
            report("control-inertia-start", S_OK);
        } else
            m_velocityY = 0;
        m_lastFrameTime = now();
        m_inputElement->ReleasePointerCaptures();
        WKPageStateUWP state;
        if (m_api.state(m_page, &state) == WKPageResultOkUWP)
            report("control-scroll-y", state.scrollY);
    }

    return hr;
}

// Seconds since boot from the performance counter for gesture physics.
double WebKitWebViewUWP::now() const
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / m_counterFrequency;
}

// Clears gesture and inertia state on pointer release or navigation.
void WebKitWebViewUWP::resetMotion()
{
    m_pendingScrollX = m_pendingScrollY = 0;
    m_velocityY = m_overscrollY = m_springVelocity = 0;
    m_inertia = m_pendingElastic = false;
    m_lastFrameTime = now();
}

// Moves the document through the engine; leftover delta becomes edge overscroll.
HRESULT WebKitWebViewUWP::applyScroll(int dx, int dy, bool elastic, bool kinetic)
{
    // Reverse motion first consumes a held edge displacement before moving
    // the document. Page coordinates stay clamped; only presentation bounces.
    if (m_overscrollY * dy > kZeroScrollDelta) {
        double consumed = std::min(std::abs(static_cast<double>(dy)), std::abs(m_overscrollY));
        consumed = std::copysign(consumed, m_overscrollY);
        m_overscrollY -= consumed;
        dy -= static_cast<int>(consumed);
    }
    WKPageStateUWP before, after;
    HRESULT hr = bridgeResult(m_api.state(m_page, &before));
    if (FAILED(hr))
        return hr;
    int maximumY = std::max(0, before.contentHeight - static_cast<int>(before.height));
    int targetY = static_cast<int>(std::clamp<long long>(static_cast<long long>(before.scrollY) + dy, 0, maximumY));
    after = before;
    if (dx || targetY != before.scrollY) {
        hr = bridgeResult(m_api.scroll(m_page, dx, targetY - before.scrollY));
        if (FAILED(hr) || FAILED(hr = bridgeResult(m_api.state(m_page, &after))))
            return hr;
        m_redrawPending = true;
    }
    int unused = dy - (after.scrollY - before.scrollY);
    if (unused && elastic) {
        // Kinetic flings meet a stiffer edge than slow drags; resistance also
        // grows with the held displacement so the edge cannot run away.
        constexpr double kResistanceKinetic = 0.2;
        constexpr double kResistanceStaticBase = 0.4;
        constexpr double kResistanceFalloffPx = 36.0;
        constexpr double kMaxOverscrollPx = 56.0;
        constexpr double kKineticSpringTransfer = 0.2;
        double resistance = kinetic ? kResistanceKinetic : kResistanceStaticBase / (1 + std::abs(m_overscrollY) / kResistanceFalloffPx);
        bool firstEdge = std::abs(m_overscrollY) < kEdgeRestThreshold;
        m_overscrollY = std::clamp(m_overscrollY - unused * resistance, -kMaxOverscrollPx, kMaxOverscrollPx);
        if (firstEdge)
            report("control-edge-hit", S_OK);
        if (kinetic) {
            m_springVelocity = -m_velocityY * kKineticSpringTransfer;
            m_inertia = false;
            m_velocityY = 0;
        }
    }
    return S_OK;
}

// Advances inertia, ticks the engine and presents when dirty; one Rendering callback.
HRESULT WebKitWebViewUWP::advanceFrame()
{
    // Long stalls (suspend/resume) invalidate fling state; steps stay small for stable springs.
    constexpr double kStallResetIntervalS = 0.25;
    constexpr double kMaxFrameStepS = 0.05;
    constexpr double kFlingFriction = 4.2;
    constexpr double kFlingStopVelocity = 20.0;
    constexpr double kSpringStiffness = 256.0;
    constexpr double kSpringDamping = 32.0;
    constexpr double kSpringSubstepS = 1.0 / kSpringIntegrationHz;
    constexpr double kEdgeSnapDistance = 0.15;
    constexpr double kEdgeSnapVelocity = 2.0;
    constexpr double kOffsetChangeEpsilon = 0.001;
    double time = now(), elapsed = time - m_lastFrameTime;
    m_lastFrameTime = time;
    double previousOffset = m_overscrollY;
    if (elapsed > kStallResetIntervalS) {
        // Don't resume an old fling after suspension or a long UI stall.
        m_inertia = false;
        m_velocityY = m_overscrollY = m_springVelocity = 0;
    }
    double dt = std::clamp(elapsed, 0.0, kMaxFrameStepS);
    if (m_inertia && !m_pointerDown) {
        double next = m_velocityY * std::exp(-kFlingFriction * dt);
        m_pendingScrollY += (m_velocityY - next) / kFlingFriction;
        m_velocityY = next;
        if (std::abs(m_velocityY) < kFlingStopVelocity) {
            m_inertia = false;
            m_velocityY = 0;
            report("control-inertia-stop", S_OK);
        }
    }
    int dx = static_cast<int>(m_pendingScrollX), dy = static_cast<int>(m_pendingScrollY);
    m_pendingScrollX -= dx;
    m_pendingScrollY -= dy;
    bool kinetic = m_inertia;
    if (dx || dy) {
        HRESULT hr = applyScroll(dx, dy, m_pendingElastic || kinetic, kinetic);
        if (FAILED(hr))
            return hr;
    }
    m_pendingElastic = false;
    if (!m_pointerDown && !m_inertia && std::abs(m_overscrollY) > kZeroScrollDelta) {
        // Small substeps keep the spring stable across variable frame times.
        for (double remaining = dt; remaining > kZeroElapsedSeconds;) {
            double step = std::min(remaining, kSpringSubstepS);
            m_springVelocity += (-kSpringStiffness * m_overscrollY - kSpringDamping * m_springVelocity) * step;
            m_overscrollY += m_springVelocity * step;
            remaining -= step;
        }
        if (std::abs(m_overscrollY) < kEdgeSnapDistance && std::abs(m_springVelocity) < kEdgeSnapVelocity) {
            m_overscrollY = m_springVelocity = 0;
            report("control-edge-returned", S_OK);
        }
    }
    bool offsetChanged = std::abs(previousOffset - m_overscrollY) > kOffsetChangeEpsilon;
    if (offsetChanged && m_presenterReady && !m_compositorActive) {
        HRESULT hr = m_presenter.setCompositionOffset(0, static_cast<float>(m_overscrollY));
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
    }
    recordFramePacing(time, scrollFinished, engineFinished, submitted && SUCCEEDED(hr));
    return hr;
}

// Splits one frame into scroll/engine/render cost and reports percentiles per window.
void WebKitWebViewUWP::recordFramePacing(double started, double scrollFinished, double engineFinished, bool submitted)
{
    // Warmup frames, the 60 Hz budget and the valid/long interval bounds.
    constexpr unsigned kFrameWarmupFrames = 8;
    constexpr double kDisplayHertz = 60.0;
    constexpr double kMaxValidIntervalMS = 250.0;
    constexpr double kLongIntervalMS = 25.0;
    double finished = now();
    bool consecutive = m_previousFrameSubmitted && submitted;
    double intervalMS = (started - m_previousFrameStart) * kMillisecondsPerSecond;
    m_previousFrameSubmitted = submitted;
    m_previousFrameStart = started;
    if (!submitted)
        return;
    // Cold shader/tile costs are reported separately by the compositor. Do not
    // mix the initial eight submitted callbacks into warmed percentile samples.
    if (m_frameWarmupCount < kFrameWarmupFrames) {
        ++m_frameWarmupCount;
        return;
    }
    double workMS = (finished - started) * kMillisecondsPerSecond;
    m_frameWorkSamples[m_frameWorkCount++] = workMS;
    m_scrollWorkTotal += (scrollFinished - started) * kMillisecondsPerSecond;
    m_engineWorkTotal += (engineFinished - scrollFinished) * kMillisecondsPerSecond;
    m_renderWorkTotal += (finished - engineFinished) * kMillisecondsPerSecond;
    if (workMS > kMillisecondsPerSecond / kDisplayHertz)
        ++m_frameOverBudget;
    if (consecutive && intervalMS > kZeroFrameIntervalMS && intervalMS < kMaxValidIntervalMS) {
        m_frameIntervalSamples[m_frameIntervalCount++] = intervalMS;
        if (intervalMS > kLongIntervalMS)
            ++m_frameLongIntervals;
    }
    if (m_frameWorkCount < m_frameWorkSamples.size())
        return;
    auto summarize = [](auto samples, unsigned count, double& average, double& p95, double& maximum) {
        average = p95 = maximum = 0;
        if (!count)
            return;
        for (unsigned i = 0; i < count; ++i)
            average += samples[i];
        average /= count;
        std::sort(samples.begin(), samples.begin() + count);
        p95 = samples[(count * kP95Percentile + kP95RoundingBias) / kPercentileBase - 1];
        maximum = samples[count - 1];
    };
    double workAverage, workP95, workMaximum, intervalAverage, intervalP95, intervalMaximum;
    summarize(m_frameWorkSamples, m_frameWorkCount, workAverage, workP95, workMaximum);
    summarize(m_frameIntervalSamples, m_frameIntervalCount, intervalAverage, intervalP95, intervalMaximum);
    char summary[kFrameProfileMessageCapacity];
    sprintf_s(summary, "frame-profile n=%u work-us=%.0f/%.0f/%.0f interval-n=%u interval-us=%.0f/%.0f/%.0f callback-mHz=%.0f over-budget=%u long-intervals=%u scroll-us=%.0f engine-us=%.0f render-us=%.0f",
        m_frameWorkCount, workAverage * kMicrosecondsPerMillisecond, workP95 * kMicrosecondsPerMillisecond, workMaximum * kMicrosecondsPerMillisecond,
        m_frameIntervalCount, intervalAverage * kMicrosecondsPerMillisecond, intervalP95 * kMicrosecondsPerMillisecond, intervalMaximum * kMicrosecondsPerMillisecond,
        intervalAverage > kNoMeasuredIntervalMS ? kMilliHertzPerMillisecond / intervalAverage : 0, m_frameOverBudget, m_frameLongIntervals,
        m_scrollWorkTotal * kMicrosecondsPerMillisecond / m_frameWorkCount, m_engineWorkTotal * kMicrosecondsPerMillisecond / m_frameWorkCount, m_renderWorkTotal * kMicrosecondsPerMillisecond / m_frameWorkCount);
    // Callback cadence is not proof of scan-out; this includes engine and swap
    // work, but excludes time spent reporting this summary and OS input delivery.
    report(summary, S_OK);
    m_frameWorkCount = m_frameIntervalCount = m_frameOverBudget = m_frameLongIntervals = 0;
    m_scrollWorkTotal = m_engineWorkTotal = m_renderWorkTotal = 0;
}

// Accumulates paint/upload/copy/present cost and reports averages per window.
void WebKitWebViewUWP::recordFrame(double paintMS)
{
    // Report once eight frames span at least a second of wall time.
    constexpr unsigned kProfileMinFrames = 8;
    constexpr double kProfileMinSeconds = 1.0;
    double time = now();
    if (!m_profileFrames)
        m_profileStart = time;
    auto timing = m_presenter.frameTiming();
    ++m_profileFrames;
    m_paintTotal += paintMS;
    m_uploadTotal += timing.uploadMS;
    m_copyTotal += timing.copyMS;
    m_presentTotal += timing.presentMS;
    m_frameMax = std::max(m_frameMax, paintMS + timing.uploadMS + timing.copyMS + timing.presentMS);
    if (m_profileFrames >= kProfileMinFrames && time - m_profileStart >= kProfileMinSeconds) {
        report("perf-paint-average-us", static_cast<unsigned>(m_paintTotal * kMicrosecondsPerMillisecond / m_profileFrames));
        report("perf-upload-average-us", static_cast<unsigned>(m_uploadTotal * kMicrosecondsPerMillisecond / m_profileFrames));
        report("perf-copy-submit-average-us", static_cast<unsigned>(m_copyTotal * kMicrosecondsPerMillisecond / m_profileFrames));
        report("perf-present-average-us", static_cast<unsigned>(m_presentTotal * kMicrosecondsPerMillisecond / m_profileFrames));
        report("perf-frame-max-us", static_cast<unsigned>(m_frameMax * kMicrosecondsPerMillisecond));
        report("perf-paint-sample-count", m_profileFrames);
        m_profileFrames = 0;
        m_paintTotal = m_uploadTotal = m_copyTotal = m_presentTotal = m_frameMax = 0;
    }
}

// Revokes every XAML subscription, detaches the swap chain and destroys the page.
WebKitWebViewUWP::~WebKitWebViewUWP()
{
    if (m_composition)
        m_composition->remove_Rendering(m_renderToken);
    if (m_panelElement) {
        ComPtr<Xaml::IFrameworkElement> element;
        if (SUCCEEDED(m_panelElement.As(&element)))
            element->remove_SizeChanged(m_sizeToken);
        ComPtr<Controls::ISwapChainPanel> scale;
        if (SUCCEEDED(m_panelElement.As(&scale)))
            scale->remove_CompositionScaleChanged(m_scaleToken);
        ComPtr<ISwapChainPanelNative> panel;
        if (SUCCEEDED(m_panelElement.As(&panel)))
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
