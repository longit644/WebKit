// Frame loader client owning policy, navigation events, history delegation
// and the temporary fixture suite; all diagnostics funnel through report().
// Method implementations moved unchanged from PageUWP.cpp (M10 task 4.2);
// see FrameLoaderClientUWP.h. The fixture free functions below move with the
// client that drives them, staying file-local, because no export calls them.
#include "config.h"
#include "FrameLoaderClientUWP.h"

#include "BackForwardClientUWP.h"
#include "BackForwardController.h"
#include "CertificateExceptionStoreUWP.h"
#include "ChromeClientUWP.h"
#include "CookieJar.h"
#include "Document.h"
#include "DocumentLoader.h"
#include "DocumentView.h"
#include "EmptyClients.h"
#include "EngineUWP.h"
#include "FrameDestructionObserverInlines.h"
#include "FrameLoader.h"
#include "HTMLBodyElement.h"
#include "HTMLImageElement.h"
#include "HistoryController.h"
#include "HistoryItem.h"
#include "HitTestResult.h"
#include "LocalFrame.h"
#include "LocalFrameInlines.h"
#include "LocalFrameView.h"
#include "MIMETypeRegistry.h"
#include "PageConfiguration.h"
#include "PlatformStrategies.h"
#include "ResourceError.h"
#include "ResourcePolicyUWP.h"
#include "ResourceRequest.h"
#include "ResourceResponse.h"
#include "Settings.h"
#include "StorageSessionProviderUWP.h"
#include "StyleComputedStyle+GettersInlines.h"
#include "TlsPolicyUWP.h"
#include <wtf/WallTime.h>
#include <wtf/text/AtomString.h>
#include <wtf/text/MakeString.h>
#include <wtf/text/WTFString.h>

namespace WebCore {
namespace {

// Temporary fixture-suite helpers driven only by the client methods below.
// They stay out of every header: the suite is removed wholesale in task 9.7
// once the replacement runner reproduces its rows, and nothing outside this
// translation unit may depend on its probes meanwhile.

// The fixture markers must not be read at finishedLoading(). A blocking
// <script src> in <head> (and the stylesheet beside it) stall the HTML parser
// before <body> is constructed, so document->body() is still null when the
// main document's network transfer completes and every contains() probe
// reports a false zero. The DOM load event is the first point at which the
// body exists and is laid out.
// Returns body text when parsing/layout has created a document body.
String fixtureBodyText(LocalFrame& frame)
{
    auto* document = frame.document();
    if (!document || !document->body())
        return String();
    return document->body()->innerText();
}

// Logs DOM, network, cookie and font assertions for the current fixture phase.
void probeFixtureMarkers(PageHostUWP& owner, const char* phase)
{
    if (!owner.log || !owner.page)
        return;
    String text = fixtureBodyText(owner.frame());
    auto stage = makeString("fixture-probe-phase: "_s, String::fromUTF8(phase)).utf8();
    owner.log(stage.data(), text.length());
    owner.log("fixture-css-heading", text.contains("External CSS makes this green"_s) ? WKPageLogTrueUWP : WKPageLogFalseUWP);
    owner.log("fixture-script-order", text.contains("PASS external script order: first,second"_s) ? WKPageLogTrueUWP : WKPageLogFalseUWP);
    owner.log("fixture-fetch", text.contains("PASS fetch:"_s) ? WKPageLogTrueUWP : WKPageLogFalseUWP);
    owner.log("fixture-xhr", text.contains("PASS XHR:"_s) ? WKPageLogTrueUWP : WKPageLogFalseUWP);
    owner.log("fixture-cookie-pass", text.contains("PASS cookie"_s) ? WKPageLogTrueUWP : WKPageLogFalseUWP);
    owner.log("fixture-cookie-fail", text.contains("FAIL cookie"_s) ? WKPageLogTrueUWP : WKPageLogFalseUWP);
    auto* document = owner.frame().document();
    if (!document)
        return;
    auto selected = document->querySelector(".fixture-font"_s);
    if (selected.hasException())
        return;
    auto element = selected.releaseReturnValue();
    if (element && element->renderer()) {
        auto& font = element->renderer()->style().fontCascade().primaryFont();
        owner.log("fixture-web-font-custom", font.platformData().customPlatformData() ? WKPageLogTrueUWP : WKPageLogFalseUWP);
        owner.log("fixture-web-font-interstitial", font.isInterstitial() ? WKPageLogTrueUWP : WKPageLogFalseUWP);
        owner.log("fixture-web-font-glyph", font.glyphForCharacter('B') ? WKPageLogTrueUWP : WKPageLogFalseUWP);
    }
}

// Initial settle-probe phase before the delayed follow-up checks.
constexpr unsigned kInitialSettleProbeStep = 0;
constexpr unsigned kSettleProbeStepIncrement = 1;
constexpr unsigned kFixtureSettleProbeDelayCount = 3;
constexpr Seconds kFixtureSettleProbeDelays[] = { 300_ms, 1200_ms, 3000_ms };
static_assert(sizeof(kFixtureSettleProbeDelays) / sizeof(*kFixtureSettleProbeDelays) == kFixtureSettleProbeDelayCount);

// Re-probes fetch/XHR after load because their promise callbacks can settle later.
void scheduleSettleProbe(PageHostUWP* owner, unsigned generation, unsigned step)
{
    if (step >= kFixtureSettleProbeDelayCount)
        return;
    auto runLoop = &WTF::RunLoop::currentSingleton();
    std::weak_ptr<unsigned> lifetime = owner->probeLifetime;
    runLoop->dispatchAfter(kFixtureSettleProbeDelays[step], [owner, lifetime, generation, step] {
        if (!lifetime.lock())
            return;
        if (owner->navigation.loadGeneration() != generation || !owner->page)
            return;
        probeFixtureMarkers(*owner, "settle");
        scheduleSettleProbe(owner, generation, step + kSettleProbeStepIncrement);
    });
}

// Substring armed by the suite's veto probe; matching navigations are
// refused by the host veto. File-local: only the suite arms it, and it is
// disarmed before the suite continues.
const char* g_fixtureVetoSubstring = nullptr;

// Host-veto handler for the suite: refuses URLs containing the armed
// substring, allows everything else. __stdcall to match the port contract.
bool __stdcall fixtureVetoPolicy(void*, const char* utf8Url)
{
    if (!g_fixtureVetoSubstring || !utf8Url)
        return true;
    return !String::fromUTF8(utf8Url).contains(String::fromUTF8(g_fixtureVetoSubstring));
}

// Quiet log sink for the isolation peer: its transfers stay out of the host log.
void fixturePeerLog(const char*, unsigned long)
{
}

// Second control for the session-isolation probe, mirroring WKCreatePageUWP
// without engine re-initialization: its own ephemeral session, cookie jar and
// history, sharing only the process-wide loader strategy under test.
PageHostUWP* g_fixturePeer = nullptr;

// Second control for the concurrent-bundle probe; the first peer keeps
// bundle A while this one takes the unrelated bundle.
PageHostUWP* g_fixturePeerB = nullptr;

// Reads the peer or owner control's document cookies synchronously.
String fixtureCookieString(Page* page)
{
    auto* document = page && page->localMainFrame() ? page->localMainFrame()->document() : nullptr;
    if (!document)
        return String();
    auto result = document->cookie();
    return result.hasException() ? String() : result.releaseReturnValue();
}

// Packaged fixture CA bundle beside the host executable; the engine resolves
// trust bundles from the package, never from the network.
String fixtureBundlePath(const char* filename)
{
    wchar_t imagePath[MAX_PATH];
    DWORD length = GetModuleFileNameW(nullptr, imagePath, MAX_PATH);
    if (!length || length >= MAX_PATH)
        return String();
    String directory(imagePath, length);
    auto slash = directory.reverseFind('\\');
    if (slash == notFound)
        return String();
    return makeString(StringView(directory).left(slash + 1), String::fromUTF8(filename));
}

// Fixture HTTPS origin on the alternate fixture port, derived from the plain
// fixture base so both controls address the same server identity.
String fixtureTlsBase(const String& fixtureBase)
{
    URL base { fixtureBase };
    return makeString("https://"_s, base.host().toString(), ":8004/"_s);
}

// Creates the isolation peer and starts its plain document load.
PageHostUWP* createIsolationPeer(const String& fixtureBase)
{
    auto peer = std::make_unique<PageHostUWP>();
    peer->width = 100;
    peer->height = 100;
    peer->log = fixturePeerLog;
    auto sessionID = PAL::SessionID::generateEphemeralSessionID();
    auto configuration = pageConfigurationWithEmptyClients(std::nullopt, sessionID);
    configuration.backForwardClient = BackForwardClientUWP::create();
    auto storageProvider = adoptRef(*new StorageSessionProviderUWP(sessionID));
    peer->storageProvider = storageProvider.copyRef();
    configuration.cookieJar = CookieJar::create(WTF::move(storageProvider));
    configuration.chromeClient = makeUniqueRef<ChromeClientUWP>(*peer);
    auto& local = std::get<PageConfiguration::LocalMainFrameCreationParameters>(configuration.mainFrameCreationParameters);
    local.effectiveSandboxFlags = {};
    PageHostUWP* raw = peer.get();
    local.clientCreator = [raw](LocalFrame&, FrameLoader& loader) -> UniqueRef<LocalFrameLoaderClient> {
        return makeUniqueRefWithoutRefCountedCheck<FrameLoaderClientUWP>(loader, *raw);
    };
    auto page = Page::create(WTF::move(configuration));
    page->settings().setScriptEnabled(true);
    auto* frame = page->localMainFrame();
    if (!frame)
        return nullptr;
    frame->setView(LocalFrameView::create(*frame, { 100, 100 }));
    frame->init();
    peer->page = WTF::move(page);
    ResourceRequest request(URL { makeString(fixtureBase, "index.html"_s) });
    frame->loader().load(FrameLoadRequest(*frame, WTF::move(request)));
    return peer.release();
}

// Temporary device-acceptance phases, explicitly opted into by the fixture
// URL's uwp-suite=1 query. Numbers are stable log protocol: the device log
// names completed phases, so never renumber. Unscoped for switch labels.
enum FixtureSuitePhase {
    // Probes index.html markers, then follows the redirect fixture.
    FixtureSuitePhaseIndexProbes = 0,

    // Verifies the redirect landed without a query, then opens second.html.
    FixtureSuitePhaseRedirect = 1,

    // Goes back in engine history, or falls back to the missing page.
    FixtureSuitePhaseHistoryBack = 2,

    // Verifies the back landing, then goes forward again.
    FixtureSuitePhaseHistoryForward = 3,

    // Verifies the forward landing, then reloads the document.
    FixtureSuitePhaseHistoryReload = 4,

    // Back-failure fallback; shares the missing-page checks below.
    FixtureSuitePhaseHistoryBackFallback = 5,

    // Verifies latin-1 decoding, then opens the CSP fixture.
    FixtureSuitePhaseLatin1 = 6,

    // Verifies blocked script and image, then opens cross-origin.html.
    FixtureSuitePhaseContentSecurityPolicy = 7,

    // Verifies allowed and blocked CORS, then opens slow.html.
    FixtureSuitePhaseCrossOrigin = 8,

    // Arms a replacement navigation while slow.html is still loading.
    FixtureSuitePhaseReplacementPending = 9,

    // Waits for the replacement document to settle.
    FixtureSuitePhaseReplacementStabilize = 10,

    // Verifies the replacement won, then reloads slow.html for the stop test.
    FixtureSuitePhaseReplacementStable = 11,

    // Stops slow.html mid-load.
    FixtureSuitePhaseStopPending = 12,

    // Verifies the stopped document stayed put, then returns to index.
    FixtureSuitePhaseStopStable = 13,

    // Verifies the reloaded document, then navigates to a same-page fragment.
    FixtureSuitePhaseReloadDocument = 14,

    // Verifies same-document fragment nav scrolled, then goes back.
    FixtureSuitePhaseSameFragment = 15,

    // Verifies the back landing, then opens the missing page.
    FixtureSuitePhaseSameBackDocument = 16,

    // Verifies the 404 document and truncated forward history, then latin1.
    FixtureSuitePhaseHttpError = 17,

    // Records the scroll offset, then opens second.html.
    FixtureSuitePhaseScrollBefore = 18,

    // Goes back and checks scroll restoration on return.
    FixtureSuitePhaseScrollBack = 19,

    // Verifies the restored offset, then opens pushstate.html.
    FixtureSuitePhaseScrollRestored = 20,

    // Verifies pushState added an entry, then goes back.
    FixtureSuitePhasePushState = 21,

    // Verifies back dropped the pushed query, then goes forward.
    FixtureSuitePhasePushBack = 22,

    // Verifies forward restored the pushed query, then returns to index.
    FixtureSuitePhasePushForward = 23,

    // Returns to index before the replaceState sequence.
    FixtureSuitePhaseReplaceSetup = 24,

    // Verifies replaceState mutated without pushing, then goes back.
    FixtureSuitePhaseReplaceState = 25,

    // Verifies back skipped the replaced query, then goes forward.
    FixtureSuitePhaseReplaceBack = 26,

    // Verifies forward restored the replaced query, then starts the race.
    FixtureSuitePhaseReplaceForward = 27,

    // Verifies the final landing and reports suite completion.
    FixtureSuitePhaseDone = 28,

    // Starts a slow main-frame load and replaces it in the same turn.
    FixtureSuitePhaseRaceProbe = 29,

    // Verifies the race loser is Replaced and the winner committed.
    FixtureSuitePhaseRaceStable = 30,

    // Arms the host veto and issues a refused navigation.
    FixtureSuitePhaseVetoProbe = 31,

    // Waits for the veto terminal, then navigates the good document.
    FixtureSuitePhaseVetoConfirm = 32,

    // Verifies the vetoed ID stayed Vetoed past the good commit.
    FixtureSuitePhaseVetoStable = 33,

    // Issues a navigation into an infinite redirect loop.
    FixtureSuitePhaseLoopProbe = 34,

    // Verifies the loop failed provisionally with no commit.
    FixtureSuitePhaseLoopStable = 35,

    // Issues a navigation to an attachment download.
    FixtureSuitePhaseDownloadProbe = 36,

    // Verifies the download was vetoed with no commit.
    FixtureSuitePhaseDownloadStable = 37,

    // Re-verifies the race loser is still Replaced after later phases.
    FixtureSuitePhaseRaceDelayed = 38,

    // Fails another loop navigation to arm the recorded failure.
    FixtureSuitePhaseTransientFailProbe = 39,

    // Shows the warning interstitial over the recorded failure.
    FixtureSuitePhaseTransientBlankProbe = 40,

    // Verifies opaque origin, preserved failure and still history.
    FixtureSuitePhaseTransientBlankStable = 41,

    // Arms the veto and issues a refused transient show.
    FixtureSuitePhaseTransientVetoProbe = 42,

    // Verifies the vetoed show changed nothing.
    FixtureSuitePhaseTransientVetoStable = 43,

    // Shows generic host markup over the preserved failure.
    FixtureSuitePhaseTransientGenericProbe = 44,

    // Verifies the generic show invalidated the failure.
    FixtureSuitePhaseTransientGenericStable = 45,

    // Reloads the transient document.
    FixtureSuitePhaseTransientReloadProbe = 46,

    // Verifies reload kept history, cursor and the committed terminal.
    FixtureSuitePhaseTransientReloadStable = 47,

    // Creates the second control with its own ephemeral session.
    FixtureSuitePhaseIsolationCreate = 48,

    // Waits for the peer control's document to settle.
    FixtureSuitePhaseIsolationLoad = 49,

    // Sets a cookie on the first control; the peer must not see it.
    FixtureSuitePhaseIsolationSet = 50,

    // Sets a cookie on the peer; the first control must keep its own.
    FixtureSuitePhaseIsolationCross = 51,

    // Tears down the peer; its session must vanish with it.
    FixtureSuitePhaseIsolationTeardown = 52,

    // Fails the owner control for a certificate decision.
    FixtureSuitePhaseCertFailOwner = 53,

    // Creates the peer control for the cross-issued token probe.
    FixtureSuitePhaseCertPeerCreate = 54,

    // Waits for the peer control's document to settle.
    FixtureSuitePhaseCertPeerLoad = 55,

    // Fails the peer control at the same local navigation ordinal.
    FixtureSuitePhaseCertPeerFail = 56,

    // Verifies the cross-issued token is rejected without mutation.
    FixtureSuitePhaseCertCross = 57,

    // Fails the owner again so the first token goes stale.
    FixtureSuitePhaseCertStaleFail = 58,

    // Verifies the stale token is rejected.
    FixtureSuitePhaseCertStaleStable = 59,

    // Shows warning content over the live failure decision.
    FixtureSuitePhaseCertWarn = 60,

    // Verifies the warning preserved the decision's eligibility.
    FixtureSuitePhaseCertWarnStable = 61,

    // Shows generic content over the preserved decision.
    FixtureSuitePhaseCertGeneric = 62,

    // Verifies generic content invalidated the decision.
    FixtureSuitePhaseCertGenericStable = 63,

    // Consumes the peer token and probes scoped enforcement.
    FixtureSuitePhaseCertConsume = 64,

    // Fails the owner again for the remote-invalidation probe.
    FixtureSuitePhaseCertRefail = 65,

    // Verifies the following remote command invalidated the decision.
    FixtureSuitePhaseCertRemoteGone = 66,

    // Verifies the configured policy defaults and range validation.
    FixtureSuitePhaseBudgetDefaults = 67,

    // Fails a loop navigation against the redirect hop budget.
    FixtureSuitePhaseBudgetRedirects = 68,

    // Fails a main document against a tiny resource budget.
    FixtureSuitePhaseBudgetResource = 69,

    // Verifies the resource failure and restores the budget.
    FixtureSuitePhaseBudgetResourceStable = 70,

    // Fails a main document against a tiny transient budget.
    FixtureSuitePhaseBudgetTransient = 71,

    // Verifies the transient failure and restores the budget.
    FixtureSuitePhaseBudgetTransientStable = 72,

    // Loads a delayed document against a short deadline.
    FixtureSuitePhaseBudgetTimeout = 73,

    // Verifies the deadline failure and restores the deadline.
    FixtureSuitePhaseBudgetTimeoutStable = 74,

    // Suspends the strategy mid-transfer past the deadline.
    FixtureSuitePhaseBudgetSuspend = 75,

    // Resumes after the suspended span and waits for completion.
    FixtureSuitePhaseBudgetResume = 76,

    // Verifies suspension paused the deadline.
    FixtureSuitePhaseBudgetSuspendStable = 77,

    // Snapshots the owner trust bundle and proves immutability.
    FixtureSuitePhaseTlsSnapshot = 78,

    // Creates the peer control for the concurrent-bundle probe.
    FixtureSuitePhaseTlsPeerCreate = 79,

    // Waits for the peer control's document to settle.
    FixtureSuitePhaseTlsPeerLoad = 80,

    // Navigates the owner control over HTTPS with the package bundle.
    FixtureSuitePhaseTlsOwnerHttps = 81,

    // Verifies the package bundle fails closed for the fixture server.
    FixtureSuitePhaseTlsOwnerStable = 82,

    // Navigates the bundle-A peer control over HTTPS.
    FixtureSuitePhaseTlsPeerHttps = 83,

    // Verifies bundle A verifies the fixture server.
    FixtureSuitePhaseTlsPeerStable = 84,

    // Creates the second peer control for the unrelated bundle.
    // Renamed from the early no-poisoning probe before it ever fired; the
    // concurrent window below proves isolation more strongly.
    FixtureSuitePhaseTlsPeerBCreate = 85,

    // Waits for the second peer's document, then snapshots bundle B.
    // Renamed with 85; neither number ever appeared in a device log.
    FixtureSuitePhaseTlsPeerBLoad = 86,

    // Suspends with both controls' HTTPS loads in flight.
    FixtureSuitePhaseTlsSuspend = 87,

    // Resumes after the suspended span.
    FixtureSuitePhaseTlsResume = 88,

    // Verifies concurrent opposite outcomes under one suspension.
    FixtureSuitePhaseTlsConcurrentStable = 89,

    // Requests engine init with a conflicting font directory.
    FixtureSuitePhaseTlsFontConflict = 90,

    // Verifies session teardown drops trust snapshots.
    FixtureSuitePhaseTlsTeardown = 91
};

// Retry state and run-loop delays used by the temporary fixture runner.
constexpr unsigned kInitialFixtureRetry = 0;
constexpr unsigned kFixtureRetryIncrement = 1;
constexpr Seconds kInitialFixtureRetryDelay = 250_ms;
constexpr Seconds kFixtureReloadProbeDelay = 1500_ms;
constexpr Seconds kFixtureStablePageDelay = 3500_ms;
constexpr int kFixtureScrollProbeY = 300;

// Temporary device acceptance runner, explicitly opted into by the fixture
// URL's uwp-suite=1 query. Uses the real page/loader; never injects downloaded
// HTML or substitutes a shell URL history for engine history.
void scheduleFixtureSuite(PageHostUWP* owner, FixtureSuitePhase phase, unsigned retry = kInitialFixtureRetry, Seconds delay = kInitialFixtureRetryDelay)
{
    std::weak_ptr<unsigned> lifetime = owner->probeLifetime;
    WTF::RunLoop::currentSingleton().dispatchAfter(delay, [owner, lifetime, phase, retry] {
        if (!lifetime.lock() || !owner->page || !owner->log)
            return;
        auto* document = owner->frame().document();
        auto text = fixtureBodyText(owner->frame());
        auto at = [&](ASCIILiteral path) {
            return document && document->url().path() == path
                && document->readyState() == Document::ReadyState::Complete;
        };
        auto navigate = [&](const String& path, FixtureSuitePhase next) {
            ResourceRequest request;
            request.setURL(URL { makeString(owner->fixtureBase, path) });
            // Every suite navigation is a command: allocate its ID first so the
            // scenario phases can prove redirects preserve it and traversals
            // never mint a new one. Allocation itself is asserted here,
            // synchronously, while commit attribution is asserted on landing.
            uint64_t previousId = owner->navigation.activeNavigationId();
            uint64_t allocatedId = owner->navigation.beginNavigation();
            owner->log("fixture-navigation-allocated",
                allocatedId != 0 && (previousId == 0 || allocatedId != previousId));
            owner->fixtureNavigationId = allocatedId;
            owner->fixtureNavigationGeneration = owner->navigation.documentGeneration();
            owner->frame().loader().load(FrameLoadRequest(owner->frame(), WTF::move(request)));
            scheduleFixtureSuite(owner, next);
        };
        auto outcome = [&](uint64_t navigationId) {
            return owner->navigation.terminalOutcome(navigationId);
        };
        auto wait = [&] {
            // Retries ride the run loop until the document settles; then the suite fails.
            constexpr unsigned kFixtureMaxRetries = 60;
            if (retry < kFixtureMaxRetries)
                scheduleFixtureSuite(owner, phase, retry + kFixtureRetryIncrement);
            else {
                owner->log("fixture-suite-timeout", static_cast<unsigned long>(phase));
                owner->log("fixture-suite-completed", WKPageLogFalseUWP);
            }
        };
        switch (phase) {
        case FixtureSuitePhaseIndexProbes:
            if (!at("/index.html"_s) || !text.contains("PASS cookie"_s)) {
                wait();
                return;
            }
            probeFixtureMarkers(*owner, "suite");
            navigate("redirect"_s, FixtureSuitePhaseRedirect);
            return;
        case FixtureSuitePhaseRedirect:
            if (!at("/index.html"_s) || !document->url().query().isEmpty()) {
                wait();
                return;
            }
            owner->log("fixture-suite-redirect", text.contains("External CSS makes this green"_s));
            // The redirect committed a new document under the ID the suite
            // command allocated: redirects survive without minting.
            owner->log("fixture-navigation-redirect-stable",
                owner->navigation.activeNavigationId() == owner->fixtureNavigationId
                    && owner->navigation.documentGeneration() > owner->fixtureNavigationGeneration);
            // The controller's per-navigation accessors must describe the
            // landed document: index.html over HTTP 200.
            owner->log("fixture-navigation-http",
                owner->navigation.hasHttpStatus() && owner->navigation.httpStatus() == 200);
            owner->log("fixture-navigation-url", owner->navigation.source().contains("index.html"_s));
            navigate("second.html"_s, FixtureSuitePhaseHistoryBack);
            return;
        case FixtureSuitePhaseHistoryBack:
            if (!at("/second.html"_s)) {
                wait();
                return;
            }
            owner->log("fixture-suite-history-back-count", owner->page->backForward().backCount());
            owner->fixtureBackSucceeded = owner->page->backForward().goBack();
            owner->log("fixture-suite-history-back", owner->fixtureBackSucceeded);
            if (owner->fixtureBackSucceeded)
                scheduleFixtureSuite(owner, FixtureSuitePhaseHistoryForward);
            else
                navigate("missing.html"_s, FixtureSuitePhaseHistoryBackFallback);
            return;
        case FixtureSuitePhaseHistoryForward:
            if (!at("/index.html"_s)) {
                wait();
                return;
            }
            owner->log("fixture-suite-history-back-document", WKPageLogTrueUWP);
            // Traversal allocates nothing: the active ID is still the last
            // command's, and the generation never moves backward.
            owner->log("fixture-navigation-traversal-stable",
                owner->navigation.activeNavigationId() == owner->fixtureNavigationId
                    && owner->navigation.documentGeneration() >= owner->fixtureNavigationGeneration);
            owner->log("fixture-suite-history-forward", owner->page->backForward().goForward());
            scheduleFixtureSuite(owner, FixtureSuitePhaseHistoryReload);
            return;
        case FixtureSuitePhaseHistoryReload: {
            if (!at("/second.html"_s)) {
                wait();
                return;
            }
            owner->log("fixture-suite-history-forward-document", WKPageLogTrueUWP);
            owner->fixtureReloadGeneration = owner->navigation.loadGeneration();
            // Reload is a command: the fresh ID must differ from whatever was
            // active, which the commit phase later confirms survived.
            uint64_t reloadPreviousId = owner->navigation.activeNavigationId();
            uint64_t reloadAllocatedId = owner->navigation.beginNavigation();
            owner->log("fixture-navigation-reload-allocated",
                reloadAllocatedId != 0 && (reloadPreviousId == 0 || reloadAllocatedId != reloadPreviousId));
            owner->fixtureNavigationId = reloadAllocatedId;
            owner->fixtureNavigationGeneration = owner->navigation.documentGeneration();
            owner->frame().loader().reload();
            owner->log("fixture-suite-history-reload-requested", WKPageLogTrueUWP);
            scheduleFixtureSuite(owner, FixtureSuitePhaseReloadDocument, kInitialFixtureRetry, kFixtureReloadProbeDelay);
            return;
        }
        case FixtureSuitePhaseReloadDocument:
            if (!at("/second.html"_s) || owner->navigation.loadGeneration() == owner->fixtureReloadGeneration) {
                wait();
                return;
            }
            owner->log("fixture-suite-history-reload-document", text.contains("Second page"_s));
            // The reloaded commit kept the freshly allocated ID and bumped the
            // document generation; both prove the controller tracked it.
            owner->log("fixture-navigation-reload-new",
                owner->navigation.activeNavigationId() == owner->fixtureNavigationId
                    && owner->navigation.documentGeneration() > owner->fixtureNavigationGeneration);
            navigate("index.html#bottom"_s, FixtureSuitePhaseSameFragment);
            return;
        case FixtureSuitePhaseSameFragment: {
            bool sameDocument = document && document->url().path() == "/index.html"_s
                && document->url().string().contains("#bottom"_s)
                && document->readyState() == Document::ReadyState::Complete;
            if (!sameDocument) {
                wait();
                return;
            }
            int scrollY = owner->frame().view()->scrollPosition().y();
            owner->log("fixture-suite-same-fragment", WKPageLogTrueUWP);
            owner->log("fixture-suite-same-scrolled", scrollY > 0);
            // Same-document navigation commits nothing new: the command ID
            // stands and the generation never moves backward.
            owner->log("fixture-navigation-fragment-stable",
                owner->navigation.activeNavigationId() == owner->fixtureNavigationId
                    && owner->navigation.documentGeneration() >= owner->fixtureNavigationGeneration);
            bool back = owner->page->backForward().goBack();
            owner->log("fixture-suite-same-back", back);
            if (back)
                scheduleFixtureSuite(owner, FixtureSuitePhaseSameBackDocument);
            else
                navigate("missing.html"_s, FixtureSuitePhaseHttpError);
            return;
        }
        case FixtureSuitePhaseSameBackDocument:
            if (!at("/second.html"_s)) {
                wait();
                return;
            }
            owner->log("fixture-suite-same-back-document", text.contains("Second page"_s));
            navigate("missing.html"_s, FixtureSuitePhaseHttpError);
            return;
        // The history-back fallback lands here too; both expect the missing page.
        case FixtureSuitePhaseHistoryBackFallback:
        case FixtureSuitePhaseHttpError:
            if (!at("/missing.html"_s)) {
                wait();
                return;
            }
            owner->log("fixture-suite-http-error-document", text.contains("404 fixture document"_s));
            owner->log("fixture-suite-branch-forward-count", owner->page->backForward().forwardCount());
            owner->log("fixture-suite-branch-truncated", !owner->page->backForward().forwardCount());
            navigate("latin1.html"_s, FixtureSuitePhaseLatin1);
            return;
        case FixtureSuitePhaseLatin1:
            if (!at("/latin1.html"_s)) {
                wait();
                return;
            }
            owner->log("fixture-suite-latin1", text.contains(String::fromUTF8("caf\xc3\xa9")));
            navigate("policy.html"_s, FixtureSuitePhaseContentSecurityPolicy);
            return;
        case FixtureSuitePhaseContentSecurityPolicy: {
            if (!at("/policy.html"_s)) {
                wait();
                return;
            }
            owner->log("fixture-suite-csp-script", text.contains("PASS script blocked"_s));
            auto image = document->querySelector("img"_s);
            bool imageBlocked = false;
            if (!image.hasException()) {
                auto element = image.releaseReturnValue();
                auto* img = dynamicDowncast<HTMLImageElement>(element);
                imageBlocked = img && !img->naturalWidth();
            }
            owner->log("fixture-suite-csp-image", imageBlocked);
            URL other { owner->fixtureBase };
            // Second fixture server: cross-origin means a different port here.
            constexpr uint16_t kFixtureCrossOriginPort = 8003;
            other.setPort(kFixtureCrossOriginPort);
            navigate(makeString("cross-origin.html?origin="_s, other.protocolHostAndPort()), FixtureSuitePhaseCrossOrigin);
            return;
        }
        case FixtureSuitePhaseCrossOrigin:
            if (!at("/cross-origin.html"_s) || text.contains("pending"_s)) {
                wait();
                return;
            }
            owner->log("fixture-suite-cors-allowed", text.contains("PASS allowed"_s));
            owner->log("fixture-suite-cors-blocked", text.contains("PASS blocked"_s) && !text.contains("FAIL exposed"_s));
            navigate("slow.html"_s, FixtureSuitePhaseReplacementPending);
            return;
        case FixtureSuitePhaseReplacementPending:
            if (!document || document->url().path() != "/slow.html"_s || !text.contains("Input should stay responsive"_s)) {
                wait();
                return;
            }
            owner->log("fixture-suite-replacement-pending", owner->frame().loader().activeDocumentLoader()->isLoading());
            // slow.html committed (its text is on screen); the ID below must
            // therefore stay Committed no matter what the replacement does.
            owner->fixtureReplacedId = owner->fixtureNavigationId;
            navigate("second.html"_s, FixtureSuitePhaseReplacementStabilize);
            return;
        case FixtureSuitePhaseReplacementStabilize:
            if (!at("/second.html"_s)) {
                wait();
                return;
            }
            scheduleFixtureSuite(owner, FixtureSuitePhaseReplacementStable, kInitialFixtureRetry, kFixtureStablePageDelay);
            return;
        case FixtureSuitePhaseReplacementStable:
            owner->log("fixture-suite-replacement-stable", at("/second.html"_s) && text.contains("Second page"_s));
            // The replaced-while-loading document had committed before the
            // replacement, so its terminal is Committed, not Replaced.
            owner->log("fixture-navigation-replacement-committed",
                outcome(owner->fixtureReplacedId) == NavigationOutcomeUWP::Committed);
            navigate("slow.html"_s, FixtureSuitePhaseStopPending);
            return;
        case FixtureSuitePhaseStopPending:
            if (!document || document->url().path() != "/slow.html"_s || !text.contains("Input should stay responsive"_s)) {
                wait();
                return;
            }
            owner->log("fixture-suite-stop-pending", owner->frame().loader().activeDocumentLoader()->isLoading());
            owner->frame().loader().stopAllLoaders();
            scheduleFixtureSuite(owner, FixtureSuitePhaseStopStable, kInitialFixtureRetry, kFixtureStablePageDelay);
            return;
        case FixtureSuitePhaseStopStable:
            owner->log("fixture-suite-stop-stable", document && document->url().path() == "/slow.html"_s && !owner->frame().loader().activeDocumentLoader()->isLoading());
            // Stop killed the trailing subresource after the main document
            // committed, so the stopped ID stays Committed: Stop cannot
            // rewrite a commit, and any stop failure for it lands dropped.
            owner->log("fixture-navigation-stop-committed",
                outcome(owner->fixtureNavigationId) == NavigationOutcomeUWP::Committed);
            navigate("index.html"_s, FixtureSuitePhaseScrollBefore);
            return;
        case FixtureSuitePhaseScrollBefore:
            if (!at("/index.html"_s)) {
                wait();
                return;
            }

            // Scroll depth the restoration check below must recover.
            owner->frame().view()->setScrollPosition({ 0, kFixtureScrollProbeY });
            owner->fixtureScrollBefore = owner->frame().view()->scrollPosition().y();
            owner->log("fixture-suite-scroll-before", static_cast<unsigned long>(owner->fixtureScrollBefore));
            navigate("second.html"_s, FixtureSuitePhaseScrollBack);
            return;
        case FixtureSuitePhaseScrollBack:
            if (!at("/second.html"_s)) {
                wait();
                return;
            }
            owner->log("fixture-suite-scroll-back", owner->page->backForward().goBack());
            scheduleFixtureSuite(owner, FixtureSuitePhaseScrollRestored);
            return;
        case FixtureSuitePhaseScrollRestored: {
            if (!at("/index.html"_s)) {
                wait();
                return;
            }
            int scrollAfter = owner->frame().view()->scrollPosition().y();
            owner->log("fixture-suite-scroll-after", static_cast<unsigned long>(scrollAfter));
            owner->log("fixture-suite-scroll-restored", scrollAfter == owner->fixtureScrollBefore && scrollAfter > 0);
            navigate("pushstate.html"_s, FixtureSuitePhasePushState);
            return;
        }
        case FixtureSuitePhasePushState: {
            bool pushed = document && document->url().path() == "/pushstate.html"_s
                && document->url().query() == "pushed=1"_s
                && text.contains("PASS pushed"_s);
            if (!pushed) {
                wait();
                return;
            }
            owner->log("fixture-suite-pushstate-url", WKPageLogTrueUWP);
            bool back = owner->page->backForward().goBack();
            owner->log("fixture-suite-pushstate-back", back);
            if (back)
                scheduleFixtureSuite(owner, FixtureSuitePhasePushBack);
            else
                navigate("index.html"_s, FixtureSuitePhaseReplaceSetup);
            return;
        }
        case FixtureSuitePhasePushBack: {
            bool unpushed = document && document->url().path() == "/pushstate.html"_s
                && document->url().query().isEmpty()
                && document->readyState() == Document::ReadyState::Complete;
            if (!unpushed) {
                wait();
                return;
            }
            owner->log("fixture-suite-pushstate-back-document", WKPageLogTrueUWP);
            bool forward = owner->page->backForward().goForward();
            owner->log("fixture-suite-pushstate-forward", forward);
            if (forward)
                scheduleFixtureSuite(owner, FixtureSuitePhasePushForward);
            else
                navigate("index.html"_s, FixtureSuitePhaseReplaceSetup);
            return;
        }
        case FixtureSuitePhasePushForward: {
            bool repushed = document && document->url().path() == "/pushstate.html"_s
                && document->url().query() == "pushed=1"_s
                && document->readyState() == Document::ReadyState::Complete;
            if (!repushed) {
                wait();
                return;
            }
            owner->log("fixture-suite-pushstate-forward-document", WKPageLogTrueUWP);
            // pushState mutates without a command or a commit: the ID stands
            // and the generation never moves backward.
            owner->log("fixture-navigation-push-stable",
                owner->navigation.activeNavigationId() == owner->fixtureNavigationId
                    && owner->navigation.documentGeneration() >= owner->fixtureNavigationGeneration);
            navigate("index.html"_s, FixtureSuitePhaseReplaceSetup);
            return;
        }
        case FixtureSuitePhaseReplaceSetup:
            if (!at("/index.html"_s)) {
                wait();
                return;
            }
            navigate("replacestate.html"_s, FixtureSuitePhaseReplaceState);
            return;
        case FixtureSuitePhaseReplaceState: {
            bool replaced = document && document->url().path() == "/replacestate.html"_s
                && document->url().query() == "v=2"_s
                && text.contains("PASS replaced"_s);
            if (!replaced) {
                wait();
                return;
            }
            owner->log("fixture-suite-replacestate-url", WKPageLogTrueUWP);
            // pushState + replaceState must net exactly one new entry: Back
            // must land on index, not on an intermediate ?v=1 entry.
            bool back = owner->page->backForward().goBack();
            owner->log("fixture-suite-replacestate-back", back);
            if (back)
                scheduleFixtureSuite(owner, FixtureSuitePhaseReplaceBack);
            else
                scheduleFixtureSuite(owner, FixtureSuitePhaseDone);
            return;
        }
        case FixtureSuitePhaseReplaceBack: {
            // pushState + replaceState net one entry on top of the page-load
            // entry, so Back must land on replacestate.html with an empty
            // query. Landing on ?v=1 instead would prove replaceState pushed.
            bool unreplaced = document && document->url().path() == "/replacestate.html"_s
                && document->url().query().isEmpty()
                && document->readyState() == Document::ReadyState::Complete;
            if (!unreplaced) {
                wait();
                return;
            }
            owner->log("fixture-suite-replacestate-back-noreplace", WKPageLogTrueUWP);
            owner->log("fixture-suite-replacestate-forward", owner->page->backForward().goForward());
            scheduleFixtureSuite(owner, FixtureSuitePhaseReplaceForward);
            return;
        }
        case FixtureSuitePhaseReplaceForward: {
            bool rereplaced = document && document->url().path() == "/replacestate.html"_s
                && document->url().query() == "v=2"_s
                && document->readyState() == Document::ReadyState::Complete;
            if (!rereplaced) {
                wait();
                return;
            }
            owner->log("fixture-suite-replacestate-forward-document", WKPageLogTrueUWP);
            // replaceState mutates without a command or a commit: the ID stands
            // and the generation never moves backward.
            owner->log("fixture-navigation-replace-stable",
                owner->navigation.activeNavigationId() == owner->fixtureNavigationId
                    && owner->navigation.documentGeneration() >= owner->fixtureNavigationGeneration);
            navigate("index.html"_s, FixtureSuitePhaseRaceProbe);
            return;
        }
        case FixtureSuitePhaseRaceProbe: {
            // Replacement race: a slow main-frame load replaced in the same
            // turn, before any response. The loser must end Replaced and the
            // winner must commit; the loser's late signals must change nothing.
            // A single RaceStable is scheduled (by navigate); the winner is
            // issued inline so no second schedule can fork the suite.
            navigate("delay?ms=5000"_s, FixtureSuitePhaseRaceStable);
            owner->fixtureRaceId = owner->fixtureNavigationId;
            ResourceRequest winnerRequest;
            winnerRequest.setURL(URL { makeString(owner->fixtureBase, "index.html"_s) });
            uint64_t winnerId = owner->navigation.beginNavigation();
            owner->fixtureRaceGoodId = winnerId;
            owner->fixtureNavigationId = winnerId;
            owner->fixtureNavigationGeneration = owner->navigation.documentGeneration();
            owner->frame().loader().load(FrameLoadRequest(owner->frame(), WTF::move(winnerRequest)));
            return;
        }
        case FixtureSuitePhaseRaceStable:
            if (!at("/index.html"_s)) {
                wait();
                return;
            }
            owner->log("fixture-navigation-replaced",
                outcome(owner->fixtureRaceId) == NavigationOutcomeUWP::Replaced);
            owner->log("fixture-navigation-race-good",
                outcome(owner->fixtureRaceGoodId) == NavigationOutcomeUWP::Committed);
            scheduleFixtureSuite(owner, FixtureSuitePhaseVetoProbe);
            return;
        case FixtureSuitePhaseVetoProbe:
            // Arm the host veto, then issue a refused navigation. The veto
            // lands synchronously in policy; confirmation gates the good
            // navigation so the ordering is deterministic.
            g_fixtureVetoSubstring = "vetoed-probe";
            owner->policyHandler = fixtureVetoPolicy;
            navigate("vetoed-probe.html"_s, FixtureSuitePhaseVetoConfirm);
            owner->fixtureVetoId = owner->fixtureNavigationId;
            return;
        case FixtureSuitePhaseVetoConfirm:
            if (outcome(owner->fixtureVetoId) != NavigationOutcomeUWP::Vetoed) {
                wait();
                return;
            }

            // Reentrant overlap: the good policy decision runs while the
            // vetoed load is still dying; it must not resurrect the vetoed ID.
            navigate("index.html"_s, FixtureSuitePhaseVetoStable);
            return;
        case FixtureSuitePhaseVetoStable: {
            if (!at("/index.html"_s)) {
                wait();
                return;
            }
            owner->log("fixture-navigation-vetoed",
                outcome(owner->fixtureVetoId) == NavigationOutcomeUWP::Vetoed);
            owner->log("fixture-navigation-veto-stable",
                outcome(owner->fixtureVetoId) == NavigationOutcomeUWP::Vetoed
                    && outcome(owner->fixtureNavigationId) == NavigationOutcomeUWP::Committed);
            owner->policyHandler = nullptr;
            g_fixtureVetoSubstring = nullptr;
            navigate("redirect-loop"_s, FixtureSuitePhaseLoopProbe);
            owner->fixtureLoopId = owner->fixtureNavigationId;
            return;
        }
        case FixtureSuitePhaseLoopProbe:
            // The loop never commits; LoopStable gates on the terminal.
            scheduleFixtureSuite(owner, FixtureSuitePhaseLoopStable);
            return;
        case FixtureSuitePhaseLoopStable:
            if (outcome(owner->fixtureLoopId) != NavigationOutcomeUWP::ProvisionalFailure) {
                wait();
                return;
            }
            owner->log("fixture-navigation-loop-failed", WKPageLogTrueUWP);
            // No commit happened for the looped navigation: the generation
            // the loop command observed is still the current one.
            owner->log("fixture-navigation-loop-nocommit",
                owner->navigation.documentGeneration() == owner->fixtureNavigationGeneration);
            navigate("download"_s, FixtureSuitePhaseDownloadProbe);
            owner->fixtureDownloadId = owner->fixtureNavigationId;
            return;
        case FixtureSuitePhaseDownloadProbe:
            // The attachment response is refused synchronously in policy;
            // DownloadStable gates on the terminal like the loop does.
            scheduleFixtureSuite(owner, FixtureSuitePhaseDownloadStable);
            return;
        case FixtureSuitePhaseDownloadStable:
            if (outcome(owner->fixtureDownloadId) != NavigationOutcomeUWP::Vetoed) {
                wait();
                return;
            }
            owner->log("fixture-navigation-download-vetoed", WKPageLogTrueUWP);
            owner->log("fixture-navigation-download-nocommit",
                owner->navigation.documentGeneration() == owner->fixtureNavigationGeneration);
            scheduleFixtureSuite(owner, FixtureSuitePhaseIsolationCreate);
            return;
        case FixtureSuitePhaseIsolationCreate:
            g_fixturePeer = createIsolationPeer(owner->fixtureBase);
            if (!g_fixturePeer) {
                owner->log("fixture-suite-completed", WKPageLogFalseUWP);
                return;
            }
            scheduleFixtureSuite(owner, FixtureSuitePhaseIsolationLoad);
            return;
        case FixtureSuitePhaseIsolationLoad: {
            auto* peerDocument = g_fixturePeer && g_fixturePeer->page && g_fixturePeer->page->localMainFrame()
                ? g_fixturePeer->page->localMainFrame()->document()
                : nullptr;
            bool peerReady = peerDocument && peerDocument->url().path() == "/index.html"_s
                && peerDocument->readyState() == Document::ReadyState::Complete;
            if (!peerReady) {
                wait();
                return;
            }

            // Distinct ephemeral sessions back the two controls.
            owner->log("fixture-cookie-session-distinct",
                g_fixturePeer->page->sessionID() != owner->page->sessionID());
            scheduleFixtureSuite(owner, FixtureSuitePhaseIsolationSet);
            return;
        }
        case FixtureSuitePhaseIsolationSet: {
            // The jar already holds the fetch-set cookie, so own-cookie
            // assertions contain rather than equal: DOM and transport cookies
            // share the session by design.
            owner->page->localMainFrame()->document()->setCookie("iso=A"_s);
            owner->log("fixture-cookie-own", fixtureCookieString(owner->page.get()).contains("iso=A"_s));
            // The peer shares the host but not the jar: the owner's DOM
            // cookie is absent there (its own fetch cookie may be present).
            owner->log("fixture-cookie-isolated", !fixtureCookieString(g_fixturePeer->page.get()).contains("iso=A"_s));
            scheduleFixtureSuite(owner, FixtureSuitePhaseIsolationCross);
            return;
        }
        case FixtureSuitePhaseIsolationCross:
            g_fixturePeer->page->localMainFrame()->document()->setCookie("iso=B"_s);
            owner->log("fixture-cookie-cross",
                fixtureCookieString(owner->page.get()).contains("iso=A"_s)
                    && !fixtureCookieString(owner->page.get()).contains("iso=B"_s)
                    && fixtureCookieString(g_fixturePeer->page.get()).contains("iso=B"_s)
                    && !fixtureCookieString(g_fixturePeer->page.get()).contains("iso=A"_s));
            scheduleFixtureSuite(owner, FixtureSuitePhaseIsolationTeardown);
            return;
        case FixtureSuitePhaseIsolationTeardown: {
            auto deadSession = g_fixturePeer->page->sessionID();
            auto liveSession = owner->page->sessionID();
            // Was the peer session ever registered under the page's ID?
            owner->log("fixture-cookie-teardown-registered", !!storageSessionUWP(deadSession));
            // Does scope exit unregister a stack provider for a fresh ID?
            auto probeSession = PAL::SessionID::generateEphemeralSessionID();
            {
                StorageSessionProviderUWP probe(probeSession);
                owner->log("fixture-cookie-scope-alive", !!storageSessionUWP(probeSession));
            }
            owner->log("fixture-cookie-scope-gone", !storageSessionUWP(probeSession));
            // Does anything besides the suite still own the peer page?
            owner->log("fixture-cookie-teardown-unique", g_fixturePeer->page->hasOneRef());
            owner->log("fixture-cookie-jar-shared", !g_fixturePeer->page->cookieJar().hasOneRef());
            // Keep the jar and provider alive deliberately: host teardown
            // must destroy storage without waiting for their last reference.
            Ref retainedJar = g_fixturePeer->page->cookieJar();
            Ref retainedProvider = *g_fixturePeer->storageProvider;
            WeakPtr peerPage = *g_fixturePeer->page;
            delete g_fixturePeer;
            g_fixturePeer = nullptr;
            // Teardown unregisters the dead session and touches nothing else:
            // the dead lookup misses, the live session stays registered, and
            // the surviving control keeps its cookie.
            owner->log("fixture-cookie-teardown-gone", !storageSessionUWP(deadSession));
            owner->log("fixture-cookie-teardown-live", !!storageSessionUWP(liveSession));
            owner->log("fixture-cookie-teardown-intact", fixtureCookieString(owner->page.get()).contains("iso=A"_s));
            owner->log("fixture-cookie-teardown-page-gone", !peerPage);
            owner->log("fixture-cookie-teardown-storage-closed", !retainedProvider->storageSession());
            retainedProvider->close();
            owner->log("fixture-cookie-teardown-close-idempotent", !retainedProvider->storageSession() && !storageSessionUWP(deadSession));
            bool teardownPassed = !peerPage && !storageSessionUWP(deadSession) && !!storageSessionUWP(liveSession)
                && !retainedProvider->storageSession()
                && fixtureCookieString(owner->page.get()).contains("iso=A"_s);
            owner->log("fixture-cookie-teardown", teardownPassed);
            if (!teardownPassed) {
                owner->log("fixture-suite-completed", WKPageLogFalseUWP);
                return;
            }
            scheduleFixtureSuite(owner, FixtureSuitePhaseRaceDelayed);
            return;
        }
        case FixtureSuitePhaseRaceDelayed:
            // Delayed-fixture proof: across the veto, loop and download phases
            // the race loser never reached a success terminal, and the engine
            // absorbed every stale signal as counted duplicates.
            owner->log("fixture-navigation-replaced-delayed",
                outcome(owner->fixtureRaceId) == NavigationOutcomeUWP::Replaced);
            owner->log("fixture-navigation-terminal-duplicates",
                owner->navigation.duplicateTerminalSignals());
            // Arm a fresh recorded failure for the transient contrast: the
            // loop fails without committing, naming its URL as the failure.
            navigate("redirect-loop"_s, FixtureSuitePhaseTransientFailProbe);
            owner->fixtureLoopId = owner->fixtureNavigationId;
            return;
        case FixtureSuitePhaseTransientFailProbe:
            if (outcome(owner->fixtureLoopId) != NavigationOutcomeUWP::ProvisionalFailure) {
                wait();
                return;
            }
            scheduleFixtureSuite(owner, FixtureSuitePhaseTransientBlankProbe);
            return;
        case FixtureSuitePhaseTransientBlankProbe: {
            // Snapshot history and the recorded failure, then show the
            // warning interstitial over them. Substitute documents stay
            // hidden from session history by construction.
            owner->fixtureFailedUrlBefore = owner->navigation.failedUrl();
            owner->log("fixture-transient-failure-armed", !owner->fixtureFailedUrlBefore.isEmpty());
            owner->fixtureBackCount = owner->page->backForward().backCount();
            owner->fixtureForwardCount = owner->page->backForward().forwardCount();
            auto currentItem = owner->page->backForward().currentItem();
            owner->fixtureCurrentUrl = currentItem ? currentItem->url().string() : String();
            owner->transient.show(TransientContentKindUWP::Warning,
                "<title>Warning</title><h1>TRANSIENT WARNING fixture</h1>"_s);
            scheduleFixtureSuite(owner, FixtureSuitePhaseTransientBlankStable);
            return;
        }
        case FixtureSuitePhaseTransientBlankStable: {
            bool warningShown = document && document->url().protocolIs("about"_s)
                && document->readyState() == Document::ReadyState::Complete
                && fixtureBodyText(owner->frame()).contains("TRANSIENT WARNING"_s);
            if (!warningShown) {
                wait();
                return;
            }

            // Opaque origin: about:blank serializes as null, inheriting
            // nothing from the failed page it covers.
            owner->log("fixture-transient-blank-origin",
                document->securityOrigin().toString() == "null"_s);
            owner->log("fixture-transient-kind",
                owner->transient.kind() == TransientContentKindUWP::Warning
                    && owner->transient.state() == TransientPageStateUWP::Shown);
            owner->log("fixture-transient-committed",
                outcome(owner->transient.transientNavigationId()) == NavigationOutcomeUWP::Committed);
            // The warning preserves the failure behind the proceed gesture.
            owner->log("fixture-transient-warning-preserved",
                owner->navigation.failedUrl() == owner->fixtureFailedUrlBefore
                    && !owner->navigation.failedUrl().isEmpty());
            owner->log("fixture-transient-history",
                owner->page->backForward().backCount() == owner->fixtureBackCount
                    && owner->page->backForward().forwardCount() == owner->fixtureForwardCount);
            auto shownItem = owner->page->backForward().currentItem();
            owner->log("fixture-transient-cursor",
                shownItem && shownItem->url().string() == owner->fixtureCurrentUrl);
            scheduleFixtureSuite(owner, FixtureSuitePhaseTransientVetoProbe);
            return;
        }
        case FixtureSuitePhaseTransientVetoProbe:
            // Refuse the generic show at policy: the vetoed transient must
            // change neither the document nor the preserved failure.
            g_fixtureVetoSubstring = "about:blank";
            owner->policyHandler = fixtureVetoPolicy;
            owner->transient.show(TransientContentKindUWP::GenericHtml,
                "<title>Generic</title><p>TRANSIENT GENERIC fixture</p>"_s);
            owner->fixtureVetoId = owner->transient.transientNavigationId();
            scheduleFixtureSuite(owner, FixtureSuitePhaseTransientVetoStable);
            return;
        case FixtureSuitePhaseTransientVetoStable: {
            if (outcome(owner->fixtureVetoId) != NavigationOutcomeUWP::Vetoed) {
                wait();
                return;
            }
            owner->log("fixture-transient-vetoed", WKPageLogTrueUWP);
            // The refused show changed nothing: the warning document, its
            // Shown kind slot, the preserved failure and history all stand.
            auto* vetoDocument = owner->frame().document();
            owner->log("fixture-transient-veto-stable",
                vetoDocument && fixtureBodyText(owner->frame()).contains("TRANSIENT WARNING"_s)
                    && owner->transient.kind() == TransientContentKindUWP::Warning
                    && owner->transient.state() == TransientPageStateUWP::Shown
                    && owner->navigation.failedUrl() == owner->fixtureFailedUrlBefore
                    && owner->page->backForward().backCount() == owner->fixtureBackCount
                    && owner->page->backForward().forwardCount() == owner->fixtureForwardCount);
            owner->policyHandler = nullptr;
            g_fixtureVetoSubstring = nullptr;
            scheduleFixtureSuite(owner, FixtureSuitePhaseTransientGenericProbe);
            return;
        }
        case FixtureSuitePhaseTransientGenericProbe:
            owner->transient.show(TransientContentKindUWP::GenericHtml,
                "<title>Generic</title><p>TRANSIENT GENERIC fixture</p>"_s);
            scheduleFixtureSuite(owner, FixtureSuitePhaseTransientGenericStable);
            return;
        case FixtureSuitePhaseTransientGenericStable: {
            bool genericShown = document && document->url().protocolIs("about"_s)
                && document->readyState() == Document::ReadyState::Complete
                && fixtureBodyText(owner->frame()).contains("TRANSIENT GENERIC"_s);
            if (!genericShown) {
                wait();
                return;
            }
            owner->log("fixture-transient-generic-origin",
                document->securityOrigin().toString() == "null"_s);
            // Generic host markup fails closed: its commit invalidates the
            // recorded failure instead of inheriting the warning's proceed.
            owner->log("fixture-transient-generic-invalidated",
                owner->navigation.failedUrl().isEmpty());
            owner->log("fixture-transient-generic-history",
                owner->page->backForward().backCount() == owner->fixtureBackCount
                    && owner->page->backForward().forwardCount() == owner->fixtureForwardCount);
            scheduleFixtureSuite(owner, FixtureSuitePhaseTransientReloadProbe);
            return;
        }
        case FixtureSuitePhaseTransientReloadProbe:
            owner->frame().loader().reload();
            scheduleFixtureSuite(owner, FixtureSuitePhaseTransientReloadStable);
            return;
        case FixtureSuitePhaseTransientReloadStable: {
            bool reloadedBlank = document && document->url().protocolIs("about"_s)
                && document->readyState() == Document::ReadyState::Complete;
            if (!reloadedBlank) {
                wait();
                return;
            }

            // Reloading transient content reloads blankness, not the network:
            // no entry is added and the ID stays Committed. Core re-anchors
            // the current entry to the reloaded blank document, so the cursor
            // entry reads about:blank with the entry list itself untouched.
            owner->log("fixture-transient-reload-history",
                owner->page->backForward().backCount() == owner->fixtureBackCount
                    && owner->page->backForward().forwardCount() == owner->fixtureForwardCount);
            auto reloadedItem = owner->page->backForward().currentItem();
            owner->log("fixture-transient-reload-cursor",
                reloadedItem && reloadedItem->url().protocolIs("about"_s));
            owner->log("fixture-transient-reload-committed",
                outcome(owner->transient.transientNavigationId()) == NavigationOutcomeUWP::Committed);
            owner->log("fixture-transient-reload-body",
                fixtureBodyText(owner->frame()).contains("TRANSIENT GENERIC"_s));
            // Fail the owner control for a certificate decision: the loop
            // fails without committing and records a failure revision.
            navigate("redirect-loop"_s, FixtureSuitePhaseCertFailOwner);
            owner->fixtureLoopId = owner->fixtureNavigationId;
            return;
        }
        case FixtureSuitePhaseCertFailOwner:
            if (outcome(owner->fixtureLoopId) != NavigationOutcomeUWP::ProvisionalFailure) {
                wait();
                return;
            }
            CertificateExceptionStoreUWP::issuedToken(owner->page->sessionID(),
                owner->fixtureCertIssuerA, owner->fixtureCertDecisionA);
            owner->log("fixture-cert-owner-armed",
                owner->fixtureCertIssuerA != 0 && owner->fixtureCertDecisionA != 0);
            scheduleFixtureSuite(owner, FixtureSuitePhaseCertPeerCreate);
            return;
        case FixtureSuitePhaseCertPeerCreate:
            g_fixturePeer = createIsolationPeer(owner->fixtureBase);
            if (!g_fixturePeer) {
                owner->log("fixture-suite-completed", WKPageLogFalseUWP);
                return;
            }
            scheduleFixtureSuite(owner, FixtureSuitePhaseCertPeerLoad);
            return;
        case FixtureSuitePhaseCertPeerLoad: {
            auto* peerDocument = g_fixturePeer && g_fixturePeer->page && g_fixturePeer->page->localMainFrame()
                ? g_fixturePeer->page->localMainFrame()->document()
                : nullptr;
            bool peerReady = peerDocument && peerDocument->url().path() == "/index.html"_s
                && peerDocument->readyState() == Document::ReadyState::Complete;
            if (!peerReady) {
                wait();
                return;
            }

            // The peer fails at its own first revision: equal numeric
            // ordinals across controls must never imply equal tokens.
            g_fixturePeer->navigation.beginNavigation();
            ResourceRequest peerRequest;
            peerRequest.setURL(URL { makeString(owner->fixtureBase, "redirect-loop"_s) });
            auto& peerFrame = g_fixturePeer->frame();
            peerFrame.loader().load(FrameLoadRequest(peerFrame, WTF::move(peerRequest)));
            scheduleFixtureSuite(owner, FixtureSuitePhaseCertPeerFail);
            return;
        }
        case FixtureSuitePhaseCertPeerFail:
            CertificateExceptionStoreUWP::issuedToken(g_fixturePeer->page->sessionID(),
                owner->fixtureCertIssuerB, owner->fixtureCertDecisionB);
            if (!owner->fixtureCertDecisionB) {
                wait();
                return;
            }
            owner->log("fixture-cert-peer-armed",
                owner->fixtureCertIssuerB != 0 && owner->fixtureCertIssuerB != owner->fixtureCertIssuerA);
            scheduleFixtureSuite(owner, FixtureSuitePhaseCertCross);
            return;
        case FixtureSuitePhaseCertCross: {
            URL loopURL { makeString(owner->fixtureBase, "redirect-loop"_s) };
            auto host = loopURL.host().toString();
            uint16_t port = loopURL.port().value_or(8002);
            // A's token submitted to B is rejected even though both controls
            // failed at local navigation ID 1: issuer binding decides.
            bool crossAccepted = CertificateExceptionStoreUWP::consumeToken(
                g_fixturePeer->page->sessionID(),
                owner->fixtureCertIssuerA, owner->fixtureCertDecisionA, host, port);
            owner->log("fixture-cert-cross-rejected", !crossAccepted);
            // A failed consume mutates nothing: A's token stays eligible.
            uint64_t issuer = 0;
            uint64_t decision = 0;
            CertificateExceptionStoreUWP::issuedToken(owner->page->sessionID(), issuer, decision);
            owner->log("fixture-cert-cross-intact",
                issuer == owner->fixtureCertIssuerA && decision == owner->fixtureCertDecisionA);
            navigate("redirect-loop"_s, FixtureSuitePhaseCertStaleFail);
            owner->fixtureLoopId = owner->fixtureNavigationId;
            return;
        }
        case FixtureSuitePhaseCertStaleFail:
            if (outcome(owner->fixtureLoopId) != NavigationOutcomeUWP::ProvisionalFailure) {
                wait();
                return;
            }
            scheduleFixtureSuite(owner, FixtureSuitePhaseCertStaleStable);
            return;
        case FixtureSuitePhaseCertStaleStable: {
            uint64_t issuer = 0;
            uint64_t decision = 0;
            CertificateExceptionStoreUWP::issuedToken(owner->page->sessionID(), issuer, decision);
            URL loopURL { makeString(owner->fixtureBase, "redirect-loop"_s) };
            // The first token went stale when the second failure superseded
            // it: same session, older revision, rejected.
            bool staleAccepted = CertificateExceptionStoreUWP::consumeToken(
                owner->page->sessionID(),
                owner->fixtureCertIssuerA, owner->fixtureCertDecisionA,
                loopURL.host().toString(), loopURL.port().value_or(8002));
            owner->log("fixture-cert-stale-rejected", !staleAccepted && decision != owner->fixtureCertDecisionA);
            owner->fixtureCertIssuerA = issuer;
            owner->fixtureCertDecisionA = decision;
            owner->transient.show(TransientContentKindUWP::Warning,
                "<title>Warning</title><h1>TRANSIENT WARNING fixture</h1>"_s);
            scheduleFixtureSuite(owner, FixtureSuitePhaseCertWarn);
            return;
        }
        case FixtureSuitePhaseCertWarn:
            scheduleFixtureSuite(owner, FixtureSuitePhaseCertWarnStable);
            return;
        case FixtureSuitePhaseCertWarnStable: {
            bool warningShown = document && document->url().protocolIs("about"_s)
                && document->readyState() == Document::ReadyState::Complete
                && fixtureBodyText(owner->frame()).contains("TRANSIENT WARNING"_s);
            if (!warningShown) {
                wait();
                return;
            }

            // Token-associated warning content preserves eligibility through
            // its provisional and commit, matching the failure preservation.
            uint64_t issuer = 0;
            uint64_t decision = 0;
            CertificateExceptionStoreUWP::issuedToken(owner->page->sessionID(), issuer, decision);
            owner->log("fixture-cert-warn-preserved",
                issuer == owner->fixtureCertIssuerA && decision == owner->fixtureCertDecisionA);
            owner->transient.show(TransientContentKindUWP::GenericHtml,
                "<title>Generic</title><p>TRANSIENT GENERIC fixture</p>"_s);
            scheduleFixtureSuite(owner, FixtureSuitePhaseCertGeneric);
            return;
        }
        case FixtureSuitePhaseCertGeneric:
            scheduleFixtureSuite(owner, FixtureSuitePhaseCertGenericStable);
            return;
        case FixtureSuitePhaseCertGenericStable: {
            bool genericShown = document && document->url().protocolIs("about"_s)
                && document->readyState() == Document::ReadyState::Complete
                && fixtureBodyText(owner->frame()).contains("TRANSIENT GENERIC"_s);
            if (!genericShown) {
                wait();
                return;
            }

            // Generic content invalidates the decision like it invalidates
            // the recorded failure: issuing now yields zero/zero.
            uint64_t issuer = 0;
            uint64_t decision = 0;
            CertificateExceptionStoreUWP::issuedToken(owner->page->sessionID(), issuer, decision);
            owner->log("fixture-cert-generic-invalidated", !issuer && !decision);
            scheduleFixtureSuite(owner, FixtureSuitePhaseCertConsume);
            return;
        }
        case FixtureSuitePhaseCertConsume: {
            auto peerSession = g_fixturePeer->page->sessionID();
            auto ownerSession = owner->page->sessionID();
            URL loopURL { makeString(owner->fixtureBase, "redirect-loop"_s) };
            auto host = loopURL.host().toString();
            uint16_t port = loopURL.port().value_or(8002);
            // The peer's own token consumes exactly once and arms a
            // session/host/port exception for the validated target only.
            bool consumed = CertificateExceptionStoreUWP::consumeToken(
                peerSession, owner->fixtureCertIssuerB, owner->fixtureCertDecisionB, host, port);
            owner->log("fixture-cert-consumed", consumed);
            URL otherPortURL { owner->fixtureBase };
            otherPortURL.setPort(8003);
            owner->log("fixture-cert-port-isolated",
                CertificateExceptionStoreUWP::allowsRequest(peerSession, loopURL)
                    && !CertificateExceptionStoreUWP::allowsRequest(peerSession, otherPortURL)
                    && !CertificateExceptionStoreUWP::allowsRequest(ownerSession, loopURL));
            // Reuse is rejected: the token died with its single consumption.
            bool reused = CertificateExceptionStoreUWP::consumeToken(
                peerSession, owner->fixtureCertIssuerB, owner->fixtureCertDecisionB, host, port);
            owner->log("fixture-cert-used-rejected", !reused);
            navigate("redirect-loop"_s, FixtureSuitePhaseCertRefail);
            owner->fixtureLoopId = owner->fixtureNavigationId;
            return;
        }
        case FixtureSuitePhaseCertRefail:
            if (outcome(owner->fixtureLoopId) != NavigationOutcomeUWP::ProvisionalFailure) {
                wait();
                return;
            }
            scheduleFixtureSuite(owner, FixtureSuitePhaseCertRemoteGone);
            return;
        case FixtureSuitePhaseCertRemoteGone: {
            uint64_t issuer = 0;
            uint64_t decision = 0;
            CertificateExceptionStoreUWP::issuedToken(owner->page->sessionID(), issuer, decision);
            owner->log("fixture-cert-refail-armed", issuer != 0 && decision != 0);
            navigate("index.html"_s, FixtureSuitePhaseBudgetDefaults);
            return;
        }
        case FixtureSuitePhaseBudgetDefaults: {
            if (!at("/index.html"_s)) {
                wait();
                return;
            }

            // Contract defaults from artifact/public-api.md, enforced below.
            auto defaults = ResourcePolicyUWP::defaults();
            owner->log("fixture-budget-defaults",
                defaults.maxConcurrentTransfers == 8
                    && defaults.maxResourceBytes == 32u * 1024u * 1024u
                    && defaults.maxBufferedBytes == 16u * 1024u * 1024u
                    && defaults.timeoutMilliseconds == 30000
                    && defaults.maxQueuedRequests == 128
                    && defaults.maxRedirectHops == 10);
            // Out-of-range overrides are rejected without touching policy.
            auto badLimits = defaults;
            badLimits.maxConcurrentTransfers = 99;
            auto goodLimits = defaults;
            goodLimits.maxConcurrentTransfers = 1;
            auto sessionID = owner->page->sessionID();
            owner->log("fixture-budget-ranges",
                !ResourcePolicyUWP::setSessionLimits(sessionID, badLimits)
                    && ResourcePolicyUWP::setSessionLimits(sessionID, goodLimits));
            ResourcePolicyUWP::resetSessionLimits(sessionID);
            navigate("redirect-loop"_s, FixtureSuitePhaseBudgetRedirects);
            owner->fixtureLoopId = owner->fixtureNavigationId;
            return;
        }
        case FixtureSuitePhaseBudgetRedirects:
            if (outcome(owner->fixtureLoopId) != NavigationOutcomeUWP::ProvisionalFailure) {
                wait();
                return;
            }

            // The loop dies on the redirect budget with its distinguishable
            // code; the hop count is in the transport log beside it.
            owner->log("fixture-budget-redirect-code", owner->navigation.transportError() == 8);
            scheduleFixtureSuite(owner, FixtureSuitePhaseBudgetResource);
            return;
        case FixtureSuitePhaseBudgetResource: {
            // index.html is 976 bytes: it passes the default budget and fails
            // a 512-byte fixture budget, proving the per-resource check.
            auto tinyLimits = ResourcePolicyUWP::defaults();
            tinyLimits.maxResourceBytes = 512;
            ResourcePolicyUWP::setSessionLimitsForTesting(owner->page->sessionID(), tinyLimits);
            navigate("index.html"_s, FixtureSuitePhaseBudgetResourceStable);
            owner->fixtureLoopId = owner->fixtureNavigationId;
            return;
        }
        case FixtureSuitePhaseBudgetResourceStable:
            if (outcome(owner->fixtureLoopId) != NavigationOutcomeUWP::ProvisionalFailure) {
                wait();
                return;
            }
            owner->log("fixture-budget-resource-code", owner->navigation.transportError() == 11);
            ResourcePolicyUWP::resetSessionLimits(owner->page->sessionID());
            navigate("second.html"_s, FixtureSuitePhaseBudgetTransient);
            return;
        case FixtureSuitePhaseBudgetTransient: {
            if (!at("/second.html"_s)) {
                wait();
                return;
            }

            // The session transient account bounds in-flight bytes: a 976-byte
            // document fails a 512-byte fixture buffer before any copy.
            auto tinyLimits = ResourcePolicyUWP::defaults();
            tinyLimits.maxBufferedBytes = 512;
            ResourcePolicyUWP::setSessionLimitsForTesting(owner->page->sessionID(), tinyLimits);
            navigate("index.html"_s, FixtureSuitePhaseBudgetTransientStable);
            owner->fixtureLoopId = owner->fixtureNavigationId;
            return;
        }
        case FixtureSuitePhaseBudgetTransientStable: {
            if (outcome(owner->fixtureLoopId) != NavigationOutcomeUWP::ProvisionalFailure) {
                wait();
                return;
            }
            owner->log("fixture-budget-transient-code", owner->navigation.transportError() == 13);
            ResourcePolicyUWP::resetSessionLimits(owner->page->sessionID());
            // Arm the short deadline before the delayed navigation starts so
            // the transfer's own timer carries the fixture bound.
            auto quickLimits = ResourcePolicyUWP::defaults();
            quickLimits.timeoutMilliseconds = 1500;
            ResourcePolicyUWP::setSessionLimits(owner->page->sessionID(), quickLimits);
            navigate("delay?ms=2000"_s, FixtureSuitePhaseBudgetTimeout);
            owner->fixtureLoopId = owner->fixtureNavigationId;
            return;
        }
        case FixtureSuitePhaseBudgetTimeout: {
            scheduleFixtureSuite(owner, FixtureSuitePhaseBudgetTimeoutStable);
            return;
        }
        case FixtureSuitePhaseBudgetTimeoutStable: {
            if (outcome(owner->fixtureLoopId) != NavigationOutcomeUWP::ProvisionalFailure) {
                wait();
                return;
            }
            owner->log("fixture-budget-timeout-code", owner->navigation.transportError() == 28);
            ResourcePolicyUWP::resetSessionLimits(owner->page->sessionID());
            // Arm the short deadline before the delayed navigation starts so
            // the transfer's own timer carries the fixture bound.
            auto suspendLimits = ResourcePolicyUWP::defaults();
            suspendLimits.timeoutMilliseconds = 1500;
            ResourcePolicyUWP::setSessionLimits(owner->page->sessionID(), suspendLimits);
            navigate("delay?ms=2000"_s, FixtureSuitePhaseBudgetSuspend);
            owner->fixtureLoopId = owner->fixtureNavigationId;
            return;
        }
        case FixtureSuitePhaseBudgetSuspend: {
            // Suspend past the 1500 ms deadline while the delayed transfer is
            // in flight: the suspended span must not count against it.
            auto strategyHandle = platformStrategies()->loaderStrategy();
            auto* strategy = static_cast<LoaderStrategyUWP*>(strategyHandle.get());
            owner->log("fixture-budget-suspend-running",
                strategy->activeTransferCount(owner->page->sessionID()) >= 1);
            strategy->suspendPendingRequests();
            scheduleFixtureSuite(owner, FixtureSuitePhaseBudgetResume, kInitialFixtureRetry, 2500_ms);
            return;
        }
        case FixtureSuitePhaseBudgetResume: {
            auto strategyHandle = platformStrategies()->loaderStrategy();
            auto* strategy = static_cast<LoaderStrategyUWP*>(strategyHandle.get());
            strategy->resumePendingRequests();
            owner->log("fixture-budget-resume-running",
                strategy->activeTransferCount(owner->page->sessionID()) >= 1);
            scheduleFixtureSuite(owner, FixtureSuitePhaseBudgetSuspendStable);
            return;
        }
        case FixtureSuitePhaseBudgetSuspendStable: {
            bool delayedDone = document && document->url().path() == "/delay"_s
                && document->readyState() == Document::ReadyState::Complete;
            if (!delayedDone) {
                wait();
                return;
            }

            // The transfer survived 2500 ms suspended under a 1500 ms
            // deadline, so suspension paused the clock; visual unload never
            // pauses it, which the plain timeout phase already proved.
            auto suspendOutcome = outcome(owner->fixtureLoopId);
            owner->log("fixture-budget-suspend-survived",
                suspendOutcome == NavigationOutcomeUWP::Committed);
            owner->log("fixture-budget-suspend-outcome", static_cast<unsigned long>(suspendOutcome));
            owner->log("fixture-budget-suspend-transport", static_cast<unsigned long>(owner->navigation.transportError()));
            // Values, not just verdicts: the loop ID under test, the live
            // active ID and the last committed ID discriminate capture bugs,
            // stray commands and ledger anomalies without further guessing.
            owner->log("fixture-budget-suspend-loopid", static_cast<unsigned long>(owner->fixtureLoopId));
            owner->log("fixture-budget-suspend-activeid", static_cast<unsigned long>(owner->navigation.activeNavigationId()));
            owner->log("fixture-budget-suspend-commitid", static_cast<unsigned long>(owner->navigation.lastCommitId()));
            // No stray command may have replaced the suspended navigation.
            owner->log("fixture-budget-suspend-active",
                owner->navigation.activeNavigationId() == owner->fixtureLoopId);
            ResourcePolicyUWP::resetSessionLimits(owner->page->sessionID());
            navigate("index.html"_s, FixtureSuitePhaseTlsSnapshot);
            return;
        }
        case FixtureSuitePhaseTlsSnapshot: {
            if (!at("/index.html"_s)) {
                wait();
                return;
            }

            // The shell's startup navigation already snapshotted this session
            // with the package bundle: first wins, so per-bundle proofs run
            // on fresh peers below with their own sessions.
            auto* ownerSnapshot = TlsPolicyUWP::snapshotFor(owner->page->sessionID());
            owner->log("fixture-tls-owner-bundle", ownerSnapshot && !ownerSnapshot->sha256Hex.isEmpty());
            if (ownerSnapshot)
                owner->fixtureTlsOwnerHash = ownerSnapshot->sha256Hex;

            // Device wall clock for certificate validity diagnosis.
            auto wallSeconds = static_cast<unsigned long>(WallTime::now().secondsSinceEpoch().seconds());
            owner->log("fixture-wall-clock", wallSeconds);
            scheduleFixtureSuite(owner, FixtureSuitePhaseTlsPeerCreate);
            return;
        }
        case FixtureSuitePhaseTlsPeerCreate:
            g_fixturePeer = createIsolationPeer(owner->fixtureBase);
            if (!g_fixturePeer) {
                owner->log("fixture-suite-completed", WKPageLogFalseUWP);
                return;
            }
            scheduleFixtureSuite(owner, FixtureSuitePhaseTlsPeerLoad);
            return;
        case FixtureSuitePhaseTlsPeerLoad: {
            auto* peerDocument = g_fixturePeer && g_fixturePeer->page && g_fixturePeer->page->localMainFrame()
                ? g_fixturePeer->page->localMainFrame()->document()
                : nullptr;
            bool peerReady = peerDocument && peerDocument->url().path() == "/index.html"_s
                && peerDocument->readyState() == Document::ReadyState::Complete;
            if (!peerReady) {
                wait();
                return;
            }
            auto bundleA = fixtureBundlePath("fixture-ca.pem");
            auto peerSession = g_fixturePeer->page->sessionID();
            owner->log("fixture-tls-snapshot-a",
                !bundleA.isEmpty() && TlsPolicyUWP::snapshotSessionBundle(peerSession, bundleA));
            auto* snapshotA = TlsPolicyUWP::snapshotFor(peerSession);
            owner->log("fixture-tls-snapshot-hash", snapshotA && !snapshotA->sha256Hex.isEmpty());
            // A later different bundle for the same session is ignored: the
            // snapshot is immutable, so concurrent controls cannot rebind it.
            auto bundleB = fixtureBundlePath("fixture-unrelated-ca.pem");
            String hashBefore = snapshotA ? snapshotA->sha256Hex : String();
            bool rebinding = TlsPolicyUWP::snapshotSessionBundle(peerSession, bundleB);
            auto* snapshotAfter = TlsPolicyUWP::snapshotFor(peerSession);
            owner->log("fixture-tls-immutable",
                rebinding && snapshotAfter && snapshotAfter->sha256Hex == hashBefore);
            // Owner HTTPS with the package bundle fails closed: the fixture
            // CA is not in it, so verification must reject.
            ResourceRequest ownerRequest;
            ownerRequest.setURL(URL { makeString(fixtureTlsBase(owner->fixtureBase), "index.html"_s) });
            owner->fixtureLoopId = owner->navigation.beginNavigation();
            owner->fixtureNavigationId = owner->fixtureLoopId;
            owner->fixtureNavigationGeneration = owner->navigation.documentGeneration();
            owner->frame().loader().load(FrameLoadRequest(owner->frame(), WTF::move(ownerRequest)));
            scheduleFixtureSuite(owner, FixtureSuitePhaseTlsOwnerHttps);
            return;
        }
        case FixtureSuitePhaseTlsOwnerHttps:
            if (outcome(owner->fixtureLoopId) != NavigationOutcomeUWP::ProvisionalFailure) {
                wait();
                return;
            }
            scheduleFixtureSuite(owner, FixtureSuitePhaseTlsOwnerStable);
            return;
        case FixtureSuitePhaseTlsOwnerStable: {
            // The package bundle fails closed for the fixture server: the
            // fixture CA is not in it, so verification must reject. This also
            // proves no native-store augmentation sneaks the private CA in.
            owner->log("fixture-tls-owner-fails-closed", owner->navigation.transportError() == 60);
            g_fixturePeer->navigation.beginNavigation();
            ResourceRequest peerRequest;
            peerRequest.setURL(URL { makeString(fixtureTlsBase(owner->fixtureBase), "index.html"_s) });
            auto& peerFrame = g_fixturePeer->frame();
            peerFrame.loader().load(FrameLoadRequest(peerFrame, WTF::move(peerRequest)));
            scheduleFixtureSuite(owner, FixtureSuitePhaseTlsPeerHttps);
            return;
        }
        case FixtureSuitePhaseTlsPeerHttps:
            scheduleFixtureSuite(owner, FixtureSuitePhaseTlsPeerStable);
            return;
        case FixtureSuitePhaseTlsPeerStable: {
            auto peerANavId = g_fixturePeer->navigation.activeNavigationId();
            bool peerACommitted = g_fixturePeer->navigation.terminalOutcome(peerANavId) == NavigationOutcomeUWP::Committed;
            if (!peerACommitted) {
                wait();
                return;
            }

            // Bundle A verifies the fixture server over real TLS.
            owner->log("fixture-tls-peerA-committed", WKPageLogTrueUWP);
            scheduleFixtureSuite(owner, FixtureSuitePhaseTlsPeerBCreate);
            return;
        }
        case FixtureSuitePhaseTlsPeerBCreate:
            g_fixturePeerB = createIsolationPeer(owner->fixtureBase);
            if (!g_fixturePeerB) {
                owner->log("fixture-suite-completed", WKPageLogFalseUWP);
                return;
            }
            scheduleFixtureSuite(owner, FixtureSuitePhaseTlsPeerBLoad);
            return;
        case FixtureSuitePhaseTlsPeerBLoad: {
            auto* peerBDocument = g_fixturePeerB && g_fixturePeerB->page && g_fixturePeerB->page->localMainFrame()
                ? g_fixturePeerB->page->localMainFrame()->document()
                : nullptr;
            bool peerBReady = peerBDocument && peerBDocument->url().path() == "/index.html"_s
                && peerBDocument->readyState() == Document::ReadyState::Complete;
            if (!peerBReady) {
                wait();
                return;
            }

            auto bundleB = fixtureBundlePath("fixture-unrelated-ca.pem");
            owner->log("fixture-tls-snapshot-b",
                !bundleB.isEmpty() && TlsPolicyUWP::snapshotSessionBundle(g_fixturePeerB->page->sessionID(), bundleB));
            scheduleFixtureSuite(owner, FixtureSuitePhaseTlsSuspend);
            return;
        }
        case FixtureSuitePhaseTlsSuspend: {
            // Suspend with both peers' HTTPS loads in flight so they
            // handshake and transfer concurrently under opposite bundles.
            auto strategyHandle = platformStrategies()->loaderStrategy();
            auto* strategy = static_cast<LoaderStrategyUWP*>(strategyHandle.get());
            strategy->suspendPendingRequests();
            g_fixturePeer->navigation.beginNavigation();
            ResourceRequest peerARequest;
            peerARequest.setURL(URL { makeString(fixtureTlsBase(owner->fixtureBase), "index.html"_s) });
            auto& peerAFrame = g_fixturePeer->frame();
            peerAFrame.loader().load(FrameLoadRequest(peerAFrame, WTF::move(peerARequest)));
            g_fixturePeerB->navigation.beginNavigation();
            ResourceRequest peerBRequest;
            peerBRequest.setURL(URL { makeString(fixtureTlsBase(owner->fixtureBase), "index.html"_s) });
            auto& peerBFrame = g_fixturePeerB->frame();
            peerBFrame.loader().load(FrameLoadRequest(peerBFrame, WTF::move(peerBRequest)));
            scheduleFixtureSuite(owner, FixtureSuitePhaseTlsResume, kInitialFixtureRetry, 1500_ms);
            return;
        }
        case FixtureSuitePhaseTlsResume: {
            auto strategyHandle = platformStrategies()->loaderStrategy();
            auto* strategy = static_cast<LoaderStrategyUWP*>(strategyHandle.get());
            strategy->resumePendingRequests();
            scheduleFixtureSuite(owner, FixtureSuitePhaseTlsConcurrentStable);
            return;
        }
        case FixtureSuitePhaseTlsConcurrentStable: {
            auto peerANavId = g_fixturePeer->navigation.activeNavigationId();
            auto peerBNavId = g_fixturePeerB->navigation.activeNavigationId();
            bool peerACommitted = g_fixturePeer->navigation.terminalOutcome(peerANavId) == NavigationOutcomeUWP::Committed;
            bool peerBFailed = g_fixturePeerB->navigation.terminalOutcome(peerBNavId) == NavigationOutcomeUWP::ProvisionalFailure
                && g_fixturePeerB->navigation.transportError() == 60;
            if (!peerACommitted || !peerBFailed) {
                wait();
                return;
            }

            // Opposite outcomes under one suspension: bundle A verifies while
            // bundle B fails closed, sharing only timing, never trust.
            owner->log("fixture-tls-concurrent-owner", WKPageLogTrueUWP);
            owner->log("fixture-tls-concurrent-peer", WKPageLogTrueUWP);
            scheduleFixtureSuite(owner, FixtureSuitePhaseTlsFontConflict);
            return;
        }
        case FixtureSuitePhaseTlsFontConflict: {
            // A conflicting font directory fails before any page creation
            // instead of silently changing live pages.
            unsigned result = engineEnsureInitializedUWP(L"C:\\nonexistent-fonts-4-11", owner->log);
            owner->log("fixture-tls-font-conflict", result == FirstPageResultConfigurationConflictUWP);
            scheduleFixtureSuite(owner, FixtureSuitePhaseTlsTeardown);
            return;
        }
        case FixtureSuitePhaseTlsTeardown:
            // The single teardown point in Done drops trust snapshots with
            // everything else; ending on plain HTTP leaves a clean document.
            navigate("index.html"_s, FixtureSuitePhaseDone);
            return;
        case FixtureSuitePhaseDone: {
            if (!at("/index.html"_s)) {
                wait();
                return;
            }
            owner->log("fixture-navigation-replaced-final",
                outcome(owner->fixtureRaceId) == NavigationOutcomeUWP::Replaced);
            // The closing remote command invalidated the refailure decision.
            uint64_t doneIssuer = 0;
            uint64_t doneDecision = 0;
            CertificateExceptionStoreUWP::issuedToken(owner->page->sessionID(), doneIssuer, doneDecision);
            owner->log("fixture-cert-remote-invalidated", !doneIssuer && !doneDecision);
            // The cert peer's session vanishes with it, as the isolation
            // probe proved for cookie sessions.
            bool peerTornDown = false;
            if (g_fixturePeer) {
                auto certPeerSession = g_fixturePeer->page->sessionID();
                delete g_fixturePeer;
                g_fixturePeer = nullptr;
                peerTornDown = !storageSessionUWP(certPeerSession)
                    && !TlsPolicyUWP::snapshotFor(certPeerSession);
            }
            owner->log("fixture-cert-peer-torn-down", peerTornDown);
            // The second peer vanishes with its unrelated-bundle snapshot too.
            bool peerBTornDown = false;
            if (g_fixturePeerB) {
                auto peerBSession = g_fixturePeerB->page->sessionID();
                delete g_fixturePeerB;
                g_fixturePeerB = nullptr;
                peerBTornDown = !storageSessionUWP(peerBSession)
                    && !TlsPolicyUWP::snapshotFor(peerBSession);
            }
            owner->log("fixture-tls-peerB-torn-down", peerBTornDown);
            // Trust teardown rides the same deletion: the peer snapshot is
            // gone while the owner snapshot keeps its frozen hash.
            auto* ownerSnapshot = TlsPolicyUWP::snapshotFor(owner->page->sessionID());
            owner->log("fixture-tls-teardown-intact",
                ownerSnapshot && ownerSnapshot->sha256Hex == owner->fixtureTlsOwnerHash);
            owner->log("fixture-suite-completed", WKPageLogTrueUWP);
            return;
        }
        }
    });
}

} // namespace

// Binds the loader client to its frame and owning host.
FrameLoaderClientUWP::FrameLoaderClientUWP(FrameLoader& loader, PageHostUWP& owner)
    : EmptyFrameLoaderClient(loader)
    , m_owner(owner)
{
}

// Always hosted; enables WebView-specific loader paths.
bool FrameLoaderClientUWP::hasWebView() const
{
    return true;
}

// Only http(s), data: and about: URLs load on this port.
bool FrameLoaderClientUWP::canHandleRequest(const ResourceRequest& request) const
{
    return request.url().protocolIsInHTTPFamily() || request.url().protocolIsData() || request.url().protocolIsAbout();
}

// Defers to the shared MIME registry.
bool FrameLoaderClientUWP::canShowMIMEType(const String& type) const
{
    return MIMETypeRegistry::canShowMIMEType(type);
}

// Phone-shaped UA string identifying this port.
String FrameLoaderClientUWP::userAgent(const URL&) const
{
    return "Mozilla/5.0 (Windows Phone 10.0; ARM) AppleWebKit/605.1.15 WebKitWebView/1.0"_s;
}

// Intercepts the proceed link, consults the host veto, then allows handled schemes.
void FrameLoaderClientUWP::dispatchDecidePolicyForNavigationAction(const NavigationAction&,
    const ResourceRequest& request, const ResourceResponse&, FormState*, const String&,
    std::optional<NavigationIdentifier>, std::optional<HitTestResult>&&, bool, NavigationUpgradeToHTTPSBehavior,
    SandboxFlags, PolicyDecisionMode, FramePolicyFunction&& completion)
{
    if (m_owner.log) {
        auto stage = makeString("navigation-policy: "_s, request.url().string()).utf8();
        m_owner.log(stage.data(), canHandleRequest(request));
    }

    // Reserved document URLs never grant consent: the minibrowser proceed
    // link fails closed here. Consent arrives only through an explicitly
    // consumed certificate token (task 4.8); the shell migrates to it.
    if (request.url().protocolIs("minibrowser"_s)) {
        report("navigation-proceed-reserved", WKPageLogFalseUWP);
        completion(PolicyAction::Ignore);
        return;
    }

    // Optional host veto (component NavigationStarting). Runs synchronously
    // here so refusal lands before any transfer. Null allows normally.
    if (m_owner.policyHandler) {
        auto url = request.url().string().utf8();
        if (!m_owner.policyHandler(m_owner.policyContext, url.data())) {
            // The veto preempts every loader signal: the active ID reaches
            // its exactly-once terminal here, so any late failure or commit
            // the loader still delivers lands as a counted duplicate.
            auto vetoedId = m_owner.navigation.activeNavigationId();
            if (m_owner.navigation.cancelNavigation(NavigationOutcomeUWP::Vetoed))
                report("navigation-vetoed", vetoedId);
            completion(PolicyAction::Ignore);
            return;
        }
    }
    completion(canHandleRequest(request) ? PolicyAction::Use : PolicyAction::Ignore);
}

// New windows are never opened on this port.
void FrameLoaderClientUWP::dispatchDecidePolicyForNewWindowAction(const NavigationAction&, const ResourceRequest&,
    FormState*, const String&, std::optional<HitTestResult>&&, FramePolicyFunction&& completion)
{
    completion(PolicyAction::Ignore);
}

// Attachments never display inline; anything viewable loads. A refused
// main-document response is a veto terminal for the active ID, recorded
// synchronously so the stopped load cannot succeed later.
void FrameLoaderClientUWP::dispatchDecidePolicyForResponse(const ResourceResponse& response, const ResourceRequest&,
    const String&, FramePolicyFunction&& completion)
{
    bool use = !response.isAttachment() && canShowMIMEType(response.mimeType());
    if (!use) {
        auto vetoedId = m_owner.navigation.activeNavigationId();
        if (m_owner.navigation.cancelNavigation(NavigationOutcomeUWP::Vetoed))
            report("navigation-response-vetoed", vetoedId);
    }
    completion(use ? PolicyAction::Use : PolicyAction::Ignore);
}

// Counts the provisional load so stale marker probes drop themselves. Any
// new main-frame attempt supersedes consent-pending state, except the single
// provisional a token-associated warning show armed preservation for.
void FrameLoaderClientUWP::dispatchDidStartProvisionalLoad()
{
    if (m_owner.page && !CertificateExceptionStoreUWP::takePreservePending(m_owner.page->sessionID()))
        CertificateExceptionStoreUWP::invalidatePending(m_owner.page->sessionID());
    m_owner.navigation.noteProvisionalLoad();
    report("navigation-started", WKPageLogFalseUWP);
}

// Retires the recorded failure: only the current failure can back a proceed gesture.
void FrameLoaderClientUWP::dispatchDidCommitLoad(
    std::optional<HasInsecureContent>, std::optional<UsedLegacyTLS>, std::optional<WasPrivateRelayed>)
{
    // Every commit here is a new document: same-document updates never commit,
    // so the generation classifies exactly what the contract requires.
    auto* document = m_owner.frame().document();
    m_owner.navigation.noteDocumentCommit();
    if (document)
        m_owner.navigation.setSource(document->url().string());
    // Transient lifecycle: an about:blank commit shows the substitute
    // document (only transient loads use about:blank on this port); any
    // other commit dismisses it back to Idle as an ordinary history load.
    if (document && document->url().protocolIs("about"_s))
        m_owner.transient.noteCommitted();
    else
        m_owner.transient.noteDismissed();
    report("navigation-committed", WKPageLogFalseUWP);
    report("navigation-committed-id", m_owner.navigation.lastCommitId());
    if (document && m_owner.log) {
        auto stage = makeString("navigation-committed-url: "_s, document->url().string()).utf8();
        m_owner.log(stage.data(), WKPageLogFalseUWP);
    }
}

// Reports completion and kicks the settle probes for async page scripts.
// Finishing never mutates the terminal ledger: a finish for an ID that
// already reached a non-commit terminal is a late success and is reported
// instead of recorded, so canceled IDs stay canceled.
void FrameLoaderClientUWP::dispatchDidFinishLoad()
{
    report("navigation-completed", WKPageLogFalseUWP);
    if (m_owner.navigation.noteLoadFinished())
        report("navigation-late-success", m_owner.navigation.activeNavigationId());
    if (auto* document = m_owner.frame().document(); document && m_owner.log) {
        auto stage = makeString("navigation-completed-url: "_s, document->url().string()).utf8();
        m_owner.log(stage.data(), WKPageLogFalseUWP);
    }
    auto generation = m_owner.navigation.loadGeneration();
    probeFixtureMarkers(m_owner, "load");
    scheduleSettleProbe(&m_owner, generation, kInitialSettleProbeStep);
    auto* document = m_owner.frame().document();
    if (document && !m_owner.fixtureSuiteStarted && document->url().query().contains("uwp-suite=1"_s)) {
        m_owner.fixtureSuiteStarted = true;
        m_owner.fixtureBase = URL(document->url(), "/"_s).string();
        scheduleFixtureSuite(&m_owner, FixtureSuitePhaseIndexProbes);
    }
    RefPtr next = document ? document->getElementById(AtomString { "next"_s }) : nullptr;
    if (!next || !m_owner.log)
        return;
    auto rect = next->boundingClientRect();
    auto hit = m_owner.frame().hitTestResultAtPoint(roundedIntPoint(rect.center()), HitTestRequest::Type::ReadOnly);
    report("link-center-x", static_cast<unsigned long>(rect.center().x()));
    report("link-center-y", static_cast<unsigned long>(rect.center().y()));
    report("link-center-url-length", hit.absoluteLinkURL().string().length());
    if (auto* node = hit.targetNode()) {
        auto stage = makeString("link-center-node: "_s, node->nodeName()).utf8();
        m_owner.log(stage.data(), WKPageLogFalseUWP);
    }
}

// Records the main-document HTTP status so hosts can distinguish an error
// document that rendered from a transport failure. Uses the response's own
// status value; no status is invented when the load never got one.
void FrameLoaderClientUWP::dispatchDidReceiveResponse(
    DocumentLoader*, ResourceLoaderIdentifier, const ResourceResponse& response)
{
    m_owner.navigation.setHttpStatus(true, response.httpStatusCode());
}

// Counts server redirect hops for the active navigation so a host can
// report that a navigation was redirected without scraping log lines.
void FrameLoaderClientUWP::dispatchDidReceiveServerRedirectForProvisionalLoad()
{
    m_owner.navigation.noteRedirect();
}

// Captures the committed document title as reported by the loader.
void FrameLoaderClientUWP::dispatchDidReceiveTitle(const StringWithDirection& title)
{
    m_owner.navigation.setTitle(title.string);
}

// Fragment-only change: same document, new anchor. Replaces inferring this
// from URL equality, which also matched same-URL reloads.
void FrameLoaderClientUWP::dispatchDidChangeLocationWithinPage()
{
    m_owner.navigation.setChange(WKDocumentChangeKindFragmentUWP);
}

// pushState mutated the URL and added a history entry.
void FrameLoaderClientUWP::dispatchDidPushStateWithinPage()
{
    m_owner.navigation.setChange(WKDocumentChangeKindPushStateUWP);
}

// replaceState mutated the URL without adding a history entry.
void FrameLoaderClientUWP::dispatchDidReplaceStateWithinPage()
{
    m_owner.navigation.setChange(WKDocumentChangeKindReplaceStateUWP);
}

// History traversal restored an existing same-document entry.
void FrameLoaderClientUWP::dispatchDidPopStateWithinPage()
{
    m_owner.navigation.setChange(WKDocumentChangeKindPopStateUWP);
}

// Records provisional failures for the interstitial and the proceed link.
// Cancellation (Stop, replacement) is the Stopped terminal; anything else
// (redirect loops, refused connections, policy failures) is the
// ProvisionalFailure terminal. Repeats for an already-terminal ID are
// dropped and counted, never re-recorded.
void FrameLoaderClientUWP::dispatchDidFailProvisionalLoad(
    const ResourceError& error, WillContinueLoading, WillInternallyHandleFailure)
{
    report("navigation-provisional-error", error.errorCode());
    auto stage = makeString("navigation-provisional-error-url: "_s, error.failingURL().string()).utf8();
    m_owner.log(stage.data(), error.errorCode());
    m_owner.navigation.noteProvisionalFailure(error.isCancellation());
    m_owner.navigation.setTransportError(error.errorCode());
    if (m_owner.page) {
        // Cancellation (Stop, replacement) invalidates consent-pending
        // state; any other failure records a new failure revision that may
        // back exactly one explicitly consumed token.
        if (error.isCancellation())
            CertificateExceptionStoreUWP::invalidatePending(m_owner.page->sessionID());
        else
            CertificateExceptionStoreUWP::noteFailure(m_owner.page->sessionID(), error.failingURL());
    }
    m_owner.navigation.setFailedUrl(error.failingURL().string());
}

// Records committed-load failures such as teardown races. A failure after
// commit is its own terminal, kept exactly once like all the others. A
// committed failure names its document: only the current source can fail
// the active navigation, so a failure for an older document is a counted
// stale signal, never a terminal for the newer ID that replaced it.
void FrameLoaderClientUWP::dispatchDidFailLoad(const ResourceError& error)
{
    report("navigation-error", error.errorCode());
    auto stage = makeString("navigation-error-url: "_s, error.failingURL().string()).utf8();
    m_owner.log(stage.data(), error.errorCode());
    const String& source = m_owner.navigation.source();
    if (source.isEmpty() || error.failingURL().string() == source)
        m_owner.navigation.noteCommittedFailure();
    else
        m_owner.navigation.noteStaleSignal();
}

// History traversals always run; same-document forks are handled by core.
ShouldGoToHistoryItem FrameLoaderClientUWP::shouldGoToHistoryItem(HistoryItem&, IsSameDocumentNavigation) const
{
    return ShouldGoToHistoryItem::Yes;
}

// Delegates item-tree creation to the frame history.
RefPtr<HistoryItem> FrameLoaderClientUWP::createHistoryItemTree(
    bool clipAtTarget, BackForwardItemIdentifier itemID) const
{
    auto& frame = m_owner.frame();
    return frame.loader().history().createItemTree(frame, clipAtTarget, itemID);
}

// EmptyFrameLoaderClient drops every delivered chunk on the floor, so
// m_gotFirstByte never becomes true and DocumentLoader::finishedLoading()
// commits an empty placeholder Document at the end of the load. That is why
// a fully transferred page committed and finished but rendered blank.
// commitData() is WebCore's own writer pipeline: it records the first byte,
// calls DocumentWriter::begin() with the document URL and appends the data.

// Feeds delivered bytes to the writer; counts them for the parse report.
void FrameLoaderClientUWP::committedLoad(DocumentLoader* loader, const SharedBuffer& data)
{
    if (!loader)
        return;
    loader->commitData(data);
    m_parsedBytes += data.size();
    report("document-bytes-parsed", static_cast<unsigned long>(data.size()));
    report("document-bytes-total", static_cast<unsigned long>(m_parsedBytes));
}

// Lays out the finished document; marker probing waits for DOMContentLoaded instead.
void FrameLoaderClientUWP::finishedLoading(DocumentLoader* loader)
{
    // DocumentLoader::finishedLoading() closes the writer itself.
    if (!loader)
        return;
    auto* frame = loader->frame();
    if (frame && frame->view())
        frame->view()->updateLayoutAndStyleIfNeededRecursive();
    report("document-parsed-total", static_cast<unsigned long>(m_parsedBytes));
    // Fixture markers are deliberately NOT probed here. See
    // fixtureBodyText(): <body> does not exist yet at this point.
}

// Marks dirty and emits one diagnostic line when a sink is installed.
void FrameLoaderClientUWP::report(const char* stage, unsigned long result)
{
    m_owner.dirty = true;
    if (m_owner.log)
        m_owner.log(stage, result);
}

} // namespace WebCore
