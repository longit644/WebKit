// Reusable process engine startup and shared service pump. Moved unchanged
// from FirstPageUWP.cpp (M10 task 4.1) so the smoke snapshot path and the
// persistent page host share one initialization and one pump; see EngineUWP.h.
#include "config.h"
#include "EngineUWP.h"

#include "LoaderStrategyUWP.h"
#include "MediaStrategy.h"
#include "PageUWP.h"
#include "PlatformStrategies.h"
#include "WebCoreMainThread.h"
#include <fontconfig/fontconfig.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <wtf/RunLoop.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/WTFString.h>

namespace WebCore {

// FreeType face selected from the packaged fonts.
constexpr FT_Long kRegularFontFaceIndex = 0;

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
    static DWORD engineThread;
    static bool initialized;
    if (engineThread && engineThread != GetCurrentThreadId())
        return FirstPageResultWrongThreadUWP;
    if (initialized)
        return FirstPageResultOkUWP;
    engineThread = GetCurrentThreadId();
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
    String directory(fontDirectory);
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
    initialized = true;
    return FirstPageResultOkUWP;
}

// Runs timers, transport pump and deferred work once on the owner thread.
void engineServiceUWP()
{
    WTF::RunLoop::cycle();
}

} // namespace WebCore
