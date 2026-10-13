/*
 * WKW10MPageForwardW10M.cpp - first production page-table slice for the
 * history-neutral HostHtml path (M10 task 11.0).
 */
#include "config.h"
#include "WKW10MPageForwardW10M.h"

#include "BackForwardController.h"
#include "EngineW10M.h"
#include "PageW10M.h"
#include "PageHostW10M.h"
#include "TransientPageW10M.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <windows.ui.xaml.media.dxinterop.h>

namespace WebCore {

namespace {

// Bound page registry capacity; module page ownership is tracked by the consumer too.
constexpr uint32_t kMaximumEnginePages = 64;

// Fixed process page registry; handles are monotonic and never expose pointers.
struct PageRecord {
    WKW10MPageHandle id { 0 };
    PageHostW10M* host { nullptr };
    // Retained native visual belongs to this attachment until detach/close succeeds.
    ISwapChainPanelNative* compositorPanel { nullptr };
    // Geometry must be installed before attachment, and scale changes require detach.
    double scaleX { 1 }, scaleY { 1 };
    // Candidate evidence records a real first swap once per attachment.
    bool hasPresented { false };
};

PageRecord s_pages[kMaximumEnginePages];
uint64_t s_nextPageId { 1 };
DWORD s_engineOwner { 0 };
DWORD s_fontConfigurationError { 0 };
DWORD s_freeTypeFaceError { 0 };
DWORD s_freeTypeFaceOsError { 0 };
wchar_t s_fontDirectory[MAX_PATH] { };

#if defined(WKW10M_HOSTHTML_CANDIDATE)
wchar_t s_initializationTracePath[MAX_PATH] { };

// The app's TempState is writable before the engine is initialized. Preserve
// GetLastError so writing diagnostics cannot change the error being measured.
void writeInitializationTrace(const char* stage, unsigned long result)
{
    auto lastError = GetLastError();
    if (s_initializationTracePath[0]) {
        auto file = CreateFile2(s_initializationTracePath, FILE_APPEND_DATA, FILE_SHARE_READ, OPEN_ALWAYS, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            auto line = std::string(stage) + " result=" + std::to_string(result);
            char hex[32];
            std::snprintf(hex, sizeof(hex), " (0x%08lX)\r\n", result);
            line += hex;
            DWORD written = 0;
            WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
            CloseHandle(file);
        }
    }
    SetLastError(lastError);
}

// Start a fresh trace for this initialization attempt rather than mixing runs.
void beginInitializationTrace()
{
    auto lastError = GetLastError();
    wchar_t temporaryDirectory[MAX_PATH];
    auto length = GetTempPathW(ARRAYSIZE(temporaryDirectory), temporaryDirectory);
    constexpr wchar_t filename[] = L"hosthtml-engine.txt";
    if (length && length + ARRAYSIZE(filename) <= ARRAYSIZE(s_initializationTracePath)) {
        memcpy(s_initializationTracePath, temporaryDirectory, length * sizeof(wchar_t));
        memcpy(s_initializationTracePath + length, filename, sizeof(filename));
        auto file = CreateFile2(s_initializationTracePath, GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, nullptr);
        if (file != INVALID_HANDLE_VALUE)
            CloseHandle(file);
    }
    SetLastError(lastError);
}
#endif

// Bias distinguishing a FreeType face error from a Win32 error in the preserved font failure.
constexpr DWORD kFontFaceErrorBiasW10M = 0x4000;

// Preserve a concrete fontconfig-stage failure for the initialization result.
// Stage names must match EngineW10M.cpp report() sites; keep both sides in sync.
void __stdcall recordEngineInitializationStage(const char* stage, unsigned long result)
{
#if defined(WKW10M_HOSTHTML_CANDIDATE)
    writeInitializationTrace(stage, result);
#endif
    if (s_fontConfigurationError)
        return;
    if (!strcmp(stage, "page-font-freetype-open")) {
        s_freeTypeFaceError = result;
        return;
    }
    if (!strcmp(stage, "page-font-freetype-open-os-error")) {
        s_freeTypeFaceOsError = result;
        return;
    }

    // Native open and FreeType face-open reports are probes, not initialization
    // gates. Preserve only the Fontconfig results that engineEnsureInitializedW10M
    // actually checks before returning FontsFailed.
    if (result)
        return;
    if (!strcmp(stage, "page-font-regular-added")) {
        if (s_freeTypeFaceOsError)
            s_fontConfigurationError = s_freeTypeFaceOsError;
        else if (s_freeTypeFaceError)
            s_fontConfigurationError = kFontFaceErrorBiasW10M + s_freeTypeFaceError;
        else
            s_fontConfigurationError = ERROR_FILE_NOT_FOUND;
    } else if (!strcmp(stage, "page-font-bold-added"))
        s_fontConfigurationError = ERROR_BAD_FORMAT;
    else if (!strcmp(stage, "page-font-config-built"))
        s_fontConfigurationError = ERROR_INVALID_DATA;
    else if (!strcmp(stage, "page-font-config-current"))
        s_fontConfigurationError = ERROR_NOT_READY;
}

// Find one currently open negotiated page without allocating or taking ownership.
PageRecord* findPage(WKW10MPageHandle id)
{
    for (auto& page : s_pages) {
        if (page.id == id)
            return &page;
    }
    return nullptr;
}

// Translate the stable legacy page result subset into the frozen V1 result values.
WKW10MResult pageResult(unsigned result)
{
    if (result <= WKPageResultCompositorUnavailableW10M)
        return static_cast<WKW10MResult>(result);
    return WKW10MResultUnsupported;
}

// Map process-initialization results by their own enum, not the legacy page-result values.
WKW10MResult engineInitializationResult(unsigned result)
{
    switch (result) {
    case FirstPageResultOkW10M: return WKW10MResultOK;
    case FirstPageResultInvalidArgumentW10M: return WKW10MResultInvalidArgument;
    case FirstPageResultNoFontConfigW10M: return WKW10MResultNotInitialized;
    case FirstPageResultFontsFailedW10M: return WKW10MResultUnsupported;
    case FirstPageResultNoFrameW10M: return WKW10MResultNoMainFrame;
    case FirstPageResultNoLoaderW10M: return WKW10MResultNoActiveDocumentLoader;
    case FirstPageResultNoSurfaceW10M: return WKW10MResultNoPaintSurface;
    case FirstPageResultDocumentBeginFailedW10M: return WKW10MResultDocumentWriterBeginFailed;
    case FirstPageResultWrongThreadW10M: return WKW10MResultWrongThread;
    case FirstPageResultConfigurationConflictW10M: return WKW10MResultConfigurationConflict;
    default: return WKW10MResultUnsupported;
    }
}

// Borrow UTF-8 bytes from a temporary CString only for the enclosing callback.
WKW10MString span(const CString& value)
{
    return { reinterpret_cast<const uint8_t*>(value.data()), static_cast<uint32_t>(value.length()) };
}

// Fill common page/event snapshots; event-specific fields remain zero until assigned.
WKW10MPageEvent eventFor(PageHostW10M& host, uint32_t kind, uint32_t contentKind,
    uint64_t navigationId, const CString& uri, const CString& title)
{
    WKW10MPageEvent event { };
    event.header = { sizeof(event), WKW10M_STRUCTURE_VERSION };
    event.pageId = host.enginePageId;
    event.callbackSequence = ++host.callbackSequence;
    event.navigationId = navigationId;
    event.documentGeneration = host.navigation.documentGeneration();
    event.eventKind = kind;
    event.contentKind = contentKind;
    // Substitute responses are local content, even when WebCore supplies a
    // response object. The public HostHtml contract carries no HTTP status.
    if (contentKind == WKW10MContentRemote) {
        event.httpStatus = host.navigation.httpStatus();
        event.hasHttpStatus = host.navigation.hasHttpStatus();
    }
    event.canGoBack = host.page && host.page->backForward().backCount() > 0;
    event.canGoForward = host.page && host.page->backForward().forwardCount() > 0;
    event.isLoading = host.engineIsLoading;
    event.uri = span(uri);
    event.title = span(title);
    return event;
}

// Send a copied-value notification through the currently retained callback table.
// A throwing subscriber must not unwind through WebCore; report and drop instead.
void notify(PageHostW10M& host, const WKW10MPageEvent& event)
{
    if (!host.engineCallbacksAttached || !host.engineCallbacks.notify)
        return;
    try {
        host.engineCallbacks.notify(host.engineCallbacks.context, &event);
    } catch (...) {
#if defined(WKW10M_HOSTHTML_CANDIDATE)
        writeInitializationTrace("engine-notify-threw", 0);
#endif
    }
}

} // namespace

// A HostHtml proposal is synchronously visible to the public control before WebCore starts loading it.
WKW10MResult startEngineNavigationW10M(PageHostW10M& host, uint64_t navigationId,
    uint32_t contentKind, const char* target, uint32_t* cancel)
{
    if (!cancel || !target || !navigationId)
        return WKW10MResultInvalidArgument;
    *cancel = 0;
    if (!host.engineCallbacksAttached || !host.engineCallbacks.starting)
        return WKW10MResultOK;
    if (host.callbackSequence == std::numeric_limits<uint64_t>::max())
        return WKW10MResultInvalidState;
    host.engineIsLoading = true;
    CString targetString(target);
    CString empty;
    auto event = eventFor(host, WKW10MEventStarting, contentKind, navigationId, targetString, empty);
    try {
        return host.engineCallbacks.starting(host.engineCallbacks.context, &event, cancel);
    } catch (...) {
#if defined(WKW10M_HOSTHTML_CANDIDATE)
        writeInitializationTrace("engine-starting-threw", 0);
#endif
        return WKW10MResultInvalidState;
    }
}

// Publish the committed document URI after the engine navigation controller updates its generation.
void notifyEngineSourceChangedW10M(PageHostW10M& host, uint32_t contentKind)
{
    if (!host.engineCallbacksAttached || host.callbackSequence == std::numeric_limits<uint64_t>::max())
        return;
    auto uri = host.navigation.source().utf8();
    auto title = host.navigation.title().utf8();
    auto event = eventFor(host, WKW10MEventSourceChanged, contentKind,
        host.navigation.lastCommitId(), uri, title);
    notify(host, event);
}

// Publish a terminal with the current page snapshot; only the loader's terminal paths call this.
void notifyEngineNavigationCompletedW10M(PageHostW10M& host, uint32_t contentKind,
    uint32_t outcome, uint32_t errorDomain, int32_t errorCode)
{
    if (!host.engineCallbacksAttached || host.callbackSequence == std::numeric_limits<uint64_t>::max())
        return;
    auto navigationId = host.navigation.activeNavigationId();
    if (!navigationId || host.engineTerminalId == navigationId)
        return;
    host.engineIsLoading = false;
    auto uri = host.navigation.source().utf8();
    auto title = host.navigation.title().utf8();
    auto event = eventFor(host, WKW10MEventCompleted, contentKind,
        navigationId, uri, title);
    event.outcome = outcome;
    event.errorDomain = errorDomain;
    event.errorCode = errorCode;
    host.engineTerminalId = navigationId;
    notify(host, event);
}

// Drop callback delivery before releasing the provider-retained consumer context exactly once.
void releaseEngineCallbacksW10M(PageHostW10M& host)
{
    if (!host.engineCallbacksAttached)
        return;
    auto callbacks = host.engineCallbacks;
    host.engineCallbacks = { };
    host.engineCallbacksAttached = false;
    if (callbacks.releaseContext)
        callbacks.releaseContext(callbacks.context);
}

// Initialize process fonts for the explicit package directory before any page exists.
WKW10MResult initializeEnginePagesW10M(const WKW10MEngineOptions* options)
{
    if (!options || options->header.structureSize < sizeof(*options)
        || options->header.structureVersion != WKW10M_STRUCTURE_VERSION
        || !options->fontDirectory.data || !options->fontDirectory.length
        || options->fontDirectory.length >= MAX_PATH
        || (options->fontFingerprint.length && !options->fontFingerprint.data))
        return WKW10MResultInvalidArgument;
    // Host services are negotiated separately; this first page slice has no clipboard requests.
    if (options->platformServices)
        return WKW10MResultUnsupported;
    auto thread = GetCurrentThreadId();
    if (s_engineOwner && s_engineOwner != thread)
        return WKW10MResultWrongThread;
#if defined(WKW10M_HOSTHTML_CANDIDATE)
    beginInitializationTrace();
#endif
    std::wstring directory;
    directory.reserve(options->fontDirectory.length);
    auto* path = reinterpret_cast<const wchar_t*>(options->fontDirectory.data);
    for (uint32_t index = 0; index < options->fontDirectory.length; ++index)
        directory.push_back(path[index]);
    // The consumer canonicalizes paths case-insensitively; restore the installed
    // package's spelling for font libraries that use their own path parser.
    wchar_t actualPath[MAX_PATH];
    auto actualPathLength = GetLongPathNameW(directory.c_str(), actualPath, ARRAYSIZE(actualPath));
    if (actualPathLength && actualPathLength < ARRAYSIZE(actualPath))
        directory.assign(actualPath, actualPathLength);
    String fontDirectory(directory.c_str());
    // The component contract serializes lowercase ASCII while WTF's hex helper
    // emits uppercase; compare the same digest identity independent of casing.
    auto actualFingerprint = fontFingerprintForDirectory(fontDirectory).convertToASCIILowercase().utf8();
    if (actualFingerprint.length() != options->fontFingerprint.length
        || memcmp(actualFingerprint.data(), options->fontFingerprint.data, actualFingerprint.length()))
        return WKW10MResultConfigurationConflict;
    s_fontConfigurationError = 0;
    s_freeTypeFaceError = 0;
    s_freeTypeFaceOsError = 0;
    auto result = engineEnsureInitializedW10M(directory.c_str(), &recordEngineInitializationStage);
#if defined(WKW10M_HOSTHTML_CANDIDATE)
    writeInitializationTrace("engine-initialize-result", result);
#endif
    if (result == FirstPageResultFontsFailedW10M) {
        // Biased FreeType errors are not Win32 errors; keep them out of HRESULT_FROM_WIN32.
        if (!s_fontConfigurationError)
            return static_cast<WKW10MResult>(HRESULT_FROM_WIN32(ERROR_INVALID_DATA));
        if (s_fontConfigurationError >= kFontFaceErrorBiasW10M)
            return WKW10MResultUnsupported;
        return static_cast<WKW10MResult>(HRESULT_FROM_WIN32(s_fontConfigurationError));
    }
    if (result != WKPageResultOkW10M)
        return engineInitializationResult(result);
    memcpy(s_fontDirectory, directory.data(), directory.size() * sizeof(wchar_t));
    s_fontDirectory[directory.size()] = L'\0';
    s_engineOwner = thread;
    return WKW10MResultOK;
}

// Create one owner-thread engine page and assign a never-reused frozen handle.
WKW10MResult createEnginePageW10M(const WKW10MPageOptions* options, WKW10MPageHandle* output)
{
    if (GetCurrentThreadId() != s_engineOwner)
        return WKW10MResultWrongThread;
    if (!options || !output || options->header.structureSize < sizeof(*options)
        || options->header.structureVersion != WKW10M_STRUCTURE_VERSION || !s_fontDirectory[0])
        return WKW10MResultInvalidArgument;
    auto slot = std::find_if(std::begin(s_pages), std::end(s_pages), [](const PageRecord& page) { return !page.host; });
    if (slot == std::end(s_pages) || s_nextPageId == std::numeric_limits<uint64_t>::max())
        return WKW10MResultInvalidState;
    unsigned result = WKPageResultInvalidArgumentW10M;
    WKPageLogW10M pageLog = nullptr;
#if defined(WKW10M_HOSTHTML_CANDIDATE)
    pageLog = &recordEngineInitializationStage;
#endif
    auto* host = static_cast<PageHostW10M*>(WKCreatePageW10M(s_fontDirectory, 480, 800, pageLog, &result));
    if (!host)
        return pageResult(result);
    auto id = s_nextPageId++;
    host->enginePageId = id;
    slot->id = id;
    slot->host = host;
    *output = id;
    return WKW10MResultOK;
}

// Close the retained page, deactivating callback delivery before context release.
WKW10MResult closeEnginePageW10M(WKW10MPageHandle id)
{
    if (GetCurrentThreadId() != s_engineOwner)
        return WKW10MResultWrongThread;
    auto* slot = findPage(id);
    if (!slot || !slot->host)
        return WKW10MResultInvalidArgument;
    auto detached = detachEngineCompositorW10M(id);
    if (detached != WKW10MResultOK)
        return detached;
    auto* page = slot->host;
    *slot = { };
    return pageResult(WKDestroyPageW10M(page));
}

// Retain a validated page callback table exactly once for this attachment.
WKW10MResult attachEngineCallbacksW10M(WKW10MPageHandle id, const WKW10MPageCallbacks* callbacks)
{
    if (GetCurrentThreadId() != s_engineOwner)
        return WKW10MResultWrongThread;
    auto* slot = findPage(id);
    if (!slot || !slot->host || !callbacks)
        return WKW10MResultInvalidArgument;
    if (callbacks->header.structureSize < sizeof(*callbacks)
        || callbacks->header.structureVersion != WKW10M_STRUCTURE_VERSION
        || (!!callbacks->retainContext != !!callbacks->releaseContext)
        || (callbacks->context && !callbacks->retainContext))
        return WKW10MResultInvalidArgument;
    if (slot->host->engineCallbacksAttached)
        return WKW10MResultInvalidState;
    if (callbacks->retainContext)
        callbacks->retainContext(callbacks->context);
    slot->host->engineCallbacks = *callbacks;
    slot->host->engineCallbacksAttached = true;
    return WKW10MResultOK;
}

// Deactivate and release one attachment without closing the page.
WKW10MResult detachEngineCallbacksW10M(WKW10MPageHandle id)
{
    if (GetCurrentThreadId() != s_engineOwner)
        return WKW10MResultWrongThread;
    auto* slot = findPage(id);
    if (!slot || !slot->host)
        return WKW10MResultInvalidArgument;
    releaseEngineCallbacksW10M(*slot->host);
    return WKW10MResultOK;
}

// Load explicit UTF-8 HostHtml via the engine's history-hidden about:blank substitute.
WKW10MResult loadEngineHtmlW10M(WKW10MPageHandle id, const WKW10MNavigationRequest* request,
    uint64_t* navigationId)
{
    if (GetCurrentThreadId() != s_engineOwner)
        return WKW10MResultWrongThread;
    auto* slot = findPage(id);
    if (!slot || !slot->host || !request || !navigationId
        || request->header.structureSize < sizeof(*request)
        || request->header.structureVersion != WKW10M_STRUCTURE_VERSION
        || request->contentKind != WKW10MContentHostHtml || request->uri.length || request->uri.data
        || (request->html.length && !request->html.data))
        return WKW10MResultInvalidArgument;
    if (request->warningToken.issuer || request->warningToken.decision)
        return WKW10MResultUnsupported;
    auto html = String::fromUTF8(std::span<const uint8_t>(request->html.data, request->html.length));
    slot->host->engineContentKind = WKW10MContentHostHtml;
    slot->host->transient.show(TransientContentKindW10M::GenericHtml, html);
    *navigationId = slot->host->transient.transientNavigationId();
    return *navigationId ? WKW10MResultOK : WKW10MResultInvalidState;
}

// Validate the frozen geometry before any page mutation or panel allocation.
WKW10MResult resizeEnginePageW10M(WKW10MPageHandle id, uint32_t width, uint32_t height,
    double scaleX, double scaleY)
{
    if (GetCurrentThreadId() != s_engineOwner)
        return WKW10MResultWrongThread;
    constexpr double maxDeviceScale = static_cast<double>(W10MMaxDeviceScale);
    constexpr double maxPaintDimension = static_cast<double>(W10MMaxPaintDimension);
    auto* slot = findPage(id);
    if (!slot || !width || !height || width > W10MMaxPageDimension || height > W10MMaxPageDimension
        || !std::isfinite(scaleX) || !std::isfinite(scaleY) || scaleX <= 0 || scaleY <= 0
        || scaleX > maxDeviceScale || scaleY > maxDeviceScale
        || std::ceil(width * scaleX) > maxPaintDimension || std::ceil(height * scaleY) > maxPaintDimension
        || std::abs(scaleX - scaleY) > 0.01)
        return WKW10MResultInvalidArgument;
    if (slot->compositorPanel && (scaleX != slot->scaleX || scaleY != slot->scaleY))
        return WKW10MResultInvalidState;
    auto result = pageResult(WKResizePageW10M(slot->host, width, height));
    if (result == WKW10MResultOK) {
        slot->scaleX = scaleX;
        slot->scaleY = scaleY;
        slot->host->page->setDeviceScaleFactor(scaleX);
        if (slot->host->log) {
            slot->host->log("presentation-css-width", width);
            slot->host->log("presentation-css-height", height);
            slot->host->log("presentation-scale-milli", static_cast<unsigned long>(scaleX * 1000));
        }
    }
    return result;
}

// Retain the exact native panel and give the engine compositor its borrowed Inspectable.
WKW10MResult attachEngineCompositorW10M(WKW10MPageHandle id, const WKW10MCompositorOptions* options)
{
    if (GetCurrentThreadId() != s_engineOwner)
        return WKW10MResultWrongThread;
    auto* slot = findPage(id);
    if (!slot || !options || options->header.structureVersion != WKW10M_STRUCTURE_VERSION
        || options->header.structureSize < sizeof(*options) || !options->panelInspectable
        || !std::isfinite(options->scaleX) || !std::isfinite(options->scaleY)
        || options->scaleX != slot->scaleX || options->scaleY != slot->scaleY)
        return WKW10MResultInvalidArgument;
    if (slot->compositorPanel || slot->host->compositor)
        return WKW10MResultInvalidState;
    ISwapChainPanelNative* panel = nullptr;
    auto queried = static_cast<IInspectable*>(options->panelInspectable)->QueryInterface(IID_PPV_ARGS(&panel));
    if (FAILED(queried))
        return static_cast<WKW10MResult>(queried);
    auto result = pageResult(WKEnableCompositorW10M(slot->host, options->panelInspectable, options->scaleX, options->scaleY));
    if (result != WKW10MResultOK) {
        panel->SetSwapChain(nullptr);
        panel->Release();
        return result;
    }
    slot->compositorPanel = panel;
    slot->hasPresented = false;
    return WKW10MResultOK;
}

// A successful detach releases the actual swap chain, graphics context and visual reference.
WKW10MResult detachEngineCompositorW10M(WKW10MPageHandle id)
{
    if (GetCurrentThreadId() != s_engineOwner)
        return WKW10MResultWrongThread;
    auto* slot = findPage(id);
    if (!slot)
        return WKW10MResultInvalidArgument;
    if (!slot->compositorPanel)
        return WKW10MResultOK;
    auto detached = slot->compositorPanel->SetSwapChain(nullptr);
    if (FAILED(detached))
        return static_cast<WKW10MResult>(detached);
    auto result = pageResult(WKDisableCompositorW10M(slot->host));
    if (result != WKW10MResultOK)
        return result;
    slot->host->compositor.reset();
    slot->compositorPanel->Release();
    slot->compositorPanel = nullptr;
    slot->hasPresented = false;
    if (slot->host->log)
        slot->host->log("presentation-detached", 0);
    return WKW10MResultOK;
}

// Service is supplied by the owner driver; render only when the engine needs a new frame.
WKW10MResult renderEngineCompositorW10M(WKW10MPageHandle id, double overscrollY)
{
    if (GetCurrentThreadId() != s_engineOwner)
        return WKW10MResultWrongThread;
    auto* slot = findPage(id);
    if (!slot || !std::isfinite(overscrollY))
        return WKW10MResultInvalidArgument;
    if (!slot->compositorPanel)
        return WKW10MResultInvalidState;
    // Match the proven legacy frame order: page rendering update/finalization
    // precedes dirty inspection and TextureMapper paint/present.
    auto result = pageResult(WKTickPageW10M(slot->host));
    if (result != WKW10MResultOK)
        return result;
    unsigned needsPaint = 0;
    result = pageResult(WKPageNeedsPaintW10M(slot->host, &needsPaint));
    if (result != WKW10MResultOK)
        return result;
    if (!needsPaint)
        return WKW10MResultPending;
    auto before = slot->host->compositor->presentedFrameCount();
    result = pageResult(WKRenderCompositorW10M(slot->host, overscrollY));
    if (result == WKW10MResultOK && before == slot->host->compositor->presentedFrameCount())
        return WKW10MResultPending;
    if (result == WKW10MResultOK && !slot->hasPresented) {
        slot->hasPresented = true;
        if (slot->host->log)
            slot->host->log("presentation-first-frame", 0);
    }
    return result;
}

} // namespace WebCore
