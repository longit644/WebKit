#include "WebKitWebViewUWP.h"
#include <windows.ui.input.h>
#include <windows.ui.xaml.media.dxinterop.h>
#include <windows.ui.xaml.markup.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cmath>
#include <wincodec.h>
#include <algorithm>
#include <cstdio>

namespace WebKit {
using namespace Microsoft::WRL;
using Microsoft::WRL::Wrappers::HStringReference;
namespace Xaml = ABI::Windows::UI::Xaml;
namespace Controls = ABI::Windows::UI::Xaml::Controls;
namespace Input = ABI::Windows::UI::Xaml::Input;
namespace Foundation = ABI::Windows::Foundation;

static HRESULT bridgeResult(unsigned result)
{
    if (result == 8) return RPC_E_WRONG_THREAD;
    if (result == 1) return E_INVALIDARG;
    return result ? E_FAIL : S_OK;
}

void WebKitWebViewUWP::report(const char* stage, HRESULT result) { if (m_log) m_log(stage, result); }

HRESULT WebKitWebViewUWP::initialize(const wchar_t* fonts, WKPageLogUWP log)
{
    if (m_page || !fonts) return E_INVALIDARG;
    m_owner = GetCurrentThreadId();
    LARGE_INTEGER frequency;
    if (!QueryPerformanceFrequency(&frequency)) return HRESULT_FROM_WIN32(GetLastError());
    m_counterFrequency = static_cast<double>(frequency.QuadPart);
    m_log = log;
    m_engine = LoadPackagedLibrary(L"WebCore.dll", 0);
    if (!m_engine) return HRESULT_FROM_WIN32(GetLastError());
#define BIND(field, symbol) m_api.field = reinterpret_cast<decltype(m_api.field)>(GetProcAddress(m_engine, #symbol)); if (!m_api.field) return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND)
    BIND(create, WKCreatePageUWP);
    BIND(destroy, WKDestroyPageUWP);
    BIND(loadHTML, WKLoadHTMLUWP);
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
    m_page = m_api.create(fonts, 1, 1, m_log, &result);
    if (!m_page) return bridgeResult(result ? result : 1);

    ComPtr<Controls::IUserControlFactory> controlFactory;
    ComPtr<Controls::IUserControl> control;
    ComPtr<IInspectable> inner;
    HRESULT hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_UI_Xaml_Controls_UserControl).Get(), IID_PPV_ARGS(&controlFactory));
    if (FAILED(hr) || FAILED(hr = controlFactory->CreateInstance(nullptr, &inner, &control))
        || FAILED(hr = control.As(&m_rootElement))) return hr;
    ComPtr<Xaml::Markup::IXamlReaderStatics> reader;
    ComPtr<Controls::ISwapChainPanel> panel;
    ComPtr<IInspectable> panelObject;
    ComPtr<IInspectable> inputObject;
    ComPtr<Xaml::IFrameworkElement> inputFramework;
    hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_UI_Xaml_Markup_XamlReader).Get(), IID_PPV_ARGS(&reader));
    if (FAILED(hr) || FAILED(hr = reader->Load(HStringReference(L"<Grid xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' xmlns:x='http://schemas.microsoft.com/winfx/2006/xaml' Background='Transparent'><SwapChainPanel x:Name='Surface'/></Grid>").Get(), &inputObject))
        || FAILED(hr = inputObject.As(&inputFramework))
        || FAILED(hr = inputFramework->FindName(HStringReference(L"Surface").Get(), &panelObject))
        || FAILED(hr = panelObject.As(&panel))
        || FAILED(hr = panel.As(&m_panelElement)) || FAILED(hr = inputObject.As(&m_inputElement))) return hr;
    if (FAILED(hr = control->put_Content(m_inputElement.Get()))) return hr;
    ComPtr<Xaml::IDependencyObject> dependency;
    ComPtr<ABI::Windows::UI::Core::ICoreDispatcher> clipboardDispatcher;
    ComPtr<IInspectable> clipboardDispatcherObject;
    if (FAILED(hr = m_rootElement.As(&dependency))
        || FAILED(hr = dependency->get_Dispatcher(&clipboardDispatcher))
        || FAILED(hr = clipboardDispatcher.As(&clipboardDispatcherObject))) return hr;
    hr = m_api.installClipboardDispatcher(m_page, clipboardDispatcherObject.Get());
    report("control-clipboard-dispatcher", hr);
    if (FAILED(hr)) return hr;
    ComPtr<IInspectable> clip;
    if (FAILED(hr = RoActivateInstance(HStringReference(RuntimeClass_Windows_UI_Xaml_Media_RectangleGeometry).Get(), &clip))
        || FAILED(hr = clip.As(&m_clip))
        || FAILED(hr = m_inputElement->put_Clip(m_clip.Get()))) return hr;
    report("control-panel-attached", S_OK);
    ComPtr<Xaml::IFrameworkElement> element;
    if (FAILED(hr = panel.As(&element))) return hr;
    auto sizeChanged = Callback<Xaml::ISizeChangedEventHandler>([this](IInspectable*, Xaml::ISizeChangedEventArgs* args) -> HRESULT {
        Foundation::Size size;
        HRESULT hr = args->get_NewSize(&size);
        if (FAILED(hr) || size.Width < 1 || size.Height < 1) return hr;
        hr = resize(static_cast<unsigned>(size.Width), static_cast<unsigned>(size.Height));
        report("control-resize", hr);
        return hr;
    });
    if (FAILED(hr = element->add_SizeChanged(sizeChanged.Get(), &m_sizeToken))) return hr;
    auto scaleChanged = Callback<Foundation::ITypedEventHandler<Controls::SwapChainPanel*, IInspectable*>>([this](Controls::ISwapChainPanel*, IInspectable*) -> HRESULT {
        HRESULT hr = resize(m_width, m_height);
        report("control-dpi-change", hr);
        return hr;
    });
    if (FAILED(hr = panel->add_CompositionScaleChanged(scaleChanged.Get(), &m_scaleToken))) return hr;
    auto press = Callback<Input::IPointerEventHandler>([this](IInspectable*, Input::IPointerRoutedEventArgs* args) -> HRESULT { return pointer(0, args); });
    auto move = Callback<Input::IPointerEventHandler>([this](IInspectable*, Input::IPointerRoutedEventArgs* args) -> HRESULT { return pointer(1, args); });
    auto release = Callback<Input::IPointerEventHandler>([this](IInspectable*, Input::IPointerRoutedEventArgs* args) -> HRESULT { return pointer(2, args); });
    auto wheel = Callback<Input::IPointerEventHandler>([this](IInspectable*, Input::IPointerRoutedEventArgs* args) -> HRESULT {
        ComPtr<ABI::Windows::UI::Input::IPointerPoint> point;
        ComPtr<ABI::Windows::UI::Input::IPointerPointProperties> properties;
        INT32 delta = 0;
        HRESULT hr = args->GetCurrentPoint(m_inputElement.Get(), &point);
        if (FAILED(hr) || FAILED(hr = point->get_Properties(&properties)) || FAILED(hr = properties->get_MouseWheelDelta(&delta))) return hr;
        args->put_Handled(true);
        m_pendingScrollY -= delta * 80.0 / 120;
        m_inertia = false; m_velocityY = 0; m_pendingElastic = false;
        m_redrawPending = true;
        return S_OK;
    });
    auto cancel = Callback<Input::IPointerEventHandler>([this](IInspectable*, Input::IPointerRoutedEventArgs*) -> HRESULT {
        m_pointerDown = false; m_inertia = false; m_velocityY = 0;
        m_pendingScrollX = m_pendingScrollY = 0;
        return S_OK;
    });
    if (FAILED(hr = m_inputElement->add_PointerPressed(press.Get(), &m_pressToken))
        || FAILED(hr = m_inputElement->add_PointerMoved(move.Get(), &m_moveToken))
        || FAILED(hr = m_inputElement->add_PointerReleased(release.Get(), &m_releaseToken))
        || FAILED(hr = m_inputElement->add_PointerWheelChanged(wheel.Get(), &m_wheelToken))
        || FAILED(hr = m_inputElement->add_PointerCanceled(cancel.Get(), &m_cancelToken))) return hr;
    hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_UI_Xaml_Media_CompositionTarget).Get(), IID_PPV_ARGS(&m_composition));
    if (FAILED(hr)) return hr;
    auto tick = Callback<Foundation::IEventHandler<IInspectable*>>([this](IInspectable*, IInspectable*) -> HRESULT {
        if (!m_page || m_inPaint) return S_OK;
        HRESULT hr = advanceFrame();
        if (FAILED(hr)) { report("control-frame-failed", hr); m_composition->remove_Rendering(m_renderToken); }
        return S_OK;
    });
    return m_composition->add_Rendering(tick.Get(), &m_renderToken);
}

HRESULT WebKitWebViewUWP::resize(unsigned width, unsigned height)
{
    if (GetCurrentThreadId() != m_owner) return RPC_E_WRONG_THREAD;
    if (!width || !height || width > 2048 || height > 2048) return E_INVALIDARG;
    if (m_clip) m_clip->put_Rect({ 0, 0, static_cast<float>(width), static_cast<float>(height) });
    ComPtr<Controls::ISwapChainPanel> panel;
    float scaleX = 1, scaleY = 1;
    HRESULT hr = m_panelElement.As(&panel);
    if (FAILED(hr) || FAILED(hr = panel->get_CompositionScaleX(&scaleX)) || FAILED(hr = panel->get_CompositionScaleY(&scaleY))) return hr;
    if (!std::isfinite(scaleX) || !std::isfinite(scaleY) || scaleX <= 0 || scaleY <= 0 || scaleX > 8 || scaleY > 8) return E_INVALIDARG;
    unsigned pixelWidth = static_cast<unsigned>(std::ceil(width * scaleX));
    unsigned pixelHeight = static_cast<unsigned>(std::ceil(height * scaleY));
    if (pixelWidth > 4096 || pixelHeight > 4096) return E_INVALIDARG;
    if (m_surfaceReady && width == m_width && height == m_height && scaleX == m_scaleX && scaleY == m_scaleY) return S_OK;
    resetMotion();
    if (m_compositorActive && (scaleX != m_scaleX || scaleY != m_scaleY)) {
        hr = bridgeResult(m_api.disableCompositor(m_page));
        if (FAILED(hr)) return hr;
        m_compositorActive = false;
    }
    hr = bridgeResult(m_api.resize(m_page, width, height));
    if (FAILED(hr)) return hr;
    m_width = width; m_height = height;
    m_pixels.reset();
    m_pixelWidth = pixelWidth; m_pixelHeight = pixelHeight;
    m_scaleX = scaleX; m_scaleY = scaleY;
    if (!m_compositorAttempted) {
        m_compositorAttempted = true;
        ComPtr<IInspectable> nativePanel;
        hr = m_panelElement.As(&nativePanel);
        if (SUCCEEDED(hr)) hr = bridgeResult(m_api.enableCompositor(m_page, nativePanel.Get(), scaleX, scaleY));
        m_compositorActive = SUCCEEDED(hr);
        report("control-compositor-initialize", hr);
    }
    if (!m_compositorActive && FAILED(hr = useFallbackPresenter())) return hr;
    m_surfaceReady = true;
    report("control-viewport-width", width);
    report("control-viewport-height", height);
    report("control-pixel-width", pixelWidth);
    report("control-pixel-height", pixelHeight);
    report("control-scale-x-milli", static_cast<unsigned>(scaleX * 1000));
    return repaint();
}

HRESULT WebKitWebViewUWP::useFallbackPresenter()
{
    HRESULT hr = bridgeResult(m_api.disableCompositor(m_page));
    if (FAILED(hr)) return hr;
    m_compositorActive = false;
    m_pixels = std::make_unique<unsigned char[]>(static_cast<size_t>(m_pixelWidth) * m_pixelHeight * 4);
    if (m_presenterReady) hr = m_presenter.resize(m_pixelWidth, m_pixelHeight);
    else hr = m_presenter.initializeForSwapChainPanel(m_panelElement.Get(), m_pixelWidth, m_pixelHeight);
    if (SUCCEEDED(hr)) hr = m_presenter.setCompositionScale(m_scaleX, m_scaleY);
    if (SUCCEEDED(hr)) hr = m_presenter.setCompositionOffset(0, static_cast<float>(m_overscrollY));
    m_presenterReady = SUCCEEDED(hr);
    report("control-cairo-fallback", hr);
    return hr;
}

HRESULT WebKitWebViewUWP::loadHTML(const char* utf8)
{
    if (GetCurrentThreadId() != m_owner) return RPC_E_WRONG_THREAD;
    HRESULT hr = bridgeResult(m_api.loadHTML(m_page, utf8));
    resetMotion();
    if (m_presenterReady) m_presenter.setCompositionOffset(0, 0);
    m_redrawPending = true;
    m_frameWarmupCount = m_frameWorkCount = m_frameIntervalCount = 0;
    m_frameOverBudget = m_frameLongIntervals = 0;
    m_scrollWorkTotal = m_engineWorkTotal = m_renderWorkTotal = 0;
    m_previousFrameSubmitted = false;
    return hr;
}

HRESULT WebKitWebViewUWP::scrollBy(int dx, int dy)
{
    if (GetCurrentThreadId() != m_owner) return RPC_E_WRONG_THREAD;
    resetMotion();
    if (m_presenterReady) m_presenter.setCompositionOffset(0, 0);
    HRESULT hr = bridgeResult(m_api.scroll(m_page, dx, dy));
    if (SUCCEEDED(hr)) {
        WKPageStateUWP state;
        if (!m_api.state(m_page, &state)) report("control-scroll-y", state.scrollY);
    }
    m_redrawPending = true;
    return hr;
}

HRESULT WebKitWebViewUWP::pageState(WKPageStateUWP& state)
{
    if (GetCurrentThreadId() != m_owner) return RPC_E_WRONG_THREAD;
    return bridgeResult(m_api.state(m_page, &state));
}

HRESULT WebKitWebViewUWP::repaint()
{
    if (GetCurrentThreadId() != m_owner) return RPC_E_WRONG_THREAD;
    if (!m_surfaceReady || m_inPaint) return S_FALSE;
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
        if (FAILED(hr)) { m_inPaint = false; return hr; }
    }
    double start = now();
    HRESULT hr = bridgeResult(m_api.paintScaled(m_page, m_pixels.get(), static_cast<size_t>(m_pixelWidth) * m_pixelHeight * 4, m_pixelWidth * 4, m_scaleX, m_scaleY));
    double paintMS = (now() - start) * 1000;
    if (SUCCEEDED(hr)) hr = m_presenter.present(m_pixels.get(), m_pixelWidth * 4);
    if (SUCCEEDED(hr)) m_redrawPending = false;
    if (SUCCEEDED(hr)) recordFrame(paintMS);
    m_inPaint = false;
    return hr;
}

HRESULT WebKitWebViewUWP::savePNG(const wchar_t* filename)
{
    if (GetCurrentThreadId() != m_owner) return RPC_E_WRONG_THREAD;
    if (!filename || !m_surfaceReady) return E_INVALIDARG;
    if (!m_pixels) m_pixels = std::make_unique<unsigned char[]>(static_cast<size_t>(m_pixelWidth) * m_pixelHeight * 4);
    HRESULT snapshot = bridgeResult(m_api.paintScaled(m_page, m_pixels.get(), static_cast<size_t>(m_pixelWidth) * m_pixelHeight * 4, m_pixelWidth * 4, m_scaleX, m_scaleY));
    if (FAILED(snapshot)) { report("capture-page-snapshot", snapshot); return snapshot; }
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
        || FAILED(hr = frame->SetSize(m_pixelWidth, m_pixelHeight))) return hr;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(hr = frame->SetPixelFormat(&format))
        || FAILED(hr = factory->CreateBitmapFromMemory(m_pixelWidth, m_pixelHeight, GUID_WICPixelFormat32bppPBGRA, m_pixelWidth * 4, m_pixelWidth * m_pixelHeight * 4, m_pixels.get(), &bitmap))
        || FAILED(hr = factory->CreateFormatConverter(&converter))
        || FAILED(hr = converter->Initialize(bitmap.Get(), format, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))
        || FAILED(hr = frame->WriteSource(converter.Get(), nullptr))
        || FAILED(hr = frame->Commit())) return hr;
    return encoder->Commit();
}

HRESULT WebKitWebViewUWP::pointer(unsigned type, Input::IPointerRoutedEventArgs* args)
{
    ComPtr<ABI::Windows::UI::Input::IPointerPoint> point;
    Foundation::Point position;
    HRESULT hr = args->GetCurrentPoint(m_inputElement.Get(), &point);
    if (FAILED(hr) || FAILED(hr = point->get_Position(&position))) return hr;
    args->put_Handled(true);
    if (!type) {
        report("control-pointer-pressed", S_OK);
        m_pointerDown = true; m_dragging = false;
        m_inertia = false; m_velocityY = 0; m_springVelocity = 0;
        m_lastPointerTime = m_lastMotionTime = now();
        m_startX = position.X; m_startY = m_lastY = position.Y;
        ComPtr<Input::IPointer> pointer;
        boolean captured;
        if (SUCCEEDED(args->get_Pointer(&pointer))) m_inputElement->CapturePointer(pointer.Get(), &captured);
    } else if (type == 1 && m_pointerDown) {
        m_dragging = m_dragging || std::abs(position.Y - m_startY) > 8 || std::abs(position.X - m_startX) > 8;
        double time = now(), elapsed = time - m_lastPointerTime;
        double delta = m_lastY - position.Y;
        if (m_dragging) {
            m_pendingScrollY += delta; m_pendingElastic = true;
            if (elapsed > 0 && elapsed < 0.2) {
                double sample = std::clamp(delta / elapsed, -5000.0, 5000.0);
                double blend = 1 - std::exp(-elapsed / 0.035);
                m_velocityY += blend * (sample - m_velocityY);
            }
            if (std::abs(delta) > 0.2) m_lastMotionTime = time;
        }
        m_lastPointerTime = time;
        m_lastY = position.Y;
    } else if (type == 2) {
        report("control-pointer-released", S_OK);
        if (m_pointerDown && !m_dragging) {
            hr = bridgeResult(m_api.pointer(m_page, 0, position.X, position.Y - m_overscrollY, 0));
            if (SUCCEEDED(hr)) hr = bridgeResult(m_api.pointer(m_page, 2, position.X, position.Y - m_overscrollY, 0));
            m_redrawPending = true;
        }
        m_pointerDown = false;
        m_inertia = m_dragging && std::abs(m_overscrollY) < 0.1 && std::abs(m_velocityY) >= 60 && now() - m_lastMotionTime < 0.1;
        if (m_inertia) { m_velocityY = std::clamp(m_velocityY * 1.12, -5500.0, 5500.0); report("control-inertia-start", S_OK); }
        else m_velocityY = 0;
        m_lastFrameTime = now();
        m_inputElement->ReleasePointerCaptures();
        WKPageStateUWP state;
        if (!m_api.state(m_page, &state)) report("control-scroll-y", state.scrollY);
    }
    return hr;
}

double WebKitWebViewUWP::now() const
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / m_counterFrequency;
}

void WebKitWebViewUWP::resetMotion()
{
    m_pendingScrollX = m_pendingScrollY = 0;
    m_velocityY = m_overscrollY = m_springVelocity = 0;
    m_inertia = m_pendingElastic = false;
    m_lastFrameTime = now();
}

HRESULT WebKitWebViewUWP::applyScroll(int dx, int dy, bool elastic, bool kinetic)
{
    // Reverse motion first consumes a held edge displacement before moving
    // the document. Page coordinates stay clamped; only presentation bounces.
    if (m_overscrollY * dy > 0) {
        double consumed = std::min(std::abs(static_cast<double>(dy)), std::abs(m_overscrollY));
        consumed = std::copysign(consumed, m_overscrollY);
        m_overscrollY -= consumed;
        dy -= static_cast<int>(consumed);
    }
    WKPageStateUWP before, after;
    HRESULT hr = bridgeResult(m_api.state(m_page, &before));
    if (FAILED(hr)) return hr;
    int maximumY = std::max(0, before.contentHeight - static_cast<int>(before.height));
    int targetY = static_cast<int>(std::clamp<long long>(static_cast<long long>(before.scrollY) + dy, 0, maximumY));
    after = before;
    if (dx || targetY != before.scrollY) {
        hr = bridgeResult(m_api.scroll(m_page, dx, targetY - before.scrollY));
        if (FAILED(hr) || FAILED(hr = bridgeResult(m_api.state(m_page, &after)))) return hr;
        m_redrawPending = true;
    }
    int unused = dy - (after.scrollY - before.scrollY);
    if (unused && elastic) {
        double resistance = kinetic ? 0.2 : 0.4 / (1 + std::abs(m_overscrollY) / 36);
        bool firstEdge = std::abs(m_overscrollY) < 0.1;
        m_overscrollY = std::clamp(m_overscrollY - unused * resistance, -56.0, 56.0);
        if (firstEdge) report("control-edge-hit", S_OK);
        if (kinetic) {
            m_springVelocity = -m_velocityY * 0.2;
            m_inertia = false; m_velocityY = 0;
        }
    }
    return S_OK;
}

HRESULT WebKitWebViewUWP::advanceFrame()
{
    double time = now(), elapsed = time - m_lastFrameTime;
    m_lastFrameTime = time;
    double previousOffset = m_overscrollY;
    if (elapsed > 0.25) {
        // Don't resume an old fling after suspension or a long UI stall.
        m_inertia = false; m_velocityY = m_overscrollY = m_springVelocity = 0;
    }
    double dt = std::clamp(elapsed, 0.0, 0.05);
    if (m_inertia && !m_pointerDown) {
        constexpr double friction = 4.2;
        double next = m_velocityY * std::exp(-friction * dt);
        m_pendingScrollY += (m_velocityY - next) / friction;
        m_velocityY = next;
        if (std::abs(m_velocityY) < 20) {
            m_inertia = false; m_velocityY = 0;
            report("control-inertia-stop", S_OK);
        }
    }
    int dx = static_cast<int>(m_pendingScrollX), dy = static_cast<int>(m_pendingScrollY);
    m_pendingScrollX -= dx; m_pendingScrollY -= dy;
    bool kinetic = m_inertia;
    if (dx || dy) {
        HRESULT hr = applyScroll(dx, dy, m_pendingElastic || kinetic, kinetic);
        if (FAILED(hr)) return hr;
    }
    m_pendingElastic = false;
    if (!m_pointerDown && !m_inertia && std::abs(m_overscrollY) > 0) {
        constexpr double stiffness = 256, damping = 32;
        // Small substeps keep the spring stable across variable frame times.
        for (double remaining = dt; remaining > 0;) {
            double step = std::min(remaining, 1.0 / 120);
            m_springVelocity += (-stiffness * m_overscrollY - damping * m_springVelocity) * step;
            m_overscrollY += m_springVelocity * step;
            remaining -= step;
        }
        if (std::abs(m_overscrollY) < 0.15 && std::abs(m_springVelocity) < 2) {
            m_overscrollY = m_springVelocity = 0;
            report("control-edge-returned", S_OK);
        }
    }
    bool offsetChanged = std::abs(previousOffset - m_overscrollY) > 0.001;
    if (offsetChanged && m_presenterReady && !m_compositorActive) {
        HRESULT hr = m_presenter.setCompositionOffset(0, static_cast<float>(m_overscrollY));
        if (FAILED(hr)) return hr;
    }
    double scrollFinished = now();
    HRESULT hr = bridgeResult(m_api.tick(m_page));
    if (FAILED(hr) && m_compositorActive) {
        report("control-compositor-tick-failed", hr);
        hr = useFallbackPresenter();
        if (SUCCEEDED(hr)) hr = bridgeResult(m_api.tick(m_page));
    }
    unsigned dirty = 0;
    if (SUCCEEDED(hr)) hr = bridgeResult(m_api.needsPaint(m_page, &dirty));
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

void WebKitWebViewUWP::recordFramePacing(double started, double scrollFinished, double engineFinished, bool submitted)
{
    double finished = now();
    bool consecutive = m_previousFrameSubmitted && submitted;
    double intervalMS = (started - m_previousFrameStart) * 1000;
    m_previousFrameSubmitted = submitted;
    m_previousFrameStart = started;
    if (!submitted) return;
    // Cold shader/tile costs are reported separately by the compositor. Do not
    // mix the initial eight submitted callbacks into warmed percentile samples.
    if (m_frameWarmupCount < 8) { ++m_frameWarmupCount; return; }
    double workMS = (finished - started) * 1000;
    m_frameWorkSamples[m_frameWorkCount++] = workMS;
    m_scrollWorkTotal += (scrollFinished - started) * 1000;
    m_engineWorkTotal += (engineFinished - scrollFinished) * 1000;
    m_renderWorkTotal += (finished - engineFinished) * 1000;
    if (workMS > 1000.0 / 60) ++m_frameOverBudget;
    if (consecutive && intervalMS > 0 && intervalMS < 250) {
        m_frameIntervalSamples[m_frameIntervalCount++] = intervalMS;
        if (intervalMS > 25) ++m_frameLongIntervals;
    }
    if (m_frameWorkCount < m_frameWorkSamples.size()) return;
    auto summarize = [](auto samples, unsigned count, double& average, double& p95, double& maximum) {
        average = p95 = maximum = 0;
        if (!count) return;
        for (unsigned i = 0; i < count; ++i) average += samples[i];
        average /= count;
        std::sort(samples.begin(), samples.begin() + count);
        p95 = samples[(count * 95 + 99) / 100 - 1];
        maximum = samples[count - 1];
    };
    double workAverage, workP95, workMaximum, intervalAverage, intervalP95, intervalMaximum;
    summarize(m_frameWorkSamples, m_frameWorkCount, workAverage, workP95, workMaximum);
    summarize(m_frameIntervalSamples, m_frameIntervalCount, intervalAverage, intervalP95, intervalMaximum);
    char summary[512];
    sprintf_s(summary, "frame-profile n=%u work-us=%.0f/%.0f/%.0f interval-n=%u interval-us=%.0f/%.0f/%.0f callback-mHz=%.0f over-budget=%u long-intervals=%u scroll-us=%.0f engine-us=%.0f render-us=%.0f",
        m_frameWorkCount, workAverage * 1000, workP95 * 1000, workMaximum * 1000,
        m_frameIntervalCount, intervalAverage * 1000, intervalP95 * 1000, intervalMaximum * 1000,
        intervalAverage > 0 ? 1000000 / intervalAverage : 0, m_frameOverBudget, m_frameLongIntervals,
        m_scrollWorkTotal * 1000 / m_frameWorkCount, m_engineWorkTotal * 1000 / m_frameWorkCount, m_renderWorkTotal * 1000 / m_frameWorkCount);
    // Callback cadence is not proof of scan-out; this includes engine and swap
    // work, but excludes time spent reporting this summary and OS input delivery.
    report(summary, S_OK);
    m_frameWorkCount = m_frameIntervalCount = m_frameOverBudget = m_frameLongIntervals = 0;
    m_scrollWorkTotal = m_engineWorkTotal = m_renderWorkTotal = 0;
}

void WebKitWebViewUWP::recordFrame(double paintMS)
{
    double time = now();
    if (!m_profileFrames) m_profileStart = time;
    auto timing = m_presenter.frameTiming();
    ++m_profileFrames;
    m_paintTotal += paintMS;
    m_uploadTotal += timing.uploadMS;
    m_copyTotal += timing.copyMS;
    m_presentTotal += timing.presentMS;
    m_frameMax = std::max(m_frameMax, paintMS + timing.uploadMS + timing.copyMS + timing.presentMS);
    if (m_profileFrames >= 8 && time - m_profileStart >= 1) {
        report("perf-paint-average-us", static_cast<unsigned>(m_paintTotal * 1000 / m_profileFrames));
        report("perf-upload-average-us", static_cast<unsigned>(m_uploadTotal * 1000 / m_profileFrames));
        report("perf-copy-submit-average-us", static_cast<unsigned>(m_copyTotal * 1000 / m_profileFrames));
        report("perf-present-average-us", static_cast<unsigned>(m_presentTotal * 1000 / m_profileFrames));
        report("perf-frame-max-us", static_cast<unsigned>(m_frameMax * 1000));
        report("perf-paint-sample-count", m_profileFrames);
        m_profileFrames = 0;
        m_paintTotal = m_uploadTotal = m_copyTotal = m_presentTotal = m_frameMax = 0;
    }
}

WebKitWebViewUWP::~WebKitWebViewUWP()
{
    if (m_composition) m_composition->remove_Rendering(m_renderToken);
    if (m_panelElement) {
        ComPtr<Xaml::IFrameworkElement> element;
        if (SUCCEEDED(m_panelElement.As(&element))) element->remove_SizeChanged(m_sizeToken);
        ComPtr<Controls::ISwapChainPanel> scale;
        if (SUCCEEDED(m_panelElement.As(&scale))) scale->remove_CompositionScaleChanged(m_scaleToken);
        ComPtr<ISwapChainPanelNative> panel;
        if (SUCCEEDED(m_panelElement.As(&panel))) panel->SetSwapChain(nullptr);
    }
    if (m_inputElement) {
        m_inputElement->remove_PointerPressed(m_pressToken);
        m_inputElement->remove_PointerMoved(m_moveToken);
        m_inputElement->remove_PointerReleased(m_releaseToken);
        m_inputElement->remove_PointerWheelChanged(m_wheelToken);
        m_inputElement->remove_PointerCanceled(m_cancelToken);
    }
    if (m_page) m_api.destroy(m_page);
    // Keep WebCore loaded: other controls or pending engine tasks may use it.
}
}
