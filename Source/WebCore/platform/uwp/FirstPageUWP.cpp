// Synchronous first-page bring-up harness. Ordinary browser navigation and
// event delivery will be owned by WebKitWebViewUWP's persistent page host.
#include "config.h"
#include "Document.h"
#include "DocumentLoader.h"
#include "DocumentView.h"
#include "DocumentWriter.h"
#include "EmptyClients.h"
#include "EngineUWP.h"
#include "FrameLoader.h"
#if USE(CAIRO)
#include "GraphicsContextCairo.h"
#elif USE(SKIA)
#include "GraphicsContextSkia.h"
#include <skia/core/SkSurface.h>
#endif
#include "LoaderStrategy.h"
#include "LoaderStrategyUWP.h"
#include "LocalFrame.h"
#include "LocalFrameInlines.h"
#include "LocalFrameView.h"
#include "Page.h"
#include "PageConfiguration.h"
#include "PageUWP.h" // shared result codes and validation limits
#include "ResourceError.h"
#include "ResourceRequest.h"
#include "Settings.h"
#include "SubresourceLoader.h"
#include <wtf/text/MakeString.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

// This snapshot harness has no network requests. Completion callbacks report
// unsupported requests; browser navigation requires a real loader strategy.
class FirstPageLoaderStrategy final : public LoaderStrategy {
public:
    // Snapshot harness loads nothing; completes with no loader.
    void loadResource(LocalFrame&, CachedResource&, ResourceRequest&&, const ResourceLoaderOptions&, CompletionHandler<void(RefPtr<SubresourceLoader>&&)>&& completion) final { completion(nullptr); }

    // Blocking loads are refused; the harness paints one local snapshot.
    void loadResourceSynchronously(FrameLoader&, ResourceLoaderIdentifier, const ResourceRequest& request, ClientCredentialPolicy, const FetchOptions&, const HTTPHeaderMap&, ResourceError& error, ResourceResponse&, Vector<uint8_t>&) final { error = blockedError(request); }

    // No-op: the snapshot needs no page-load bookkeeping.
    void pageLoadCompleted(Page&) final { }

    // No-op: no browsing contexts outlive the snapshot.
    void browsingContextRemoved(LocalFrame&) final { }

    // No-op: nothing is tracked, so nothing needs removal.
    void remove(ResourceLoader*) final { }

    // No-op: nothing defers in the snapshot harness.
    void setDefersLoading(ResourceLoader&, bool) final { }

    // No-op: redirects never occur without loads.
    void crossOriginRedirectReceived(ResourceLoader*, const URL&) final { }

    // No-op: requests start immediately, nothing stays pending.
    void servePendingRequests(ResourceLoadPriority) final { }

    // No-op: the snapshot runs no pump to suspend.
    void suspendPendingRequests() final { }

    // No-op: the snapshot runs no pump to resume.
    void resumePendingRequests() final { }

    // Ping loads are refused; beacons report blocked.
    void startPingLoad(LocalFrame&, ResourceRequest& request, const HTTPHeaderMap&, const FetchOptions&, ContentSecurityPolicyImposition, PingLoadCompletionHandler&& completion) final
    {
        if (completion)
            completion(blockedError(request), {});
    }

    // Preconnect completes immediately; ordinary requests still load.
    void preconnectTo(FrameLoader&, ResourceRequest&& request, StoredCredentialsPolicy, ShouldPreconnectAsFirstParty, PreconnectCompletionHandler&& completion) final { completion(blockedError(request)); }

    // No-op: extra metrics capture stays off.
    void setCaptureExtraNetworkLoadMetricsEnabled(bool) final { }

    // Offline by construction; the harness paints bundled content only.
    bool isOnLine() const final { return false; }

    // No-op: online transitions are never delivered.
    void addOnlineStateChangeListener(Function<void(bool)>&&) final { }

    // Everything is trivially finished without a loader.
    void isResourceLoadFinished(CachedResource&, CompletionHandler<void(bool)>&& completion) final { completion(true); }

    // Superseded-load error for the cancelled code.
    ResourceError cancelledError(const ResourceRequest& request) const final { return error(request.url(), UWPResourceErrorCancelled); }

    // Harness refusal error for the blocked code.
    ResourceError blockedError(const ResourceRequest& request) const final { return error(request.url(), UWPResourceErrorBlocked); }

    // True for harness-domain blocked errors only.
    bool isBlockedError(const ResourceError& value) const final { return value.domain() == "UWPFirstPage"_s && value.errorCode() == UWPResourceErrorBlocked; }

    // Content-blocker refusal mapped onto the blocker code.
    ResourceError blockedByContentBlockerError(const ResourceRequest& request) const final { return error(request.url(), UWPResourceErrorBlockedByContentBlocker); }

    // Unsupported-scheme error for the unsupported-URL code.
    ResourceError cannotShowURLError(const ResourceRequest& request) const final { return error(request.url(), UWPResourceErrorUnsupportedURL); }

    // Mid-flight policy change mapped onto the policy code.
    ResourceError interruptedForPolicyChangeError(const ResourceRequest& request) const final { return error(request.url(), UWPResourceErrorInterruptedForPolicyChange); }

    // Undisplayable MIME error for the MIME code.
    ResourceError cannotShowMIMETypeError(const ResourceResponse& response) const final { return error(response.url(), UWPResourceErrorUnsupportedMIMEType); }

    // Missing local file error for the file code.
    ResourceError fileDoesNotExistError(const ResourceResponse& response) const final { return error(response.url(), UWPResourceErrorFileDoesNotExist); }

    // HTTPS upgrade loop mapped onto the redirect code.
    ResourceError httpsUpgradeRedirectLoopError(const ResourceRequest& request) const final { return error(request.url(), UWPResourceErrorRedirectFailed); }

    // HTTPS-only refusal error for the HTTPS-only code.
    ResourceError httpNavigationWithHTTPSOnlyError(const ResourceRequest& request) const final { return error(request.url(), UWPResourceErrorHTTPSOnly); }

    // True for harness-domain HTTPS-only errors only.
    bool isHttpNavigationWithHTTPSOnlyError(const ResourceError& value) const final { return value.domain() == "UWPFirstPage"_s && value.errorCode() == UWPResourceErrorHTTPSOnly; }

    // Plugin loads are unsupported on this port.
    ResourceError pluginWillHandleLoadError(const ResourceResponse& response) const final { return error(response.url(), UWPResourceErrorPluginWillHandleLoad); }

private:
    // Builds a harness-domain error sharing the adapter's code values.
    static ResourceError error(const URL& url, int code) { return { "UWPFirstPage"_s, code, url, "External resource loading is unavailable in the first-page snapshot harness"_s }; }
};

// Shared engine startup and its strategies now live in EngineUWP.h/.cpp, so
// both the smoke snapshot path and the persistent page host initialize once
// through the same implementation. The harness loader strategy above stays
// here as smoke-only content until its diagnostics host replacement passes.

} // namespace WebCore

// Initializes WebCore's main thread, bundled fonts and UWP platform strategies once.
extern "C" __declspec(dllexport) unsigned __stdcall WKInitializeEngineUWP(
    const wchar_t* fontDirectory,
    void(__stdcall* log)(const char*, unsigned long))
{
    // Single shared startup; see EngineUWP.h for the moved implementation.
    return WebCore::engineEnsureInitializedUWP(fontDirectory, log);
}

// Renders one snapshot frame into the caller pixels; result is a FirstPageResultUWP code.
extern "C" __declspec(dllexport) unsigned __stdcall WKRenderFirstPageUWP(
    const wchar_t* fontDirectory, unsigned char* pixels, unsigned width, unsigned height,
    void(__stdcall* log)(const char*, unsigned long))
{
    using namespace WebCore;
    if (!pixels || !width || !height || width > UWPMaxPageDimension || height > UWPMaxPageDimension)
        return FirstPageResultInvalidArgumentUWP;
    auto initialization = WKInitializeEngineUWP(fontDirectory, log);
    if (initialization != FirstPageResultOkUWP)
        return initialization;
    // Emits a stage with a success value unless an API supplies an error code.
    auto report = [log](const char* stage, unsigned long result = WKPageLogFalseUWP) { if (log) log(stage, result); };

    auto configuration = pageConfigurationWithEmptyClients(std::nullopt, PAL::SessionID::defaultSessionID());
    report("page-configuration-ready");
    auto page = Page::create(WTF::move(configuration));
    report("page-created");
    page->settings().setScriptEnabled(false);
    page->settings().setAcceleratedCompositingEnabled(false);
    page->settings().setShouldAllowUserInstalledFonts(true);
    page->settings().setDefaultFontSize(UWPDefaultFontSize);
    // See the equivalent call in PageUWP.cpp: the WebCore default for
    // loadsImagesAutomatically is false, which silently suppresses <img> loads.
    page->settings().setLoadsImagesAutomatically(true);
    page->settings().setImagesEnabled(true);
    auto* frame = page->localMainFrame();
    if (!frame)
        return FirstPageResultNoFrameUWP;
    frame->setView(LocalFrameView::create(*frame, { static_cast<int>(width), static_cast<int>(height) }));
    frame->init();
    frame->view()->setCanHaveScrollbars(false);
    report("page-frame-ready");
    auto* loader = frame->loader().activeDocumentLoader();
    if (!loader)
        return FirstPageResultNoLoaderUWP;
    auto& writer = loader->writer();
    writer.setMIMEType("text/html"_s);
    if (!writer.begin(URL {})) {
        report("page-document-begin-failed", FirstPageResultDocumentBeginFailedUWP);
        return FirstPageResultDocumentBeginFailedUWP;
    }
    writer.insertDataSynchronously(
        "<!doctype html><html><head><meta charset='utf-8'><style>"
        "html{background:#edf3fa;color:#14243a;font-family:'DejaVu Sans'}"
        "body{margin:28px}h1{font-size:32px;color:#1766a8}p{font-size:20px;line-height:1.5}"
        ".card{background:white;padding:24px;border:3px solid #1766a8;border-radius:18px}"
        ".bar{height:48px;background:linear-gradient(90deg,#1766a8,#20b878);border-radius:12px}"
        "svg{display:block;margin:24px auto}footer{font-size:15px;color:#52677c;margin-top:24px}"
        "</style></head><body><h1>WebKit on Lumia</h1><div class='card'>"
        "<p>This page was parsed, laid out and painted by native WebCore.</p>"
        "<div class='bar'></div><svg width='240' height='160' viewBox='0 0 240 160'>"
        "<rect width='240' height='160' rx='16' fill='#edf3fa'/>"
        "<circle cx='70' cy='80' r='42' fill='#1766a8'/>"
        "<path d='M120 122 L170 36 L220 122 Z' fill='#20b878'/></svg>"
        "<p>Bundled FreeType fonts + Cairo painting + D3D11 presentation.</p>"
        "</div><footer>ARM32 / Windows 10 Mobile / Lumia 950 XL</footer></body></html>"_s);
    writer.end();
    report("page-html-parsed");
    frame->view()->updateLayoutAndStyleIfNeededRecursive();
    frame->document()->updateLayoutIgnorePendingStylesheets();
    report("page-layout-complete");

#if USE(CAIRO)
    auto* surface = cairo_image_surface_create_for_data(pixels, CAIRO_FORMAT_ARGB32, width, height, width * UWPBytesPerBGRA);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return FirstPageResultNoSurfaceUWP;
    }
#elif USE(SKIA)
    auto surface = SkSurfaces::WrapPixels(SkImageInfo::Make(width, height, kBGRA_8888_SkColorType, kPremul_SkAlphaType), pixels, width * UWPBytesPerBGRA);
    if (!surface)
        return FirstPageResultNoSurfaceUWP;
#endif
    {
#if USE(CAIRO)
        GraphicsContextCairo context(surface);
#elif USE(SKIA)
        GraphicsContextSkia context(*surface->getCanvas(), RenderingMode::Unaccelerated, RenderingPurpose::Unspecified);
#endif
        frame->view()->paintContents(context, { 0, 0, static_cast<int>(width), static_cast<int>(height) });
    }
#if USE(CAIRO)
    cairo_surface_flush(surface);
    cairo_surface_destroy(surface);
#endif
    report("page-paint-complete");
    return FirstPageResultOkUWP;
}
