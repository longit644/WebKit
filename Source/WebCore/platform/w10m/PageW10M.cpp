#include "config.h"
#include "PageW10M.h"
#include "BackForwardClient.h"
#include "BackForwardClientW10M.h"
#include "BackForwardController.h"
#include "ChromeClientW10M.h"
#include "ClipboardW10M.h"
#include "ContainerNodeInlines.h"
#include "CookieJar.h"
#include "CurlContext.h"
#include "Document.h"
#include "DocumentLoader.h"
#include "DocumentView.h"
#include "DocumentWriter.h"
#include "EmptyClients.h"
#include "EmptyFrameLoaderClient.h"
#include "EngineW10M.h"
#include "EventHandler.h"
#include "Font.h"
#include "FontCascadeInlines.h"
#include "FrameDestructionObserverInlines.h"
#include "FrameLoadRequest.h"
#include "FrameLoader.h"
#include "FrameLoaderClientW10M.h"
#include "HTMLImageElement.h"
#include "HandleUserInputEventResult.h"
#include "HistoryController.h"
#include "HistoryItem.h"
#include "LoaderStrategyW10M.h"
#include "MIMETypeRegistry.h"
#include "MemoryCache.h"
#include "NetworkStorageSession.h"
#include "PageHostW10M.h"
#include "WKW10MPageForwardW10M.h"
#include "RenderElement.h"
#include "ResourceError.h"
#include "ResourcePolicyW10M.h"
#include "ResourceRequest.h"
#include "ResourceResponse.h"
#include "StorageSessionProvider.h"
#include "StorageSessionProviderW10M.h"
#include "StyleComputedStyle+GettersInlines.h"
#include <wtf/NeverDestroyed.h>
#include <wtf/text/MakeString.h>
#if USE(CAIRO)
#include "GraphicsContextCairo.h"
#elif USE(SKIA)
#include "GraphicsContextSkia.h"
#include <skia/core/SkSurface.h>
#endif
#include "HTMLAnchorElement.h"
#include "HTMLBodyElement.h"
#include "HTMLElement.h"
#include "HitTestRequest.h"
#include "HitTestResult.h"
#include "LocalFrame.h"
#include "LocalFrameInlines.h"
#include "LocalFrameView.h"
#include "Page.h"
#include "PageConfiguration.h"
#include "PlatformMouseEvent.h"
#include "Settings.h"
#include "TextureMapperCompositorW10M.h"
#include "TlsPolicyW10M.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <windows.ui.core.h>
#include <wrl.h>
#include <wtf/RunLoop.h>
#include <wtf/Seconds.h>
#include <wtf/TZoneMallocInlines.h>
#include <wtf/text/WTFString.h>

using namespace WebCore;

// Session lifetime follows host teardown, not the last cached CookieJar.
// Cancel transfers before destroying the provider's cookie database, and
// evict only this page's ephemeral session so other controls stay intact.
PageHostW10M::~PageHostW10M()
{
    releaseEngineCallbacksW10M(*this);
    probeLifetime.reset();
    if (page)
        page->forEachLocalFrame([](LocalFrame& frame) { frame.loader().stopAllLoaders(); });
    if (compositor) {
        compositor->makeCurrent();
        compositor->setRootLayer(nullptr);
    }
    if (page) {
        auto sessionID = page->sessionID();
        removeProceedExceptions(sessionID);
        page = nullptr;
        MemoryCache::singleton().evictResources(sessionID);
        ResourcePolicyW10M::resetSessionLimits(sessionID);
        TlsPolicyW10M::eraseSession(sessionID);
    }
    if (storageProvider)
        storageProvider->close();
    storageProvider = nullptr;
    compositor.reset();
}

// Casts the opaque WK handle back to its host; every entry point funnels here.
PageHostW10M* host(void* handle) { return static_cast<PageHostW10M*>(handle); }

// Paint-buffer uniformity tolerance; the compositor needs square pixels.
constexpr double kScaleUniformityToleranceW10M = 0.01;

// Nonempty extents inside the compositor's presentable range.
bool validSize(unsigned width, unsigned height) { return width && height && width <= W10MMaxPageDimension && height <= W10MMaxPageDimension; }

// Creates the persistent page, per-page history, cookie session and host clients.
extern "C" __declspec(dllexport) void* __stdcall WKCreatePageW10M(const wchar_t* fonts, unsigned width, unsigned height, WKPageLogW10M log, unsigned* result)
{
    if (!result)
        return nullptr;
    *result = WKPageResultInvalidArgumentW10M;
    if (!validSize(width, height))
        return nullptr;
    *result = WKInitializeEngineW10M(fonts, log);
    if (*result != WKPageResultOkW10M)
        return nullptr;
    auto handle = std::make_unique<PageHostW10M>();
    handle->width = width;
    handle->height = height;
    handle->log = log;
    // DOM and transport cookies share one in-memory session per page. The
    // host explicitly closes it even when cached resources retain its jar.
    auto sessionID = PAL::SessionID::generateEphemeralSessionID();
    auto configuration = pageConfigurationWithEmptyClients(std::nullopt, sessionID);
    configuration.backForwardClient = BackForwardClientW10M::create();
    auto storageProvider = adoptRef(*new StorageSessionProviderW10M(sessionID));
    handle->storageProvider = storageProvider.copyRef();
    configuration.cookieJar = CookieJar::create(WTF::move(storageProvider));
    configuration.chromeClient = makeUniqueRef<ChromeClientW10M>(*handle);
    // App-supplied local HTML has the normal script sandbox policy. External
    // requests remain unsupported by the current local-only loader strategy.
    auto& local = std::get<PageConfiguration::LocalMainFrameCreationParameters>(configuration.mainFrameCreationParameters);
    local.effectiveSandboxFlags = {};
    local.clientCreator = [owner = handle.get()](LocalFrame&, FrameLoader& loader) -> UniqueRef<LocalFrameLoaderClient> {
        return makeUniqueRefWithoutRefCountedCheck<FrameLoaderClientW10M>(loader, *owner);
    };
    auto page = Page::create(WTF::move(configuration));
    page->settings().setScriptEnabled(true);
    page->settings().setAcceleratedCompositingEnabled(false);
    page->settings().setShouldAllowUserInstalledFonts(true);
    page->settings().setDefaultFontSize(W10MDefaultFontSize);
    // loadsImagesAutomatically defaults to false for WebCore-only builds (see
    // UnifiedWebPreferences.yaml: the WebCore default, not WebKit's true).
    // CachedImage::load() consults it through shouldPerformImageLoad() and
    // calls setLoading(false) otherwise, which silently drops every <img>
    // network request without raising an error event on the element.
    page->settings().setLoadsImagesAutomatically(true);
    page->settings().setImagesEnabled(true);
    // The app-scoped fontconfig contains only the packaged fonts and no system
    // alias configuration. Resolve CSS generic families to an available face,
    // otherwise an empty primary font has zero metrics even when glyph fallback
    // paints text, leaving inline links with zero-height hit-test boxes.
    String bundledFontFamily = String::fromUTF8(W10MDefaultFontFamily);
    page->settings().setStandardFontFamily(bundledFontFamily);
    page->settings().setSerifFontFamily(bundledFontFamily);
    page->settings().setSansSerifFontFamily(bundledFontFamily);
    page->settings().setFixedFontFamily(bundledFontFamily);
    page->settings().setCursiveFontFamily(bundledFontFamily);
    page->settings().setFantasyFontFamily(bundledFontFamily);
    auto* frame = page->localMainFrame();
    if (!frame) {
        *result = WKPageResultNoMainFrameW10M;
        return nullptr;
    }
    frame->setView(LocalFrameView::create(*frame, { static_cast<int>(width), static_cast<int>(height) }));
    frame->init();
    frame->view()->setCanHaveScrollbars(false);
    handle->page = WTF::move(page);
    *result = WKPageResultOkW10M;
    if (log)
        log("persistent-page-created", WKPageLogFalseW10M);
    return handle.release();
}

// Destroys the page on its owning engine thread; null is a successful no-op.
extern "C" __declspec(dllexport) unsigned __stdcall WKDestroyPageW10M(void* handle)
{
    if (!handle)
        return WKPageResultOkW10M;
    if (!host(handle)->validThread())
        return WKPageResultWrongThreadW10M;
    delete host(handle);
    return WKPageResultOkW10M;
}

// Loads app-supplied UTF-8 markup into the active document writer.
extern "C" __declspec(dllexport) unsigned __stdcall WKLoadHTMLW10M(void* handle, const char* utf8)
{
    if (!handle || !utf8)
        return WKPageResultInvalidArgumentW10M;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadW10M;
    auto* loader = view.frame().loader().activeDocumentLoader();
    if (!loader)
        return WKPageResultNoActiveDocumentLoaderW10M;
    auto& writer = loader->writer();
    writer.setMIMEType("text/html"_s);
    if (!writer.begin(URL {}))
        return WKPageResultDocumentWriterBeginFailedW10M;
    // Host HTML is an accepted command with its own navigation ID.
    view.navigation.beginNavigation();
    writer.insertDataSynchronously(String::fromUTF8(utf8));
    writer.end();
    view.frame().view()->setScrollPosition({ 0, 0 });
    view.frame().view()->updateLayoutAndStyleIfNeededRecursive();
    view.dirty = true;
    if (view.log)
        view.log("persistent-html-loaded", WKPageLogFalseW10M);
    return WKPageResultOkW10M;
}

// Starts an HTTP(S) navigation using the supplied CA bundle and strict TLS verification.
extern "C" __declspec(dllexport) unsigned __stdcall WKNavigatePageW10M(void* handle, const char* utf8, const wchar_t* caPath)
{
    if (!handle || !utf8 || !caPath)
        return WKPageResultInvalidArgumentW10M;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadW10M;
    URL url(String::fromUTF8(utf8));
    if (!url.isValid() || !url.protocolIsInHTTPFamily())
        return WKPageResultInvalidArgumentW10M;
    // Trust is snapshotted per session, never mutated globally: the first CA
    // bundle a control navigates with freezes for its lifetime, so concurrent
    // controls cannot rebind each other's verification through the process
    // singleton. A later different bundle for the same session is ignored.
    TlsPolicyW10M::snapshotSessionBundle(host(handle)->page->sessionID(), String(caPath));
    ResourceRequest request;
    request.setURL(WTF::move(url));
    // Main-frame loads get a full minute before the transport gives up.
    constexpr double kNavigationTimeoutSeconds = 60;
    request.setTimeoutInterval(kNavigationTimeoutSeconds);
    // Accepted commands allocate a navigation ID that survives redirects and
    // is never reused; validation above already rejected malformed commands.
    view.navigation.beginNavigation();
    view.frame().loader().load(FrameLoadRequest(view.frame(), WTF::move(request)));
    view.dirty = true;
    return WKPageResultOkW10M;
}

// Traverses one step toward the oldest session-history item.
extern "C" __declspec(dllexport) unsigned __stdcall WKGoBackPageW10M(void* handle)
{
    if (!handle)
        return WKPageResultInvalidArgumentW10M;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadW10M;
    bool moved = view.page->backForward().goBack();
    if (view.log)
        view.log("history-go-back", moved);
    view.dirty = true;
    return WKPageResultOkW10M;
}

// Traverses one step toward the newest session-history item.
extern "C" __declspec(dllexport) unsigned __stdcall WKGoForwardPageW10M(void* handle)
{
    if (!handle)
        return WKPageResultInvalidArgumentW10M;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadW10M;
    bool moved = view.page->backForward().goForward();
    if (view.log)
        view.log("history-go-forward", moved);
    view.dirty = true;
    return WKPageResultOkW10M;
}

// Reloads the current history item.
extern "C" __declspec(dllexport) unsigned __stdcall WKReloadPageW10M(void* handle)
{
    if (!handle)
        return WKPageResultInvalidArgumentW10M;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadW10M;
    // Reload allocates a fresh ID with no history entry, matching the V1 rule.
    view.navigation.beginNavigation();
    view.frame().loader().reload();
    if (view.log)
        view.log("history-reload", WKPageLogFalseW10M);
    view.dirty = true;
    return WKPageResultOkW10M;
}

// Stops all loads owned by the current frame.
extern "C" __declspec(dllexport) unsigned __stdcall WKStopPageW10M(void* handle)
{
    if (!handle)
        return WKPageResultInvalidArgumentW10M;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadW10M;
    view.frame().loader().stopAllLoaders();
    if (view.log)
        view.log("history-stop", WKPageLogFalseW10M);
    view.dirty = true;
    return WKPageResultOkW10M;
}

// Installs or clears the synchronous navigation-policy callback.
extern "C" __declspec(dllexport) unsigned __stdcall WKSetNavigationPolicyHandlerPageW10M(void* handle, WKNavigationPolicyCallback callback, void* context)
{
    if (!handle)
        return WKPageResultInvalidArgumentW10M;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadW10M;
    view.policyHandler = callback;
    view.policyContext = callback ? context : nullptr;
    return WKPageResultOkW10M;
}

// Updates the CSS-pixel viewport and lays out the frame.
extern "C" __declspec(dllexport) unsigned __stdcall WKResizePageW10M(void* handle, unsigned width, unsigned height)
{
    if (!handle || !validSize(width, height))
        return WKPageResultInvalidArgumentW10M;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadW10M;
    view.width = width;
    view.height = height;
    if (view.compositorActive)
        view.compositor->resize(width, height, view.compositorScale);
    view.frame().view()->setFrameRect({ 0, 0, static_cast<int>(width), static_cast<int>(height) });
    view.frame().view()->updateLayoutAndStyleIfNeededRecursive();
    view.dirty = true;
    return WKPageResultOkW10M;
}

// Scrolls within the current document's clamped scroll range.
extern "C" __declspec(dllexport) unsigned __stdcall WKScrollPageW10M(void* handle, int dx, int dy)
{
    if (!handle)
        return WKPageResultInvalidArgumentW10M;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadW10M;
    auto& frameView = *view.frame().view();
    frameView.updateLayoutAndStyleIfNeededRecursive();
    auto position = frameView.scrollPosition();
    auto maximum = frameView.maximumScrollPosition();
    int x = static_cast<int>(std::clamp<long long>(static_cast<long long>(position.x()) + dx, 0, std::max(0, maximum.x())));
    int y = static_cast<int>(std::clamp<long long>(static_cast<long long>(position.y()) + dy, 0, std::max(0, maximum.y())));
    frameView.setScrollPosition({ x, y });
    view.dirty = true;
    return WKPageResultOkW10M;
}

// Converts a host pointer event to a WebCore mouse event.
extern "C" __declspec(dllexport) unsigned __stdcall WKPointerPageW10M(void* handle, unsigned type, double x, double y, unsigned modifiers)
{
    if (!handle || type > WKPointerEventReleaseW10M)
        return WKPageResultInvalidArgumentW10M;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadW10M;
    OptionSet<PlatformEvent::Modifier> flags;
    if (modifiers & WKPointerModifierShiftW10M)
        flags.add(PlatformEvent::Modifier::ShiftKey);
    if (modifiers & WKPointerModifierControlW10M)
        flags.add(PlatformEvent::Modifier::ControlKey);
    if (modifiers & WKPointerModifierAltW10M)
        flags.add(PlatformEvent::Modifier::AltKey);
    auto eventType = type == WKPointerEventPressW10M ? PlatformEvent::Type::MousePressed : type == WKPointerEventMoveW10M ? PlatformEvent::Type::MouseMoved
                                                                                                                        : PlatformEvent::Type::MouseReleased;
    if (view.log)
        view.log("tap-stage-enter", type);
    // Click count belongs to press/release, never motion; no keyboard modifier is synthesized.
    constexpr int kNoClickCount = 0;
    constexpr int kPointerClickCount = 1;
    PlatformMouseEvent event({ x, y }, { x, y }, MouseButton::Left, eventType, type == WKPointerEventMoveW10M ? kNoClickCount : kPointerClickCount, flags, MonotonicTime::now(), WKPointerModifierNoneW10M, SyntheticClickType::NoTap, MouseEventInputSource::UserDriven);
    if (view.log)
        view.log("tap-stage-event-created", type);
    if (type != WKPointerEventMoveW10M && view.log) {
        // Report what a tap actually hits before dispatching it. Distinguishes a
        // coordinate-space bug (no/other node found) from a broken event path
        // (correct node, but no navigation follows).
        auto result = view.frame().hitTestResultAtPoint(IntPoint { static_cast<int>(x), static_cast<int>(y) }, HitTestRequest::Type::ReadOnly);
        Node* node = result.targetNode();
        auto* anchor = dynamicDowncast<HTMLAnchorElement>(node);
        if (node) {
            auto stage = makeString("tap-node: "_s, node->nodeName()).utf8();
            view.log(stage.data(), WKPageLogFalseW10M);
        }
        view.log("tap-link-url-length", result.absoluteLinkURL().string().length());
        view.log("tap-node-name-length", node ? static_cast<unsigned long>(node->nodeName().length()) : 0);
        view.log("tap-is-anchor", anchor != nullptr);
        view.log("tap-href-length", anchor ? static_cast<unsigned long>(anchor->href().string().length()) : 0);
        view.log("tap-stage-after-hit-test", type);
    }
    if (type == WKPointerEventPressW10M)
        view.frame().eventHandler().handleMousePressEvent(event);
    else if (type == WKPointerEventMoveW10M)
        view.frame().eventHandler().mouseMoved(event);
    else
        view.frame().eventHandler().handleMouseReleaseEvent(event);
    if (view.log)
        view.log("tap-stage-after-dispatch", type);
    view.dirty = true;
    return WKPageResultOkW10M;
}

// Advances this page's rendering pipeline; shared service is independently scheduled.
extern "C" __declspec(dllexport) unsigned __stdcall WKTickPageW10M(void* handle)
{
    if (!handle)
        return WKPageResultInvalidArgumentW10M;
    if (!host(handle)->validThread())
        return WKPageResultWrongThreadW10M;
    if (host(handle)->compositorActive && !host(handle)->compositor->makeCurrent())
        return WKPageResultCompositorUnavailableW10M;
    host(handle)->page->updateRendering();
    host(handle)->page->finalizeRenderingUpdate({});
    return WKPageResultOkW10M;
}

// Installs the owning UI dispatcher so CurlRequest can wake WebCore's main run loop.
extern "C" __declspec(dllexport) long __stdcall WKInstallClipboardDispatcherW10M(void* handle, void* dispatcher)
{
    if (!handle || !dispatcher)
        return E_INVALIDARG;
    auto& view = *host(handle);
    if (!view.validThread())
        return RPC_E_WRONG_THREAD;
    namespace Core = ABI::Windows::UI::Core;
    Microsoft::WRL::ComPtr<Core::ICoreDispatcher> ui;
    HRESULT result = static_cast<IInspectable*>(dispatcher)->QueryInterface(IID_PPV_ARGS(&ui));
    if (FAILED(result))
        return result;
    boolean hasAccess = false;
    if (FAILED(result = ui->get_HasThreadAccess(&hasAccess)))
        return result;
    // This standalone control owns WebCore on the XAML UI thread. Reject a
    // different apartment rather than installing an invalid host dispatcher.
    if (!hasAccess)
        return RPC_E_WRONG_THREAD;
    // CurlRequest dispatches through WTF's main RunLoop. Wake it through the
    // owning UI dispatcher, independently of frame rendering or page dirtiness.
    // All views share the one engine owner dispatcher rather than replacing its wake hook.
    static bool wakeInstalled { false };
    if (!wakeInstalled) {
        auto wakePending = std::make_shared<std::atomic<bool>>(false);
        WTF::RunLoop::setWakeUpCallback([ui, wakePending] {
            if (wakePending->exchange(true))
                return;
            auto callback = Microsoft::WRL::Callback<Core::IDispatchedHandler>([wakePending]() -> HRESULT {
                wakePending->store(false);
                engineServiceW10M();
                return S_OK;
            });
            Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncAction> action;
            if (!callback || FAILED(ui->RunAsync(Core::CoreDispatcherPriority_Normal, callback.Get(), &action)))
                wakePending->store(false);
        });
        wakeInstalled = true;
    }
    ClipboardW10M::setUIDispatcher([ui](Function<void()>&& task) -> bool {
        boolean hasAccess = false;
        if (FAILED(ui->get_HasThreadAccess(&hasAccess)))
            return false;
        if (hasAccess) {
            task();
            return true;
        }
        auto pending = std::make_shared<Function<void()>>(WTF::move(task));
        auto callback = Microsoft::WRL::Callback<Core::IDispatchedHandler>([pending]() -> HRESULT {
            (*pending)();
            return S_OK;
        });
        if (!callback)
            return false;
        Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncAction> action;
        return SUCCEEDED(ui->RunAsync(Core::CoreDispatcherPriority_Normal, callback.Get(), &action));
    });
    if (view.log)
        view.log("clipboard-ui-dispatcher-installed", S_OK);
    return S_OK;
}

// Paints the current frame at identity device scale into caller-owned BGRA memory.
extern "C" __declspec(dllexport) unsigned __stdcall WKPaintPageW10M(void* handle, unsigned char* bgra, size_t capacity, unsigned stride)
{
    // Identity scale: the unscaled entry point paints 1:1.
    return WKPaintPageScaledW10M(handle, bgra, capacity, stride, 1, 1);
}

// Paints the current frame at the requested device scale into caller-owned BGRA memory.
extern "C" __declspec(dllexport) unsigned __stdcall WKPaintPageScaledW10M(void* handle, unsigned char* bgra, size_t capacity, unsigned stride, double scaleX, double scaleY)
{
    if (!handle || !bgra)
        return WKPageResultInvalidArgumentW10M;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadW10M;
    constexpr double kMaxDeviceScale = static_cast<double>(W10MMaxDeviceScale);
    if (!std::isfinite(scaleX) || !std::isfinite(scaleY) || scaleX <= 0 || scaleY <= 0 || scaleX > kMaxDeviceScale || scaleY > kMaxDeviceScale)
        return WKPageResultInvalidArgumentW10M;
    unsigned pixelWidth = static_cast<unsigned>(std::ceil(view.width * scaleX));
    unsigned pixelHeight = static_cast<unsigned>(std::ceil(view.height * scaleY));
    if (pixelWidth > W10MMaxPaintDimension || pixelHeight > W10MMaxPaintDimension || stride < pixelWidth * W10MBytesPerBGRA || stride > W10MMaxPaintStride || capacity < static_cast<size_t>(stride) * pixelHeight)
        return WKPageResultInvalidArgumentW10M;
    if (view.page->deviceScaleFactor() != static_cast<float>(scaleX))
        view.page->setDeviceScaleFactor(scaleX);
    view.frame().view()->updateLayoutAndStyleIfNeededRecursive();
#if USE(CAIRO)
    auto* surface = cairo_image_surface_create_for_data(bgra, CAIRO_FORMAT_ARGB32, pixelWidth, pixelHeight, stride);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return WKPageResultNoPaintSurfaceW10M;
    }
#elif USE(SKIA)
    auto surface = SkSurfaces::WrapPixels(SkImageInfo::Make(pixelWidth, pixelHeight, kBGRA_8888_SkColorType, kPremul_SkAlphaType), bgra, stride);
    if (!surface)
        return WKPageResultNoPaintSurfaceW10M;
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
    return WKPageResultOkW10M;
}

// Enables the page compositor using the supplied native SwapChainPanel.
extern "C" __declspec(dllexport) unsigned __stdcall WKEnableCompositorW10M(void* handle, void* nativePanel, double scaleX, double scaleY)
{
    constexpr double kMaxDeviceScale = static_cast<double>(W10MMaxDeviceScale);
    if (!handle || !nativePanel || !std::isfinite(scaleX) || !std::isfinite(scaleY) || scaleX <= 0 || scaleX > kMaxDeviceScale || std::abs(scaleX - scaleY) > kScaleUniformityToleranceW10M)
        return WKPageResultInvalidArgumentW10M;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadW10M;
    if (view.compositorActive && view.compositorScale == scaleX)
        return WKPageResultOkW10M;
    if (view.compositor)
        return WKPageResultCompositorConfigurationUnsupportedW10M; // Context scale changes need a complete graphics-layer rebuild.
    auto compositor = std::make_unique<TextureMapperCompositorW10M>(view.frame(), view.log);
    if (!compositor->initialize(nativePanel, view.width, view.height, scaleX))
        return WKPageResultCompositorConfigurationUnsupportedW10M;
    view.compositor = WTF::move(compositor);
    view.compositorScale = scaleX;
    view.compositorActive = true;
    view.page->setDeviceScaleFactor(scaleX);
    view.page->settings().setAcceleratedCompositingEnabled(true);
    view.page->settings().setForceCompositingMode(true);
    view.page->settings().setAcceleratedCompositingForFixedPositionEnabled(true);
    view.frame().view()->updateLayoutAndStyleIfNeededRecursive();
    view.page->updateRendering();
    view.page->finalizeRenderingUpdate({});
    view.dirty = true;
    if (view.log)
        view.log("gpu-compositor-enabled", WKPageLogFalseW10M);
    return WKPageResultOkW10M;
}

// Disables and releases the page compositor.
extern "C" __declspec(dllexport) unsigned __stdcall WKDisableCompositorW10M(void* handle)
{
    if (!handle)
        return WKPageResultInvalidArgumentW10M;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadW10M;
    if (view.compositor)
        view.compositor->makeCurrent();
    view.page->settings().setForceCompositingMode(false);
    view.page->settings().setAcceleratedCompositingEnabled(false);
    view.frame().view()->updateLayoutAndStyleIfNeededRecursive();
    if (view.compositor)
        view.compositor->setRootLayer(nullptr);
    view.compositorActive = false;
    view.dirty = true;
    return WKPageResultOkW10M;
}

// Paints and presents one compositor frame with optional overscroll.
extern "C" __declspec(dllexport) unsigned __stdcall WKRenderCompositorW10M(void* handle, double overscrollY)
{
    if (!handle || !std::isfinite(overscrollY))
        return WKPageResultInvalidArgumentW10M;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadW10M;
    if (!view.compositorActive || !view.compositor)
        return WKPageResultCompositorConfigurationUnsupportedW10M;
    auto before = view.compositor->presentedFrameCount();
    if (!view.compositor->render(overscrollY))
        return WKPageResultCompositorUnavailableW10M;
    // A deferred surface synchronization did not acknowledge this dirty frame.
    view.dirty = before == view.compositor->presentedFrameCount();
    return WKPageResultOkW10M;
}

// Reports whether engine painting is needed before the next present.
extern "C" __declspec(dllexport) unsigned __stdcall WKPageNeedsPaintW10M(void* handle, unsigned* needsPaint)
{
    if (!handle || !needsPaint)
        return WKPageResultInvalidArgumentW10M;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadW10M;
    view.frame().view()->updateLayoutAndStyleIfNeededRecursive();
    *needsPaint = view.dirty || (view.compositorActive && view.compositor->needsFrame());
    return WKPageResultOkW10M;
}

// Copies viewport, document, scroll and history state into the caller struct.
extern "C" __declspec(dllexport) unsigned __stdcall WKGetPageStateW10M(void* handle, WKPageStateW10M* state)
{
    if (!handle || !state)
        return WKPageResultInvalidArgumentW10M;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadW10M;
    auto& frameView = *view.frame().view();
    auto position = frameView.scrollPosition();
    auto contents = frameView.contentsSize();
    auto& backForward = view.page->backForward();
    *state = { view.width, view.height, position.x(), position.y(), contents.width(), contents.height(), backForward.backCount() > 0, backForward.forwardCount() > 0 };
    return WKPageResultOkW10M;
}

// Copies the typed document facts captured by the loader client. The returned
// title pointer borrows page-owned storage that stays valid until the next
// commit on this engine thread; callers that outlive the call must copy it.
extern "C" __declspec(dllexport) unsigned __stdcall WKGetPageDocumentStateW10M(void* handle, WKPageDocumentStateW10M* state)
{
    if (!handle || !state)
        return WKPageResultInvalidArgumentW10M;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadW10M;
    state->title = view.navigation.titleUtf8();
    state->httpStatus = view.navigation.httpStatus();
    state->hasHttpStatus = view.navigation.hasHttpStatus();
    state->documentChange = view.navigation.change();
    state->redirectHops = view.navigation.redirectHops();
    return WKPageResultOkW10M;
}

