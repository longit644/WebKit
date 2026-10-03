// Synchronous first-page bring-up harness. Ordinary browser navigation and
// event delivery will be owned by WebKitWebViewUWP's persistent page host.
#include "config.h"
#include "Document.h"
#include "DocumentView.h"
#include "DocumentLoader.h"
#include "DocumentWriter.h"
#include "EmptyClients.h"
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
#include "LoaderStrategy.h"
#include "MediaStrategy.h"
#include "Page.h"
#include "PageConfiguration.h"
#include "PlatformStrategies.h"
#include "ResourceError.h"
#include "ResourceRequest.h"
#include "SubresourceLoader.h"
#include "Settings.h"
#include "WebCoreMainThread.h"
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <wtf/text/MakeString.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

// This snapshot harness has no network requests. Completion callbacks report
// unsupported requests; browser navigation requires a real loader strategy.
class FirstPageLoaderStrategy final : public LoaderStrategy {
public:
    void loadResource(LocalFrame&, CachedResource&, ResourceRequest&&, const ResourceLoaderOptions&, CompletionHandler<void(RefPtr<SubresourceLoader>&&)>&& completion) final { completion(nullptr); }
    void loadResourceSynchronously(FrameLoader&, ResourceLoaderIdentifier, const ResourceRequest& request, ClientCredentialPolicy, const FetchOptions&, const HTTPHeaderMap&, ResourceError& error, ResourceResponse&, Vector<uint8_t>&) final { error = blockedError(request); }
    void pageLoadCompleted(Page&) final { }
    void browsingContextRemoved(LocalFrame&) final { }
    void remove(ResourceLoader*) final { }
    void setDefersLoading(ResourceLoader&, bool) final { }
    void crossOriginRedirectReceived(ResourceLoader*, const URL&) final { }
    void servePendingRequests(ResourceLoadPriority) final { }
    void suspendPendingRequests() final { }
    void resumePendingRequests() final { }
    void startPingLoad(LocalFrame&, ResourceRequest& request, const HTTPHeaderMap&, const FetchOptions&, ContentSecurityPolicyImposition, PingLoadCompletionHandler&& completion) final { if (completion) completion(blockedError(request), { }); }
    void preconnectTo(FrameLoader&, ResourceRequest&& request, StoredCredentialsPolicy, ShouldPreconnectAsFirstParty, PreconnectCompletionHandler&& completion) final { completion(blockedError(request)); }
    void setCaptureExtraNetworkLoadMetricsEnabled(bool) final { }
    bool isOnLine() const final { return false; }
    void addOnlineStateChangeListener(Function<void(bool)>&&) final { }
    void isResourceLoadFinished(CachedResource&, CompletionHandler<void(bool)>&& completion) final { completion(true); }
    ResourceError cancelledError(const ResourceRequest& request) const final { return error(request.url(), 1); }
    ResourceError blockedError(const ResourceRequest& request) const final { return error(request.url(), 2); }
    bool isBlockedError(const ResourceError& value) const final { return value.domain() == "UWPFirstPage"_s && value.errorCode() == 2; }
    ResourceError blockedByContentBlockerError(const ResourceRequest& request) const final { return error(request.url(), 3); }
    ResourceError cannotShowURLError(const ResourceRequest& request) const final { return error(request.url(), 4); }
    ResourceError interruptedForPolicyChangeError(const ResourceRequest& request) const final { return error(request.url(), 5); }
    ResourceError cannotShowMIMETypeError(const ResourceResponse& response) const final { return error(response.url(), 6); }
    ResourceError fileDoesNotExistError(const ResourceResponse& response) const final { return error(response.url(), 7); }
    ResourceError httpsUpgradeRedirectLoopError(const ResourceRequest& request) const final { return error(request.url(), 8); }
    ResourceError httpNavigationWithHTTPSOnlyError(const ResourceRequest& request) const final { return error(request.url(), 9); }
    bool isHttpNavigationWithHTTPSOnlyError(const ResourceError& value) const final { return value.domain() == "UWPFirstPage"_s && value.errorCode() == 9; }
    ResourceError pluginWillHandleLoadError(const ResourceResponse& response) const final { return error(response.url(), 10); }
private:
    static ResourceError error(const URL& url, int code) { return { "UWPFirstPage"_s, code, url, "External resource loading is unavailable in the first-page snapshot harness"_s }; }
};

class FirstPageMediaStrategy final : public MediaStrategy { };

class FirstPagePlatformStrategies final : public PlatformStrategies {
    LoaderStrategy* createLoaderStrategy() final { return new FirstPageLoaderStrategy; }
    PasteboardStrategy* createPasteboardStrategy() final { return nullptr; }
    MediaStrategy* createMediaStrategy() final { return new FirstPageMediaStrategy; }
    BlobRegistry* createBlobRegistry() final { return nullptr; }
};
}

extern "C" __declspec(dllexport) unsigned __stdcall WKInitializeEngineUWP(
    const wchar_t* fontDirectory,
    void (__stdcall *log)(const char*, unsigned long))
{
    using namespace WebCore;
    if (!fontDirectory)
        return 1;
    static DWORD engineThread;
    static bool initialized;
    if (engineThread && engineThread != GetCurrentThreadId())
        return 8;
    if (initialized)
        return 0;
    engineThread = GetCurrentThreadId();
    auto report = [log](const char* stage, unsigned long result = 0) {
        if (log)
            log(stage, result);
    };
    report("page-initialize-start");
    initializeMainThreadIfNeeded();
    report("page-main-thread-ready");

    FcConfig* fonts = FcConfigCreate();
    if (!fonts)
        return 2;
    String directory(fontDirectory);
    auto regular = makeString(directory, "/DejaVuSans.ttf"_s).utf8();
    auto bold = makeString(directory, "/DejaVuSans-Bold.ttf"_s).utf8();
    auto nativePath = makeString(directory, "\\DejaVuSans.ttf"_s).wideCharacters();
    HANDLE fontFile = CreateFile2(nativePath.span().data(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, nullptr);
    report("page-font-native-open", fontFile == INVALID_HANDLE_VALUE ? GetLastError() : 0);
    if (fontFile != INVALID_HANDLE_VALUE)
        CloseHandle(fontFile);
    FT_Library freeType;
    auto ftResult = FT_Init_FreeType(&freeType);
    report("page-font-freetype-init", ftResult);
    if (!ftResult) {
        FT_Face face;
        ftResult = FT_New_Face(freeType, regular.data(), 0, &face);
        report("page-font-freetype-open", ftResult);
        if (!ftResult)
            FT_Done_Face(face);
        FT_Done_FreeType(freeType);
    }
    auto regularAdded = FcConfigAppFontAddFile(fonts, reinterpret_cast<const FcChar8*>(regular.data()));
    report("page-font-regular-added", regularAdded);
    auto boldAdded = FcConfigAppFontAddFile(fonts, reinterpret_cast<const FcChar8*>(bold.data()));
    report("page-font-bold-added", boldAdded);
    auto built = FcConfigBuildFonts(fonts);
    report("page-font-config-built", built);
    auto current = FcConfigSetCurrent(fonts);
    report("page-font-config-current", current);
    if (!regularAdded || !boldAdded || !built || !current) {
        FcConfigDestroy(fonts);
        return 3;
    }
    FcConfigDestroy(fonts); // FcConfigSetCurrent holds its own reference.
    report("page-bundled-fonts-ready");

    static FirstPagePlatformStrategies strategies;
    if (!platformStrategies())
        setPlatformStrategies(&strategies);
    report("page-platform-strategies-ready");
    initialized = true;
    return 0;
}

extern "C" __declspec(dllexport) unsigned __stdcall WKRenderFirstPageUWP(
    const wchar_t* fontDirectory, unsigned char* pixels, unsigned width, unsigned height,
    void (__stdcall *log)(const char*, unsigned long))
{
    using namespace WebCore;
    if (!pixels || !width || !height || width > 2048 || height > 2048)
        return 1;
    auto initialization = WKInitializeEngineUWP(fontDirectory, log);
    if (initialization)
        return initialization;
    auto report = [log](const char* stage, unsigned long result = 0) { if (log) log(stage, result); };

    auto configuration = pageConfigurationWithEmptyClients(std::nullopt, PAL::SessionID::defaultSessionID());
    report("page-configuration-ready");
    auto page = Page::create(WTF::move(configuration));
    report("page-created");
    page->settings().setScriptEnabled(false);
    page->settings().setAcceleratedCompositingEnabled(false);
    page->settings().setShouldAllowUserInstalledFonts(true);
    page->settings().setDefaultFontSize(18);
    auto* frame = page->localMainFrame();
    if (!frame)
        return 4;
    frame->setView(LocalFrameView::create(*frame, { static_cast<int>(width), static_cast<int>(height) }));
    frame->init();
    frame->view()->setCanHaveScrollbars(false);
    report("page-frame-ready");
    auto* loader = frame->loader().activeDocumentLoader();
    if (!loader)
        return 5;
    auto& writer = loader->writer();
    writer.setMIMEType("text/html"_s);
    if (!writer.begin(URL { })) {
        report("page-document-begin-failed", 7);
        return 7;
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
    auto* surface = cairo_image_surface_create_for_data(pixels, CAIRO_FORMAT_ARGB32, width, height, width * 4);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return 6;
    }
#elif USE(SKIA)
    auto surface = SkSurfaces::WrapPixels(SkImageInfo::Make(width, height, kBGRA_8888_SkColorType, kPremul_SkAlphaType), pixels, width * 4);
    if (!surface) return 6;
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
    return 0;
}
