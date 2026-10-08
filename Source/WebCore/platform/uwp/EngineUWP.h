#pragma once

#include "PageUWP.h"

// Reusable process engine startup and shared service pump, extracted from
// FirstPageUWP.cpp and PageUWP.cpp so the smoke snapshot path and the
// persistent page host share one initialization and one pump (M10 task 4.1).
// Behavior is preserved exactly: the statics, result codes, log stages and
// call order below moved here unchanged, and both former call sites funnel
// into these two functions. Smoke-only content (the first-page snapshot
// render and its harness strategies) stays in FirstPageUWP.cpp until its
// diagnostics host replacement passes in task 8.5. The historical
// FirstPageResultUWP names are kept verbatim; renames belong to section 10.
namespace WebCore {

// Snapshot-harness result codes; each value names the stage that failed. Moved
// unchanged from FirstPageUWP.cpp, which keeps using them for its snapshot
// render and for engine startup. Values 2 and 3 intentionally occupy the gaps
// the page-result family reserves, so the two families never collide.
enum FirstPageResultUWP {
    // Success; the snapshot rendered.
    FirstPageResultOkUWP = 0,

    // Null directory, pixels or log sink, or oversized snapshot.
    FirstPageResultInvalidArgumentUWP = 1,

    // Fontconfig configuration could not be created.
    FirstPageResultNoFontConfigUWP = 2,

    // Bundled fonts failed to install into fontconfig.
    FirstPageResultFontsFailedUWP = 3,

    // No local frame existed to render.
    FirstPageResultNoFrameUWP = 4,

    // No document loader was active for the frame.
    FirstPageResultNoLoaderUWP = 5,

    // Cairo/Skia surface creation failed.
    FirstPageResultNoSurfaceUWP = 6,

    // Document writing could not begin.
    FirstPageResultDocumentBeginFailedUWP = 7,

    // Called on a thread other than the engine thread.
    FirstPageResultWrongThreadUWP = 8,

    // A later font directory or fingerprint conflicts with the frozen process
    // configuration; the existing pages keep working unchanged.
    FirstPageResultConfigurationConflictUWP = 9
};

// Initializes WebCore's main thread, bundled fonts and UWP platform strategies
// once per process. The first successful setup freezes the font directory and
// its content fingerprint; a later directory or content conflict reports the
// conflict code before any page creation instead of silently changing live
// pages. A second call with matching configuration reports success without
// repeating any stage, and any thread but the first reports the wrong-thread
// code. Called by the WKInitializeEngineUWP export and, in engine, by page
// creation.
unsigned engineEnsureInitializedUWP(const wchar_t* fontDirectory, WKPageLogUWP log);

// Runs timers, transport pump and deferred work once on the owner thread.
// Called by the validated per-page tick and the owner-dispatcher wake callback;
// compositor/rendering work stays per-page. Shared scheduling is task 6.2.
void engineServiceUWP();

} // namespace WebCore
