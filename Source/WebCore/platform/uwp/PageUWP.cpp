#include "config.h"
#include "PageUWP.h"
#include "ClipboardUWP.h"
#include "Document.h"
#include "DocumentView.h"
#include "DocumentLoader.h"
#include "DocumentWriter.h"
#include "EmptyClients.h"
#include "EventHandler.h"
#include "HandleUserInputEventResult.h"
#include "FrameLoader.h"
#if USE(CAIRO)
#include "GraphicsContextCairo.h"
#elif USE(SKIA)
#include "GraphicsContextSkia.h"
#include <skia/core/SkSurface.h>
#endif
#include "LocalFrame.h"
#include "LocalFrameInlines.h"
#include "LocalFrameView.h"
#include "Page.h"
#include "PageConfiguration.h"
#include "PlatformMouseEvent.h"
#include "Settings.h"
#include "TextureMapperCompositorUWP.h"
#include <wtf/RunLoop.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/WTFString.h>
#include <algorithm>
#include <cmath>
#include <windows.ui.core.h>
#include <wrl.h>

using namespace WebCore;

namespace {
struct PageHostUWP {
    RefPtr<Page> page;
    DWORD owner { GetCurrentThreadId() };
    unsigned width, height;
    WKPageLogUWP log;
    bool dirty { true };
    std::unique_ptr<TextureMapperCompositorUWP> compositor;
    bool compositorActive { false };
    double compositorScale { 1 };
    ~PageHostUWP()
    {
        if (compositor) { compositor->makeCurrent(); compositor->setRootLayer(nullptr); }
        page = nullptr;
        compositor.reset();
    }
    bool validThread() const { return owner == GetCurrentThreadId(); }
    LocalFrame& frame() const { return *page->localMainFrame(); }
};
class PageChromeUWP final : public EmptyChromeClient {
    WTF_MAKE_TZONE_ALLOCATED(PageChromeUWP);
public:
    explicit PageChromeUWP(PageHostUWP& owner) : m_owner(owner) { }
private:
    void invalidateRootView(const IntRect&) final { m_owner.dirty = true; }
    void invalidateContentsForSlowScroll(const IntRect& rect) final { invalidateContentsAndRootView(rect); }
    void scroll(const IntSize&, const IntRect& rect, const IntRect& clip) final
    {
        // The TextureMapper host doesn't blit its non-composited backing store.
        // Repaint the complete scrolled area rather than only the exposed strip.
        auto dirtyRect = rect;
        dirtyRect.intersect(clip);
        invalidateContentsAndRootView(dirtyRect);
    }
    void invalidateContentsAndRootView(const IntRect& rect) final
    {
        m_owner.dirty = true;
        if (m_owner.compositorActive) m_owner.compositor->invalidate(rect);
    }
    void attachRootGraphicsLayer(LocalFrame&, GraphicsLayer* layer) final
    {
        if (m_owner.compositor) m_owner.compositor->setRootLayer(layer);
        m_owner.dirty = true;
    }
    void triggerRenderingUpdate() final { m_owner.dirty = true; }
    bool scheduleRenderingUpdate() final { m_owner.dirty = true; return true; }
    PageHostUWP& m_owner;
};
WTF_MAKE_TZONE_ALLOCATED_IMPL(PageChromeUWP);
PageHostUWP* host(void* handle) { return static_cast<PageHostUWP*>(handle); }
bool validSize(unsigned width, unsigned height) { return width && height && width <= 2048 && height <= 2048; }
}

extern "C" __declspec(dllexport) void* __stdcall WKCreatePageUWP(const wchar_t* fonts, unsigned width, unsigned height, WKPageLogUWP log, unsigned* result)
{
    if (!result)
        return nullptr;
    *result = 1;
    if (!validSize(width, height))
        return nullptr;
    *result = WKInitializeEngineUWP(fonts, log);
    if (*result)
        return nullptr;
    auto handle = std::make_unique<PageHostUWP>();
    handle->width = width; handle->height = height; handle->log = log;
    auto configuration = pageConfigurationWithEmptyClients(std::nullopt, PAL::SessionID::defaultSessionID());
    configuration.chromeClient = makeUniqueRef<PageChromeUWP>(*handle);
    // App-supplied local HTML has the normal script sandbox policy. External
    // requests remain unsupported by the current local-only loader strategy.
    auto& local = std::get<PageConfiguration::LocalMainFrameCreationParameters>(configuration.mainFrameCreationParameters);
    local.effectiveSandboxFlags = { };
    auto page = Page::create(WTF::move(configuration));
    page->settings().setScriptEnabled(true);
    page->settings().setAcceleratedCompositingEnabled(false);
    page->settings().setShouldAllowUserInstalledFonts(true);
    page->settings().setDefaultFontSize(18);
    auto* frame = page->localMainFrame();
    if (!frame) { *result = 4; return nullptr; }
    frame->setView(LocalFrameView::create(*frame, { static_cast<int>(width), static_cast<int>(height) }));
    frame->init();
    frame->view()->setCanHaveScrollbars(false);
    handle->page = WTF::move(page);
    *result = 0;
    if (log) log("persistent-page-created", 0);
    return handle.release();
}

extern "C" __declspec(dllexport) unsigned __stdcall WKDestroyPageUWP(void* handle)
{
    if (!handle) return 0;
    if (!host(handle)->validThread()) return 8;
    delete host(handle);
    return 0;
}

extern "C" __declspec(dllexport) unsigned __stdcall WKLoadHTMLUWP(void* handle, const char* utf8)
{
    if (!handle || !utf8) return 1;
    auto& view = *host(handle);
    if (!view.validThread()) return 8;
    auto* loader = view.frame().loader().activeDocumentLoader();
    if (!loader) return 5;
    auto& writer = loader->writer();
    writer.setMIMEType("text/html"_s);
    if (!writer.begin(URL { })) return 7;
    writer.insertDataSynchronously(String::fromUTF8(utf8));
    writer.end();
    view.frame().view()->setScrollPosition({ 0, 0 });
    view.frame().view()->updateLayoutAndStyleIfNeededRecursive();
    view.dirty = true;
    if (view.log) view.log("persistent-html-loaded", 0);
    return 0;
}

extern "C" __declspec(dllexport) unsigned __stdcall WKResizePageUWP(void* handle, unsigned width, unsigned height)
{
    if (!handle || !validSize(width, height)) return 1;
    auto& view = *host(handle);
    if (!view.validThread()) return 8;
    view.width = width; view.height = height;
    if (view.compositorActive) view.compositor->resize(width, height, view.compositorScale);
    view.frame().view()->setFrameRect({ 0, 0, static_cast<int>(width), static_cast<int>(height) });
    view.frame().view()->updateLayoutAndStyleIfNeededRecursive();
    view.dirty = true;
    return 0;
}

extern "C" __declspec(dllexport) unsigned __stdcall WKScrollPageUWP(void* handle, int dx, int dy)
{
    if (!handle) return 1;
    auto& view = *host(handle);
    if (!view.validThread()) return 8;
    auto& frameView = *view.frame().view();
    frameView.updateLayoutAndStyleIfNeededRecursive();
    auto position = frameView.scrollPosition();
    auto maximum = frameView.maximumScrollPosition();
    int x = static_cast<int>(std::clamp<long long>(static_cast<long long>(position.x()) + dx, 0, std::max(0, maximum.x())));
    int y = static_cast<int>(std::clamp<long long>(static_cast<long long>(position.y()) + dy, 0, std::max(0, maximum.y())));
    frameView.setScrollPosition({ x, y });
    view.dirty = true;
    return 0;
}

extern "C" __declspec(dllexport) unsigned __stdcall WKPointerPageUWP(void* handle, unsigned type, double x, double y, unsigned modifiers)
{
    if (!handle || type > 2) return 1;
    auto& view = *host(handle);
    if (!view.validThread()) return 8;
    OptionSet<PlatformEvent::Modifier> flags;
    if (modifiers & 1) flags.add(PlatformEvent::Modifier::ShiftKey);
    if (modifiers & 2) flags.add(PlatformEvent::Modifier::ControlKey);
    if (modifiers & 4) flags.add(PlatformEvent::Modifier::AltKey);
    auto eventType = type == 0 ? PlatformEvent::Type::MousePressed : type == 1 ? PlatformEvent::Type::MouseMoved : PlatformEvent::Type::MouseReleased;
    PlatformMouseEvent event({ x, y }, { x, y }, MouseButton::Left, eventType, type == 1 ? 0 : 1, flags, MonotonicTime::now(), 0, SyntheticClickType::NoTap, MouseEventInputSource::UserDriven);
    if (!type) view.frame().eventHandler().handleMousePressEvent(event);
    else if (type == 1) view.frame().eventHandler().mouseMoved(event);
    else view.frame().eventHandler().handleMouseReleaseEvent(event);
    view.dirty = true;
    return 0;
}

extern "C" __declspec(dllexport) unsigned __stdcall WKTickPageUWP(void* handle)
{
    if (!handle) return 1;
    if (!host(handle)->validThread()) return 8;
    WTF::RunLoop::cycle();
    if (host(handle)->compositorActive && !host(handle)->compositor->makeCurrent()) return 10;
    host(handle)->page->updateRendering();
    host(handle)->page->finalizeRenderingUpdate({ });
    return 0;
}

extern "C" __declspec(dllexport) long __stdcall WKInstallClipboardDispatcherUWP(void* handle, void* dispatcher)
{
    if (!handle || !dispatcher) return E_INVALIDARG;
    auto& view = *host(handle);
    if (!view.validThread()) return RPC_E_WRONG_THREAD;
    namespace Core = ABI::Windows::UI::Core;
    Microsoft::WRL::ComPtr<Core::ICoreDispatcher> ui;
    HRESULT result = static_cast<IInspectable*>(dispatcher)->QueryInterface(IID_PPV_ARGS(&ui));
    if (FAILED(result)) return result;
    boolean hasAccess = false;
    if (FAILED(result = ui->get_HasThreadAccess(&hasAccess))) return result;
    // This standalone control owns WebCore on the XAML UI thread. Reject a
    // different apartment rather than installing an invalid host dispatcher.
    if (!hasAccess) return RPC_E_WRONG_THREAD;
    ClipboardUWP::setUIDispatcher([ui](Function<void()>&& task) -> bool {
        boolean hasAccess = false;
        if (FAILED(ui->get_HasThreadAccess(&hasAccess))) return false;
        if (hasAccess) {
            task();
            return true;
        }
        auto pending = std::make_shared<Function<void()>>(WTF::move(task));
        auto callback = Microsoft::WRL::Callback<Core::IDispatchedHandler>([pending]() -> HRESULT {
            (*pending)();
            return S_OK;
        });
        if (!callback) return false;
        Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncAction> action;
        return SUCCEEDED(ui->RunAsync(Core::CoreDispatcherPriority_Normal, callback.Get(), &action));
    });
    if (view.log) view.log("clipboard-ui-dispatcher-installed", S_OK);
    return S_OK;
}

extern "C" __declspec(dllexport) unsigned __stdcall WKPaintPageUWP(void* handle, unsigned char* bgra, size_t capacity, unsigned stride)
{
    return WKPaintPageScaledUWP(handle, bgra, capacity, stride, 1, 1);
}

extern "C" __declspec(dllexport) unsigned __stdcall WKPaintPageScaledUWP(void* handle, unsigned char* bgra, size_t capacity, unsigned stride, double scaleX, double scaleY)
{
    if (!handle || !bgra) return 1;
    auto& view = *host(handle);
    if (!view.validThread()) return 8;
    if (!std::isfinite(scaleX) || !std::isfinite(scaleY) || scaleX <= 0 || scaleY <= 0 || scaleX > 8 || scaleY > 8) return 1;
    unsigned pixelWidth = static_cast<unsigned>(std::ceil(view.width * scaleX));
    unsigned pixelHeight = static_cast<unsigned>(std::ceil(view.height * scaleY));
    if (pixelWidth > 4096 || pixelHeight > 4096 || stride < pixelWidth * 4 || stride > 16384 || capacity < static_cast<size_t>(stride) * pixelHeight) return 1;
    if (view.page->deviceScaleFactor() != static_cast<float>(scaleX))
        view.page->setDeviceScaleFactor(scaleX);
    view.frame().view()->updateLayoutAndStyleIfNeededRecursive();
#if USE(CAIRO)
    auto* surface = cairo_image_surface_create_for_data(bgra, CAIRO_FORMAT_ARGB32, pixelWidth, pixelHeight, stride);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) { cairo_surface_destroy(surface); return 6; }
#elif USE(SKIA)
    auto surface = SkSurfaces::WrapPixels(SkImageInfo::Make(pixelWidth, pixelHeight, kBGRA_8888_SkColorType, kPremul_SkAlphaType), bgra, stride);
    if (!surface) return 6;
#endif
    {
#if USE(CAIRO)
        GraphicsContextCairo context(surface);
#elif USE(SKIA)
        // CPU snapshots/capture remain separate from GPU tile painting.
        GraphicsContextSkia context(*surface->getCanvas(), RenderingMode::Unaccelerated, RenderingPurpose::Unspecified);
#endif
        context.scale({ static_cast<float>(scaleX), static_cast<float>(scaleY) });
        context.clearRect({ 0, 0, static_cast<float>(view.width), static_cast<float>(view.height) });
        // Snapshot painting includes composited contents and viewport translation.
        view.frame().view()->paintContentsForSnapshot(context, { 0, 0, static_cast<int>(view.width), static_cast<int>(view.height) }, nullptr, LocalFrameView::IncludeSelection, LocalFrameView::ViewCoordinates);
    }
#if USE(CAIRO)
    cairo_surface_flush(surface);
    cairo_surface_destroy(surface);
#endif
    view.dirty = false;
    return 0;
}

extern "C" __declspec(dllexport) unsigned __stdcall WKEnableCompositorUWP(void* handle, void* nativePanel, double scaleX, double scaleY)
{
    if (!handle || !nativePanel || !std::isfinite(scaleX) || !std::isfinite(scaleY) || scaleX <= 0 || scaleX > 8 || std::abs(scaleX - scaleY) > 0.01) return 1;
    auto& view = *host(handle);
    if (!view.validThread()) return 8;
    if (view.compositorActive && view.compositorScale == scaleX) return 0;
    if (view.compositor) return 9; // Context scale changes need a complete graphics-layer rebuild.
    auto compositor = std::make_unique<TextureMapperCompositorUWP>(view.frame(), view.log);
    if (!compositor->initialize(nativePanel, view.width, view.height, scaleX)) return 9;
    view.compositor = WTF::move(compositor);
    view.compositorScale = scaleX;
    view.compositorActive = true;
    view.page->setDeviceScaleFactor(scaleX);
    view.page->settings().setAcceleratedCompositingEnabled(true);
    view.page->settings().setForceCompositingMode(true);
    view.page->settings().setAcceleratedCompositingForFixedPositionEnabled(true);
    view.frame().view()->updateLayoutAndStyleIfNeededRecursive();
    view.page->updateRendering();
    view.page->finalizeRenderingUpdate({ });
    view.dirty = true;
    if (view.log) view.log("gpu-compositor-enabled", 0);
    return 0;
}

extern "C" __declspec(dllexport) unsigned __stdcall WKDisableCompositorUWP(void* handle)
{
    if (!handle) return 1;
    auto& view = *host(handle);
    if (!view.validThread()) return 8;
    if (view.compositor) view.compositor->makeCurrent();
    view.page->settings().setForceCompositingMode(false);
    view.page->settings().setAcceleratedCompositingEnabled(false);
    view.frame().view()->updateLayoutAndStyleIfNeededRecursive();
    if (view.compositor) view.compositor->setRootLayer(nullptr);
    view.compositorActive = false;
    view.dirty = true;
    return 0;
}

extern "C" __declspec(dllexport) unsigned __stdcall WKRenderCompositorUWP(void* handle, double overscrollY)
{
    if (!handle || !std::isfinite(overscrollY)) return 1;
    auto& view = *host(handle);
    if (!view.validThread()) return 8;
    if (!view.compositorActive || !view.compositor) return 9;
    if (!view.compositor->render(overscrollY)) return 10;
    view.dirty = false;
    return 0;
}

extern "C" __declspec(dllexport) unsigned __stdcall WKPageNeedsPaintUWP(void* handle, unsigned* needsPaint)
{
    if (!handle || !needsPaint) return 1;
    auto& view = *host(handle);
    if (!view.validThread()) return 8;
    view.frame().view()->updateLayoutAndStyleIfNeededRecursive();
    *needsPaint = view.dirty || (view.compositorActive && view.compositor->needsFrame());
    return 0;
}

extern "C" __declspec(dllexport) unsigned __stdcall WKGetPageStateUWP(void* handle, WKPageStateUWP* state)
{
    if (!handle || !state) return 1;
    auto& view = *host(handle);
    if (!view.validThread()) return 8;
    auto& frameView = *view.frame().view();
    auto position = frameView.scrollPosition();
    auto contents = frameView.contentsSize();
    *state = { view.width, view.height, position.x(), position.y(), contents.width(), contents.height() };
    return 0;
}
