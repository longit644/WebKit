#pragma once

#include "PageW10M.h"

// Reusable process engine startup and shared service pump, extracted from
// FirstPageW10M.cpp and PageW10M.cpp so the smoke snapshot path and the
// persistent page host share one initialization and one pump (M10 task 4.1).
// Task 4.1 moved the startup/result/log contract here; task 6.2 adds validated
// process service and explicit suspension while separating page rendering.
// Smoke-only content (the first-page snapshot
// render and its harness strategies) stays in FirstPageW10M.cpp until its
// diagnostics host replacement passes in task 8.5. The historical
// FirstPageResultW10M names are kept verbatim; renames belong to section 10.
namespace WebCore {

// Snapshot-harness result codes; each value names the stage that failed. Moved
// unchanged from FirstPageW10M.cpp, which keeps using them for its snapshot
// render and for engine startup. Values 2 and 3 intentionally occupy the gaps
// the page-result family reserves, so the two families never collide.
enum FirstPageResultW10M {
    // Success; the snapshot rendered.
    FirstPageResultOkW10M = 0,

    // Null directory, pixels or log sink, or oversized snapshot.
    FirstPageResultInvalidArgumentW10M = 1,

    // Fontconfig configuration could not be created.
    FirstPageResultNoFontConfigW10M = 2,

    // Bundled fonts failed to install into fontconfig.
    FirstPageResultFontsFailedW10M = 3,

    // No local frame existed to render.
    FirstPageResultNoFrameW10M = 4,

    // No document loader was active for the frame.
    FirstPageResultNoLoaderW10M = 5,

    // Cairo/Skia surface creation failed.
    FirstPageResultNoSurfaceW10M = 6,

    // Document writing could not begin.
    FirstPageResultDocumentBeginFailedW10M = 7,

    // Called on a thread other than the engine thread.
    FirstPageResultWrongThreadW10M = 8,

    // A later font directory or fingerprint conflicts with the frozen process
    // configuration; the existing pages keep working unchanged.
    FirstPageResultConfigurationConflictW10M = 9
};

// Initializes WebCore's main thread, bundled fonts and W10M platform strategies
// once per process. The first successful setup freezes the font directory and
// its content fingerprint; a later directory or content conflict reports the
// conflict code before any page creation instead of silently changing live
// pages. A second call with matching configuration reports success without
// repeating any stage, and any thread but the first reports the wrong-thread
// code. Called by the WKInitializeEngineW10M export and, in engine, by page
// creation.
unsigned engineEnsureInitializedW10M(const wchar_t* fontDirectory, WKPageLogW10M log);

// Runs timers, transport pump and deferred work once on the owner thread.
// Called by negotiated process service and the owner-dispatcher wake; page
// presentation ticks do not cycle the process loop. Pre-init, suspended,
// recursive and foreign-thread calls perform no work or readiness transition.
void engineServiceW10M();

} // namespace WebCore

