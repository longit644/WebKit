// Reusable process engine startup and shared service pump. Moved unchanged
// from FirstPageUWP.cpp (M10 task 4.1) so the smoke snapshot path and the
// persistent page host share one initialization and one pump; see EngineUWP.h.
#include "config.h"
#include "EngineUWP.h"

#include "LoaderStrategyUWP.h"
#include "MediaStrategy.h"
#include "PageUWP.h"
#include "PlatformStrategies.h"
#include "TlsPolicyUWP.h"
#include "WebCoreMainThread.h"
#include "WKUWPEngine.h"
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <pal/crypto/CryptoDigest.h>
#include <wtf/FileSystem.h>
#include <wtf/RunLoop.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/WTFString.h>

#include <algorithm>
#include <atomic>

namespace WebCore {
namespace {

// Process engine owner, safely observable by pre-initialization service callers.
std::atomic<DWORD> engineThread { 0 };
// Publish successful setup before a negotiated service call may touch the run loop.
std::atomic<bool> engineInitialized { false };
// Process app suspension, distinct from temporary visual unload.
bool engineSuspended { false };
// Prevent recursive generic RunLoop cycles during owner callbacks.
bool engineServicing { false };

} // namespace

// FreeType face selected from the packaged fonts.
constexpr FT_Long kRegularFontFaceIndex = 0;

// Lowercase hex SHA-256 over canonically sorted font file names and contents
// in a directory; empty when nothing readable backs the configuration.
String fontFingerprintForDirectory(const String& directory)
{
    auto names = FileSystem::listDirectory(directory);
    std::sort(names.begin(), names.end(), [](const String& a, const String& b) {
        return codePointCompare(a, b) < 0;
    });
    auto digest = PAL::Crypto::CryptoDigest::create(PAL::Crypto::CryptoDigest::Algorithm::SHA_256);
    if (!digest)
        return String();
    bool anyFont = false;
    for (auto& name : names) {
        String lower = name.convertToASCIILowercase();
        if (!lower.endsWith(".ttf"_s) && !lower.endsWith(".otf"_s) && !lower.endsWith(".ttc"_s) && !lower.endsWith(".otc"_s))
            continue;
        auto bytes = FileSystem::readEntireFile(makeString(directory, "/"_s, name));
        if (!bytes || bytes->isEmpty())
            return String();
        auto utf8 = name.utf8();
        digest->addBytes({ reinterpret_cast<const uint8_t*>(utf8.data()), utf8.length() });
        digest->addBytes({ bytes->begin(), bytes->size() });
        anyFont = true;
    }
    if (!anyFont)
        return String();
    return digest->toHexString();
}

// Empty media strategy; the snapshot paints no audio or video.
class FirstPageMediaStrategy final : public MediaStrategy { };

// Snapshot platform strategies: real UWP loader, no pasteboard or blob registry.
class FirstPagePlatformStrategies final : public PlatformStrategies {

    // Binds the snapshot to the real UWP loader adapter.
    LoaderStrategy* createLoaderStrategy() final { return new LoaderStrategyUWP; }

    // Copy/paste has no backing store in the snapshot harness.
    PasteboardStrategy* createPasteboardStrategy() final { return nullptr; }

    // Binds the empty media strategy above.
    MediaStrategy* createMediaStrategy() final { return new FirstPageMediaStrategy; }

    // Blob URLs resolve to nothing in the snapshot harness.
    BlobRegistry* createBlobRegistry() final { return nullptr; }
};

// Initializes WebCore's main thread, bundled fonts and UWP platform strategies once.
unsigned engineEnsureInitializedUWP(const wchar_t* fontDirectory, WKPageLogUWP log)
{
    if (!fontDirectory)
        return FirstPageResultInvalidArgumentUWP;
    auto owner = engineThread.load();
    auto thread = GetCurrentThreadId();
    if (owner && owner != thread)
        return FirstPageResultWrongThreadUWP;
    if (!owner && !engineThread.compare_exchange_strong(owner, thread) && owner != thread)
        return FirstPageResultWrongThreadUWP;
    // Frozen process font configuration: the first successful setup wins, and
    // a conflicting directory or content fingerprint fails before any page
    // creation instead of silently changing live pages.
    static String frozenFontDirectory;
    static String frozenFontFingerprint;
    String directory(fontDirectory);
    String fingerprint = fontFingerprintForDirectory(directory);
    if (engineInitialized.load()) {
        if (directory != frozenFontDirectory || fingerprint != frozenFontFingerprint) {
            if (log)
                log("page-font-configuration-conflict", FirstPageResultConfigurationConflictUWP);
            return FirstPageResultConfigurationConflictUWP;
        }
        return FirstPageResultOkUWP;
    }
    // Emits a stage with a success value unless an API supplies an error code.
    auto report = [log](const char* stage, unsigned long result = WKPageLogFalseUWP) {
        if (log)
            log(stage, result);
    };
    report("page-initialize-start");
    setLoaderStrategyUWPLog(log);
    initializeMainThreadIfNeeded();
    report("page-main-thread-ready");

    FcConfig* fonts = FcConfigCreate();
    if (!fonts)
        return FirstPageResultNoFontConfigUWP;
    auto regular = makeString(directory, "/DejaVuSans.ttf"_s).utf8();
    auto bold = makeString(directory, "/DejaVuSans-Bold.ttf"_s).utf8();
    auto nativePath = makeString(directory, "\\DejaVuSans.ttf"_s).wideCharacters();
    HANDLE fontFile = CreateFile2(nativePath.span().data(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, nullptr);
    report("page-font-native-open", fontFile == INVALID_HANDLE_VALUE ? GetLastError() : WKPageLogFalseUWP);
    if (fontFile != INVALID_HANDLE_VALUE)
        CloseHandle(fontFile);
    FT_Library freeType;
    auto ftResult = FT_Init_FreeType(&freeType);
    report("page-font-freetype-init", ftResult);
    if (!ftResult) {
        FT_Face face;
        ftResult = FT_New_Face(freeType, regular.data(), kRegularFontFaceIndex, &face);
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
        return FirstPageResultFontsFailedUWP;
    }
    FcConfigDestroy(fonts); // FcConfigSetCurrent holds its own reference.
    report("page-bundled-fonts-ready");

    static FirstPagePlatformStrategies strategies;
    if (!platformStrategies())
        setPlatformStrategies(&strategies);
    report("page-platform-strategies-ready");
    // Freeze the configuration that just succeeded; later mismatches fail.
    frozenFontDirectory = directory;
    frozenFontFingerprint = fingerprint;
    engineInitialized.store(true);
    return FirstPageResultOkUWP;
}

// Runs timers, transport pump and deferred work once on the owner thread.
void engineServiceUWP()
{
    if (!engineInitialized.load() || GetCurrentThreadId() != engineThread.load() || engineSuspended || engineServicing)
        return;
    engineServicing = true;
    WTF::RunLoop::cycle();
    engineServicing = false;
}

// Real frozen service slot, linked only in the configured engine target.
extern "C" WKUWPResult __stdcall wkuwpEngineServiceUWP()
{
    if (!engineInitialized.load()) return WKUWPResultNotInitialized;
    if (GetCurrentThreadId() != engineThread.load()) return WKUWPResultWrongThread;
    engineServiceUWP();
    return WKUWPResultOK;
}

// Apply explicit app suspension once to process resource queue/deadline accounting.
extern "C" WKUWPResult __stdcall wkuwpEngineSetSuspendedUWP(uint32_t suspended)
{
    if (!engineInitialized.load()) return WKUWPResultNotInitialized;
    if (GetCurrentThreadId() != engineThread.load()) return WKUWPResultWrongThread;
    if (suspended > 1) return WKUWPResultInvalidArgument;
    if (engineSuspended == !!suspended) return WKUWPResultOK;
    if (suspended) platformStrategies()->loaderStrategy()->suspendPendingRequests();
    else platformStrategies()->loaderStrategy()->resumePendingRequests();
    engineSuspended = !!suspended;
    return WKUWPResultOK;
}

} // namespace WebCore
