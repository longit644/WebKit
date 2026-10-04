#include "config.h"
#include "PageUWP.h"
#include "BackForwardClient.h"
#include "BackForwardController.h"
#include "ClipboardUWP.h"
#include "ContainerNodeInlines.h"
#include "CookieJar.h"
#include "CurlContext.h"
#include "Document.h"
#include "DocumentLoader.h"
#include "DocumentView.h"
#include "DocumentWriter.h"
#include "EmptyClients.h"
#include "EmptyFrameLoaderClient.h"
#include "EventHandler.h"
#include "Font.h"
#include "FontCascadeInlines.h"
#include "FrameDestructionObserverInlines.h"
#include "FrameLoadRequest.h"
#include "FrameLoader.h"
#include "HTMLImageElement.h"
#include "HandleUserInputEventResult.h"
#include "HistoryController.h"
#include "HistoryItem.h"
#include "LoaderStrategyUWP.h"
#include "MIMETypeRegistry.h"
#include "NetworkStorageSession.h"
#include "RenderElement.h"
#include "ResourceError.h"
#include "ResourceRequest.h"
#include "ResourceResponse.h"
#include "StorageSessionProvider.h"
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
#include "TextureMapperCompositorUWP.h"
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

namespace WebCore {
static HashMap<PAL::SessionID, NetworkStorageSession*>& storageSessionsUWP()
{
    static NeverDestroyed<HashMap<PAL::SessionID, NetworkStorageSession*>> sessions;
    return sessions;
}

// Returns the page-owned cookie session for this ephemeral ID, or null.
NetworkStorageSession* storageSessionUWP(PAL::SessionID sessionID)
{
    return storageSessionsUWP().get(sessionID);
}

// In-memory cookie session registered for one page lifetime; unregisters on teardown.
class StorageSessionProviderUWP final : public StorageSessionProvider {
public:
    // Creates the session and registers it for the page lifetime.
    explicit StorageSessionProviderUWP(PAL::SessionID sessionID)
        : m_session(makeUnique<NetworkStorageSession>(sessionID))
    {
        storageSessionsUWP().add(sessionID, m_session.get());
    }

    // Unregisters the session so no stale cookies survive the page.
    ~StorageSessionProviderUWP() final
    {
        storageSessionsUWP().remove(m_session->sessionID());
    }

    // Session handed to the cookie jar for this page.
    NetworkStorageSession* storageSession() const final { return m_session.get(); }

private:
    // Owned session backing DOM and transport cookies.
    std::unique_ptr<NetworkStorageSession> m_session;
};

}

namespace {

// In-memory session history: appending truncates the forward branch and the
// list is capped, so Back/Forward/Reload work without persistent storage.
class BackForwardClientUWP final : public BackForwardClient {
public:
    // Creates an empty history with no current item.
    static Ref<BackForwardClient> create() { return adoptRef(*new BackForwardClientUWP); }

private:
    // Appends after the current item, truncating any forward branch first.
    void addItem(Ref<HistoryItem>&& item) final
    {
        if (m_hasCurrent) {
            while (m_entries.size() > m_current + 1)
                m_entries.takeLast();
        }

        // Oldest entries fall off; history never grows without bound.
        constexpr size_t kMaxHistoryEntries = 100;
        if (m_entries.size() >= kMaxHistoryEntries) {
            m_entries.removeAt(0);
            if (m_hasCurrent && m_current > 0)
                --m_current;
        }
        if (!m_hasCurrent) {
            m_entries.append(WTF::move(item));
            m_current = 0;
        } else {
            m_entries.insert(m_current + 1, WTF::move(item));
            ++m_current;
        }
        m_hasCurrent = true;
    }

    // Replaces the child item of the matching frame entry.
    void setChildItem(BackForwardFrameItemIdentifier frameItemID, Ref<HistoryItem>&& item) final
    {
        for (auto& entry : m_entries) {
            if (entry->frameItemID() == frameItemID) {
                entry->setChildItem(item.copyRef());
                return;
            }
        }
    }

    // Moves the current pointer to the given entry; unknown items are ignored.
    void goToItem(HistoryItem& item) final
    {
        for (size_t i = 0; i < m_entries.size(); ++i) {
            if (m_entries[i].ptr() == &item) {
                m_current = i;
                m_hasCurrent = true;
                return;
            }
        }
    }

    // Returns every entry for history traversal queries.
    Vector<Ref<HistoryItem>> allItems(FrameIdentifier) final { return m_entries; }

    // Returns the entry at a relative offset; null when out of range.
    RefPtr<HistoryItem> itemAtIndex(int index, FrameIdentifier) final
    {
        if (!m_hasCurrent)
            return nullptr;
        if (index < -static_cast<int>(m_current) || index > static_cast<int>(forwardListCount()))
            return nullptr;
        return m_entries[index + m_current].copyRef();
    }

    // Entries behind the current one; zero before the first navigation.
    unsigned backListCount() const final { return m_hasCurrent ? static_cast<unsigned>(m_current) : 0; }

    // Entries ahead of the current one; zero at the newest navigation.
    unsigned forwardListCount() const final { return m_hasCurrent ? static_cast<unsigned>(m_entries.size() - m_current - 1) : 0; }

    // True when the item belongs to this history.
    bool containsItem(const HistoryItem& item) const final
    {
        for (auto& entry : m_entries) {
            if (entry.ptr() == &item)
                return true;
        }
        return false;
    }

    // Clears every entry and resets the current pointer.
    void close() final
    {
        m_entries.clear();
        m_current = 0;
        m_hasCurrent = false;
    }

    // Ordered entries, oldest first.
    Vector<Ref<HistoryItem>> m_entries;

    // Index of the current entry within the list above.
    size_t m_current { 0 };

    // False until the first navigation lands.
    bool m_hasCurrent { false };
};

// Mutable engine-side page state behind the WK*PageUWP entry points; owned by
// the creating thread and torn down with the page.
struct PageHostUWP {
    // Backing WebCore page; null after teardown.
    RefPtr<Page> page;

    // Creating engine thread id; every entry point revalidates against it.
    DWORD owner { GetCurrentThreadId() };

    // Last accepted control size in CSS pixels.
    unsigned width, height;

    // Diagnostic sink installed at creation; null means quiet.
    WKPageLogUWP log;

    // Set when layer, scroll or content changes need a repaint.
    bool dirty { true };

    // GPU compositor when enabled, otherwise null (software paint).
    std::unique_ptr<TextureMapperCompositorUWP> compositor;

    // Whether the compositor path currently presents frames.
    bool compositorActive { false };

    // Device scale the compositor was enabled with.
    double compositorScale { 1 };

    // Removes session exceptions, detaches the compositor and releases the page.
    ~PageHostUWP()
    {
        // Session-scoped certificate exceptions die with the page.
        if (page)
            removeProceedExceptions(page->sessionID());
        if (compositor) {
            compositor->makeCurrent();
            compositor->setRootLayer(nullptr);
        }
        page = nullptr;
        compositor.reset();
    }

    // True on the creating engine thread; every entry point revalidates.
    bool validThread() const { return owner == GetCurrentThreadId(); }

    // Main frame of the backing page; valid while page is set.
    LocalFrame& frame() const { return *page->localMainFrame(); }

    // Bumped per provisional load so deferred marker probes belonging to a
    // replaced document are dropped instead of reporting into the new page.
    unsigned loadGeneration { 0 };

    // Deferred diagnostics must also expire when the owning page is destroyed.
    std::shared_ptr<unsigned> probeLifetime { std::make_shared<unsigned>(0) };

    // Whether the temporary fixture suite has started for this page.
    bool fixtureSuiteStarted { false };

    // Whether the suite's history-back step succeeded.
    bool fixtureBackSucceeded { false };

    // Fixture server origin the suite navigates relative to.
    String fixtureBase;

    // Load generation captured before the suite's reload step.
    unsigned fixtureReloadGeneration { 0 };

    // Scroll offset captured before the suite's scroll-restoration step.
    int fixtureScrollBefore { 0 };

    // Failing main-frame URL backing the interstitial's proceed link. Cleared
    // on every successful commit, so a stale failure can never arm a bypass.
    String lastFailedNavUrl;

    // Optional host veto consulted on every navigation policy check, after
    // the proceed interception. Null means allow per the normal rules.
    WKNavigationPolicyCallback policyHandler { nullptr };

    // Opaque context passed through to the policy handler.
    void* policyContext { nullptr };
};

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
        if (owner->loadGeneration != generation || !owner->page)
            return;
        probeFixtureMarkers(*owner, "settle");
        scheduleSettleProbe(owner, generation, step + kSettleProbeStepIncrement);
    });
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

    // Verifies forward restored the replaced query, then returns to index.
    FixtureSuitePhaseReplaceForward = 27,

    // Verifies the final landing and reports suite completion.
    FixtureSuitePhaseDone = 28
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
            owner->frame().loader().load(FrameLoadRequest(owner->frame(), WTF::move(request)));
            scheduleFixtureSuite(owner, next);
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
            owner->log("fixture-suite-history-forward", owner->page->backForward().goForward());
            scheduleFixtureSuite(owner, FixtureSuitePhaseHistoryReload);
            return;
        case FixtureSuitePhaseHistoryReload:
            if (!at("/second.html"_s)) {
                wait();
                return;
            }
            owner->log("fixture-suite-history-forward-document", WKPageLogTrueUWP);
            owner->fixtureReloadGeneration = owner->loadGeneration;
            owner->frame().loader().reload();
            owner->log("fixture-suite-history-reload-requested", WKPageLogTrueUWP);
            scheduleFixtureSuite(owner, FixtureSuitePhaseReloadDocument, kInitialFixtureRetry, kFixtureReloadProbeDelay);
            return;
        case FixtureSuitePhaseReloadDocument:
            if (!at("/second.html"_s) || owner->loadGeneration == owner->fixtureReloadGeneration) {
                wait();
                return;
            }
            owner->log("fixture-suite-history-reload-document", text.contains("Second page"_s));
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
            navigate("index.html"_s, FixtureSuitePhaseDone);
            return;
        }
        case FixtureSuitePhaseDone:
            if (!at("/index.html"_s)) {
                wait();
                return;
            }
            owner->log("fixture-suite-completed", WKPageLogTrueUWP);
            return;
        }
    });
}

// Chrome client forwarding invalidation, scroll and layer events to the host;
// the wrapper owns the frame loop, so nothing schedules work here.
class PageChromeUWP final : public EmptyChromeClient {
    WTF_MAKE_TZONE_ALLOCATED(PageChromeUWP);

public:
    // Binds the chrome client to its owning host.
    explicit PageChromeUWP(PageHostUWP& owner)
        : m_owner(owner)
    {
    }

private:
    // Marks the view dirty; the wrapper repaints on the next tick.
    void invalidateRootView(const IntRect&) final { m_owner.dirty = true; }

    // Slow scroll falls back to a full invalidation (see scroll below).
    void invalidateContentsForSlowScroll(const IntRect& rect) final { invalidateContentsAndRootView(rect); }

    // Repaints the whole scrolled area; the host keeps no blittable backing store.
    void scroll(const IntSize&, const IntRect& rect, const IntRect& clip) final
    {
        // The TextureMapper host doesn't blit its non-composited backing store.
        // Repaint the complete scrolled area rather than only the exposed strip.
        auto dirtyRect = rect;
        dirtyRect.intersect(clip);
        invalidateContentsAndRootView(dirtyRect);
    }

    // Marks dirty and forwards the rect to the compositor when active.
    void invalidateContentsAndRootView(const IntRect& rect) final
    {
        m_owner.dirty = true;
        if (m_owner.compositorActive)
            m_owner.compositor->invalidate(rect);
    }

    // Installs the layer tree in the compositor and marks dirty.
    void attachRootGraphicsLayer(LocalFrame&, GraphicsLayer* layer) final
    {
        if (m_owner.compositor)
            m_owner.compositor->setRootLayer(layer);
        m_owner.dirty = true;
    }

    // Marks dirty; the wrapper owns the frame loop, so nothing schedules here.
    void triggerRenderingUpdate() final { m_owner.dirty = true; }

    // Marks dirty and claims the update; the wrapper pumps frames on Rendering.
    bool scheduleRenderingUpdate() final
    {
        m_owner.dirty = true;
        return true;
    }

    // Owning host holding the page and compositor.
    PageHostUWP& m_owner;
};
WTF_MAKE_TZONE_ALLOCATED_IMPL(PageChromeUWP);

// Frame loader client owning policy, navigation events, history delegation
// and the temporary fixture suite; all diagnostics funnel through report().
class PageFrameLoaderUWP final : public EmptyFrameLoaderClient {
public:
    // Binds the loader client to its frame and owning host.
    PageFrameLoaderUWP(FrameLoader& loader, PageHostUWP& owner)
        : EmptyFrameLoaderClient(loader)
        , m_owner(owner)
    {
    }

private:
    // Always hosted; enables WebView-specific loader paths.
    bool hasWebView() const final { return true; }

    // Only http(s), data: and about: URLs load on this port.
    bool canHandleRequest(const ResourceRequest& request) const final { return request.url().protocolIsInHTTPFamily() || request.url().protocolIsData() || request.url().protocolIsAbout(); }

    // Defers to the shared MIME registry.
    bool canShowMIMEType(const String& type) const final { return MIMETypeRegistry::canShowMIMEType(type); }

    // Phone-shaped UA string identifying this port.
    String userAgent(const URL&) const final { return "Mozilla/5.0 (Windows Phone 10.0; ARM) AppleWebKit/605.1.15 WebKitWebView/1.0"_s; }

    // Intercepts the proceed link, consults the host veto, then allows handled schemes.
    void dispatchDecidePolicyForNavigationAction(const NavigationAction&, const ResourceRequest& request, const ResourceResponse&, FormState*, const String&, std::optional<NavigationIdentifier>, std::optional<HitTestResult>&&, bool, NavigationUpgradeToHTTPSBehavior, SandboxFlags, PolicyDecisionMode, FramePolicyFunction&& completion) final
    {
        if (m_owner.log) {
            auto stage = makeString("navigation-policy: "_s, request.url().string()).utf8();
            m_owner.log(stage.data(), canHandleRequest(request));
        }

        // Application-layer consented bypass: the warning page's proceed link.
        // Arms a host exception for the recorded failure only, then re-issues
        // the original navigation; the link itself never loads. Anything
        // unexpected fails closed.
        if (request.url().protocolIs("minibrowser"_s)) {
            URL target(m_owner.lastFailedNavUrl);
            if (!m_owner.page || !target.protocolIs("https"_s)) {
                completion(PolicyAction::Ignore);
                return;
            }
            auto& page = *m_owner.page;
            addProceedException(page.sessionID(), target.host().toString());
            if (m_owner.log) {
                auto stage = makeString("history-proceed-exception: "_s, target.host().toString()).utf8();
                m_owner.log(stage.data(), WKPageLogTrueUWP);
            }
            ResourceRequest renewed;
            renewed.setURL(WTF::move(target));
            // Consented re-issue gets a full load window, not the remaining slice.
            constexpr double kProceedTimeoutSeconds = 60;
            renewed.setTimeoutInterval(kProceedTimeoutSeconds);
            m_owner.frame().loader().load(FrameLoadRequest(m_owner.frame(), WTF::move(renewed)));
            completion(PolicyAction::Ignore);
            return;
        }

        // Optional host veto (component NavigationStarting). Runs synchronously
        // here so refusal lands before any transfer. Null allows normally.
        if (m_owner.policyHandler) {
            auto url = request.url().string().utf8();
            if (!m_owner.policyHandler(m_owner.policyContext, url.data())) {
                completion(PolicyAction::Ignore);
                return;
            }
        }
        completion(canHandleRequest(request) ? PolicyAction::Use : PolicyAction::Ignore);
    }

    // New windows are never opened on this port.
    void dispatchDecidePolicyForNewWindowAction(const NavigationAction&, const ResourceRequest&, FormState*, const String&, std::optional<HitTestResult>&&, FramePolicyFunction&& completion) final
    {
        completion(PolicyAction::Ignore);
    }

    // Attachments never display inline; anything viewable loads.
    void dispatchDecidePolicyForResponse(const ResourceResponse& response, const ResourceRequest&, const String&, FramePolicyFunction&& completion) final
    {
        completion(!response.isAttachment() && canShowMIMEType(response.mimeType()) ? PolicyAction::Use : PolicyAction::Ignore);
    }

    // Counts the provisional load so stale marker probes drop themselves.
    void dispatchDidStartProvisionalLoad() final
    {
        ++m_owner.loadGeneration;
        report("navigation-started", WKPageLogFalseUWP);
    }

    // Retires the recorded failure: only the current failure can back a proceed gesture.
    void dispatchDidCommitLoad(std::optional<HasInsecureContent>, std::optional<UsedLegacyTLS>, std::optional<WasPrivateRelayed>) final
    {
        // A successful commit retires any recorded failure: only the current
        // failure can ever back a proceed gesture.
        m_owner.lastFailedNavUrl = String();
        report("navigation-committed", WKPageLogFalseUWP);
        if (auto* document = m_owner.frame().document(); document && m_owner.log) {
            auto stage = makeString("navigation-committed-url: "_s, document->url().string()).utf8();
            m_owner.log(stage.data(), WKPageLogFalseUWP);
        }
    }

    // Reports completion and kicks the settle probes for async page scripts.
    void dispatchDidFinishLoad() final
    {
        report("navigation-completed", WKPageLogFalseUWP);
        if (auto* document = m_owner.frame().document(); document && m_owner.log) {
            auto stage = makeString("navigation-completed-url: "_s, document->url().string()).utf8();
            m_owner.log(stage.data(), WKPageLogFalseUWP);
        }
        auto generation = m_owner.loadGeneration;
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

    // Records provisional failures for the interstitial and the proceed link.
    void dispatchDidFailProvisionalLoad(const ResourceError& error, WillContinueLoading, WillInternallyHandleFailure) final
    {
        report("navigation-provisional-error", error.errorCode());
        auto stage = makeString("navigation-provisional-error-url: "_s, error.failingURL().string()).utf8();
        m_owner.log(stage.data(), error.errorCode());
        m_owner.lastFailedNavUrl = error.failingURL().string();
    }

    // Records committed-load failures such as teardown races.
    void dispatchDidFailLoad(const ResourceError& error) final
    {
        report("navigation-error", error.errorCode());
        auto stage = makeString("navigation-error-url: "_s, error.failingURL().string()).utf8();
        m_owner.log(stage.data(), error.errorCode());
    }

    // History traversals always run; same-document forks are handled by core.
    ShouldGoToHistoryItem shouldGoToHistoryItem(HistoryItem&, IsSameDocumentNavigation) const final { return ShouldGoToHistoryItem::Yes; }

    // Delegates item-tree creation to the frame history.
    RefPtr<HistoryItem> createHistoryItemTree(bool clipAtTarget, BackForwardItemIdentifier itemID) const final
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
    void committedLoad(DocumentLoader* loader, const SharedBuffer& data) override
    {
        if (!loader)
            return;
        loader->commitData(data);
        m_parsedBytes += data.size();
        report("document-bytes-parsed", static_cast<unsigned long>(data.size()));
        report("document-bytes-total", static_cast<unsigned long>(m_parsedBytes));
    }

    // Lays out the finished document; marker probing waits for DOMContentLoaded instead.
    void finishedLoading(DocumentLoader* loader) override
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
    void report(const char* stage, unsigned long result)
    {
        m_owner.dirty = true;
        if (m_owner.log)
            m_owner.log(stage, result);
    }

    // Bytes committed so far; reported per chunk and in total.
    size_t m_parsedBytes { 0 };

    // Owning host holding the page and the diagnostic sink.
    PageHostUWP& m_owner;
};

// Casts the opaque WK handle back to its host; every entry point funnels here.
PageHostUWP* host(void* handle) { return static_cast<PageHostUWP*>(handle); }

// Paint-buffer uniformity tolerance; the compositor needs square pixels.
constexpr double kScaleUniformityToleranceUWP = 0.01;

// Nonempty extents inside the compositor's presentable range.
bool validSize(unsigned width, unsigned height) { return width && height && width <= UWPMaxPageDimension && height <= UWPMaxPageDimension; }
}

// Creates the persistent page, per-page history, cookie session and host clients.
extern "C" __declspec(dllexport) void* __stdcall WKCreatePageUWP(const wchar_t* fonts, unsigned width, unsigned height, WKPageLogUWP log, unsigned* result)
{
    if (!result)
        return nullptr;
    *result = WKPageResultInvalidArgumentUWP;
    if (!validSize(width, height))
        return nullptr;
    *result = WKInitializeEngineUWP(fonts, log);
    if (*result != WKPageResultOkUWP)
        return nullptr;
    auto handle = std::make_unique<PageHostUWP>();
    handle->width = width;
    handle->height = height;
    handle->log = log;
    // DOM and transport cookies share one in-memory session per page. The
    // provider lives with CookieJar, and unregisters when the page is released.
    auto sessionID = PAL::SessionID::generateEphemeralSessionID();
    auto configuration = pageConfigurationWithEmptyClients(std::nullopt, sessionID);
    configuration.backForwardClient = BackForwardClientUWP::create();
    configuration.cookieJar = CookieJar::create(adoptRef(*new StorageSessionProviderUWP(sessionID)));
    configuration.chromeClient = makeUniqueRef<PageChromeUWP>(*handle);
    // App-supplied local HTML has the normal script sandbox policy. External
    // requests remain unsupported by the current local-only loader strategy.
    auto& local = std::get<PageConfiguration::LocalMainFrameCreationParameters>(configuration.mainFrameCreationParameters);
    local.effectiveSandboxFlags = {};
    local.clientCreator = [owner = handle.get()](LocalFrame&, FrameLoader& loader) -> UniqueRef<LocalFrameLoaderClient> {
        return makeUniqueRefWithoutRefCountedCheck<PageFrameLoaderUWP>(loader, *owner);
    };
    auto page = Page::create(WTF::move(configuration));
    page->settings().setScriptEnabled(true);
    page->settings().setAcceleratedCompositingEnabled(false);
    page->settings().setShouldAllowUserInstalledFonts(true);
    page->settings().setDefaultFontSize(UWPDefaultFontSize);
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
    String bundledFontFamily = String::fromUTF8(UWPDefaultFontFamily);
    page->settings().setStandardFontFamily(bundledFontFamily);
    page->settings().setSerifFontFamily(bundledFontFamily);
    page->settings().setSansSerifFontFamily(bundledFontFamily);
    page->settings().setFixedFontFamily(bundledFontFamily);
    page->settings().setCursiveFontFamily(bundledFontFamily);
    page->settings().setFantasyFontFamily(bundledFontFamily);
    auto* frame = page->localMainFrame();
    if (!frame) {
        *result = WKPageResultNoMainFrameUWP;
        return nullptr;
    }
    frame->setView(LocalFrameView::create(*frame, { static_cast<int>(width), static_cast<int>(height) }));
    frame->init();
    frame->view()->setCanHaveScrollbars(false);
    handle->page = WTF::move(page);
    *result = WKPageResultOkUWP;
    if (log)
        log("persistent-page-created", WKPageLogFalseUWP);
    return handle.release();
}

// Destroys the page on its owning engine thread; null is a successful no-op.
extern "C" __declspec(dllexport) unsigned __stdcall WKDestroyPageUWP(void* handle)
{
    if (!handle)
        return WKPageResultOkUWP;
    if (!host(handle)->validThread())
        return WKPageResultWrongThreadUWP;
    delete host(handle);
    return WKPageResultOkUWP;
}

// Loads app-supplied UTF-8 markup into the active document writer.
extern "C" __declspec(dllexport) unsigned __stdcall WKLoadHTMLUWP(void* handle, const char* utf8)
{
    if (!handle || !utf8)
        return WKPageResultInvalidArgumentUWP;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadUWP;
    auto* loader = view.frame().loader().activeDocumentLoader();
    if (!loader)
        return WKPageResultNoActiveDocumentLoaderUWP;
    auto& writer = loader->writer();
    writer.setMIMEType("text/html"_s);
    if (!writer.begin(URL {}))
        return WKPageResultDocumentWriterBeginFailedUWP;
    writer.insertDataSynchronously(String::fromUTF8(utf8));
    writer.end();
    view.frame().view()->setScrollPosition({ 0, 0 });
    view.frame().view()->updateLayoutAndStyleIfNeededRecursive();
    view.dirty = true;
    if (view.log)
        view.log("persistent-html-loaded", WKPageLogFalseUWP);
    return WKPageResultOkUWP;
}

// Starts an HTTP(S) navigation using the supplied CA bundle and strict TLS verification.
extern "C" __declspec(dllexport) unsigned __stdcall WKNavigatePageUWP(void* handle, const char* utf8, const wchar_t* caPath)
{
    if (!handle || !utf8 || !caPath)
        return WKPageResultInvalidArgumentUWP;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadUWP;
    URL url(String::fromUTF8(utf8));
    if (!url.isValid() || !url.protocolIsInHTTPFamily())
        return WKPageResultInvalidArgumentUWP;
    auto& ssl = CurlContext::singleton().sslHandle();
    ssl.setIgnoreSSLErrors(false);
    ssl.setCACertPath(String(caPath));
    ResourceRequest request;
    request.setURL(WTF::move(url));
    // Main-frame loads get a full minute before the transport gives up.
    constexpr double kNavigationTimeoutSeconds = 60;
    request.setTimeoutInterval(kNavigationTimeoutSeconds);
    view.frame().loader().load(FrameLoadRequest(view.frame(), WTF::move(request)));
    view.dirty = true;
    return WKPageResultOkUWP;
}

// Traverses one step toward the oldest session-history item.
extern "C" __declspec(dllexport) unsigned __stdcall WKGoBackPageUWP(void* handle)
{
    if (!handle)
        return WKPageResultInvalidArgumentUWP;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadUWP;
    bool moved = view.page->backForward().goBack();
    if (view.log)
        view.log("history-go-back", moved);
    view.dirty = true;
    return WKPageResultOkUWP;
}

// Traverses one step toward the newest session-history item.
extern "C" __declspec(dllexport) unsigned __stdcall WKGoForwardPageUWP(void* handle)
{
    if (!handle)
        return WKPageResultInvalidArgumentUWP;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadUWP;
    bool moved = view.page->backForward().goForward();
    if (view.log)
        view.log("history-go-forward", moved);
    view.dirty = true;
    return WKPageResultOkUWP;
}

// Reloads the current history item.
extern "C" __declspec(dllexport) unsigned __stdcall WKReloadPageUWP(void* handle)
{
    if (!handle)
        return WKPageResultInvalidArgumentUWP;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadUWP;
    view.frame().loader().reload();
    if (view.log)
        view.log("history-reload", WKPageLogFalseUWP);
    view.dirty = true;
    return WKPageResultOkUWP;
}

// Stops all loads owned by the current frame.
extern "C" __declspec(dllexport) unsigned __stdcall WKStopPageUWP(void* handle)
{
    if (!handle)
        return WKPageResultInvalidArgumentUWP;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadUWP;
    view.frame().loader().stopAllLoaders();
    if (view.log)
        view.log("history-stop", WKPageLogFalseUWP);
    view.dirty = true;
    return WKPageResultOkUWP;
}

// Installs or clears the synchronous navigation-policy callback.
extern "C" __declspec(dllexport) unsigned __stdcall WKSetNavigationPolicyHandlerPageUWP(void* handle, WKNavigationPolicyCallback callback, void* context)
{
    if (!handle)
        return WKPageResultInvalidArgumentUWP;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadUWP;
    view.policyHandler = callback;
    view.policyContext = callback ? context : nullptr;
    return WKPageResultOkUWP;
}

// Updates the CSS-pixel viewport and lays out the frame.
extern "C" __declspec(dllexport) unsigned __stdcall WKResizePageUWP(void* handle, unsigned width, unsigned height)
{
    if (!handle || !validSize(width, height))
        return WKPageResultInvalidArgumentUWP;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadUWP;
    view.width = width;
    view.height = height;
    if (view.compositorActive)
        view.compositor->resize(width, height, view.compositorScale);
    view.frame().view()->setFrameRect({ 0, 0, static_cast<int>(width), static_cast<int>(height) });
    view.frame().view()->updateLayoutAndStyleIfNeededRecursive();
    view.dirty = true;
    return WKPageResultOkUWP;
}

// Scrolls within the current document's clamped scroll range.
extern "C" __declspec(dllexport) unsigned __stdcall WKScrollPageUWP(void* handle, int dx, int dy)
{
    if (!handle)
        return WKPageResultInvalidArgumentUWP;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadUWP;
    auto& frameView = *view.frame().view();
    frameView.updateLayoutAndStyleIfNeededRecursive();
    auto position = frameView.scrollPosition();
    auto maximum = frameView.maximumScrollPosition();
    int x = static_cast<int>(std::clamp<long long>(static_cast<long long>(position.x()) + dx, 0, std::max(0, maximum.x())));
    int y = static_cast<int>(std::clamp<long long>(static_cast<long long>(position.y()) + dy, 0, std::max(0, maximum.y())));
    frameView.setScrollPosition({ x, y });
    view.dirty = true;
    return WKPageResultOkUWP;
}

// Converts a host pointer event to a WebCore mouse event.
extern "C" __declspec(dllexport) unsigned __stdcall WKPointerPageUWP(void* handle, unsigned type, double x, double y, unsigned modifiers)
{
    if (!handle || type > WKPointerEventReleaseUWP)
        return WKPageResultInvalidArgumentUWP;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadUWP;
    OptionSet<PlatformEvent::Modifier> flags;
    if (modifiers & WKPointerModifierShiftUWP)
        flags.add(PlatformEvent::Modifier::ShiftKey);
    if (modifiers & WKPointerModifierControlUWP)
        flags.add(PlatformEvent::Modifier::ControlKey);
    if (modifiers & WKPointerModifierAltUWP)
        flags.add(PlatformEvent::Modifier::AltKey);
    auto eventType = type == WKPointerEventPressUWP ? PlatformEvent::Type::MousePressed : type == WKPointerEventMoveUWP ? PlatformEvent::Type::MouseMoved
                                                                                                                        : PlatformEvent::Type::MouseReleased;
    if (view.log)
        view.log("tap-stage-enter", type);
    // Click count belongs to press/release, never motion; no keyboard modifier is synthesized.
    constexpr int kNoClickCount = 0;
    constexpr int kPointerClickCount = 1;
    PlatformMouseEvent event({ x, y }, { x, y }, MouseButton::Left, eventType, type == WKPointerEventMoveUWP ? kNoClickCount : kPointerClickCount, flags, MonotonicTime::now(), WKPointerModifierNoneUWP, SyntheticClickType::NoTap, MouseEventInputSource::UserDriven);
    if (view.log)
        view.log("tap-stage-event-created", type);
    if (type != WKPointerEventMoveUWP && view.log) {
        // Report what a tap actually hits before dispatching it. Distinguishes a
        // coordinate-space bug (no/other node found) from a broken event path
        // (correct node, but no navigation follows).
        auto result = view.frame().hitTestResultAtPoint(IntPoint { static_cast<int>(x), static_cast<int>(y) }, HitTestRequest::Type::ReadOnly);
        Node* node = result.targetNode();
        auto* anchor = dynamicDowncast<HTMLAnchorElement>(node);
        if (node) {
            auto stage = makeString("tap-node: "_s, node->nodeName()).utf8();
            view.log(stage.data(), WKPageLogFalseUWP);
        }
        view.log("tap-link-url-length", result.absoluteLinkURL().string().length());
        view.log("tap-node-name-length", node ? static_cast<unsigned long>(node->nodeName().length()) : 0);
        view.log("tap-is-anchor", anchor != nullptr);
        view.log("tap-href-length", anchor ? static_cast<unsigned long>(anchor->href().string().length()) : 0);
        view.log("tap-stage-after-hit-test", type);
    }
    if (type == WKPointerEventPressUWP)
        view.frame().eventHandler().handleMousePressEvent(event);
    else if (type == WKPointerEventMoveUWP)
        view.frame().eventHandler().mouseMoved(event);
    else
        view.frame().eventHandler().handleMouseReleaseEvent(event);
    if (view.log)
        view.log("tap-stage-after-dispatch", type);
    view.dirty = true;
    return WKPageResultOkUWP;
}

// Ticks timers and the rendering pipeline once.
extern "C" __declspec(dllexport) unsigned __stdcall WKTickPageUWP(void* handle)
{
    if (!handle)
        return WKPageResultInvalidArgumentUWP;
    if (!host(handle)->validThread())
        return WKPageResultWrongThreadUWP;
    WTF::RunLoop::cycle();
    if (host(handle)->compositorActive && !host(handle)->compositor->makeCurrent())
        return WKPageResultCompositorUnavailableUWP;
    host(handle)->page->updateRendering();
    host(handle)->page->finalizeRenderingUpdate({});
    return WKPageResultOkUWP;
}

// Installs the owning UI dispatcher so CurlRequest can wake WebCore's main run loop.
extern "C" __declspec(dllexport) long __stdcall WKInstallClipboardDispatcherUWP(void* handle, void* dispatcher)
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
    auto wakePending = std::make_shared<std::atomic<bool>>(false);
    WTF::RunLoop::setWakeUpCallback([ui, wakePending] {
        if (wakePending->exchange(true))
            return;
        auto callback = Microsoft::WRL::Callback<Core::IDispatchedHandler>([wakePending]() -> HRESULT {
            wakePending->store(false);
            WTF::RunLoop::cycle();
            return S_OK;
        });
        Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncAction> action;
        if (!callback || FAILED(ui->RunAsync(Core::CoreDispatcherPriority_Normal, callback.Get(), &action)))
            wakePending->store(false);
    });
    ClipboardUWP::setUIDispatcher([ui](Function<void()>&& task) -> bool {
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
extern "C" __declspec(dllexport) unsigned __stdcall WKPaintPageUWP(void* handle, unsigned char* bgra, size_t capacity, unsigned stride)
{
    // Identity scale: the unscaled entry point paints 1:1.
    return WKPaintPageScaledUWP(handle, bgra, capacity, stride, 1, 1);
}

// Paints the current frame at the requested device scale into caller-owned BGRA memory.
extern "C" __declspec(dllexport) unsigned __stdcall WKPaintPageScaledUWP(void* handle, unsigned char* bgra, size_t capacity, unsigned stride, double scaleX, double scaleY)
{
    if (!handle || !bgra)
        return WKPageResultInvalidArgumentUWP;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadUWP;
    constexpr double kMaxDeviceScale = static_cast<double>(UWPMaxDeviceScale);
    if (!std::isfinite(scaleX) || !std::isfinite(scaleY) || scaleX <= 0 || scaleY <= 0 || scaleX > kMaxDeviceScale || scaleY > kMaxDeviceScale)
        return WKPageResultInvalidArgumentUWP;
    unsigned pixelWidth = static_cast<unsigned>(std::ceil(view.width * scaleX));
    unsigned pixelHeight = static_cast<unsigned>(std::ceil(view.height * scaleY));
    if (pixelWidth > UWPMaxPaintDimension || pixelHeight > UWPMaxPaintDimension || stride < pixelWidth * UWPBytesPerBGRA || stride > UWPMaxPaintStride || capacity < static_cast<size_t>(stride) * pixelHeight)
        return WKPageResultInvalidArgumentUWP;
    if (view.page->deviceScaleFactor() != static_cast<float>(scaleX))
        view.page->setDeviceScaleFactor(scaleX);
    view.frame().view()->updateLayoutAndStyleIfNeededRecursive();
#if USE(CAIRO)
    auto* surface = cairo_image_surface_create_for_data(bgra, CAIRO_FORMAT_ARGB32, pixelWidth, pixelHeight, stride);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return WKPageResultNoPaintSurfaceUWP;
    }
#elif USE(SKIA)
    auto surface = SkSurfaces::WrapPixels(SkImageInfo::Make(pixelWidth, pixelHeight, kBGRA_8888_SkColorType, kPremul_SkAlphaType), bgra, stride);
    if (!surface)
        return WKPageResultNoPaintSurfaceUWP;
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
    return WKPageResultOkUWP;
}

// Enables the page compositor using the supplied native SwapChainPanel.
extern "C" __declspec(dllexport) unsigned __stdcall WKEnableCompositorUWP(void* handle, void* nativePanel, double scaleX, double scaleY)
{
    constexpr double kMaxDeviceScale = static_cast<double>(UWPMaxDeviceScale);
    if (!handle || !nativePanel || !std::isfinite(scaleX) || !std::isfinite(scaleY) || scaleX <= 0 || scaleX > kMaxDeviceScale || std::abs(scaleX - scaleY) > kScaleUniformityToleranceUWP)
        return WKPageResultInvalidArgumentUWP;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadUWP;
    if (view.compositorActive && view.compositorScale == scaleX)
        return WKPageResultOkUWP;
    if (view.compositor)
        return WKPageResultCompositorConfigurationUnsupportedUWP; // Context scale changes need a complete graphics-layer rebuild.
    auto compositor = std::make_unique<TextureMapperCompositorUWP>(view.frame(), view.log);
    if (!compositor->initialize(nativePanel, view.width, view.height, scaleX))
        return WKPageResultCompositorConfigurationUnsupportedUWP;
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
        view.log("gpu-compositor-enabled", WKPageLogFalseUWP);
    return WKPageResultOkUWP;
}

// Disables and releases the page compositor.
extern "C" __declspec(dllexport) unsigned __stdcall WKDisableCompositorUWP(void* handle)
{
    if (!handle)
        return WKPageResultInvalidArgumentUWP;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadUWP;
    if (view.compositor)
        view.compositor->makeCurrent();
    view.page->settings().setForceCompositingMode(false);
    view.page->settings().setAcceleratedCompositingEnabled(false);
    view.frame().view()->updateLayoutAndStyleIfNeededRecursive();
    if (view.compositor)
        view.compositor->setRootLayer(nullptr);
    view.compositorActive = false;
    view.dirty = true;
    return WKPageResultOkUWP;
}

// Paints and presents one compositor frame with optional overscroll.
extern "C" __declspec(dllexport) unsigned __stdcall WKRenderCompositorUWP(void* handle, double overscrollY)
{
    if (!handle || !std::isfinite(overscrollY))
        return WKPageResultInvalidArgumentUWP;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadUWP;
    if (!view.compositorActive || !view.compositor)
        return WKPageResultCompositorConfigurationUnsupportedUWP;
    if (!view.compositor->render(overscrollY))
        return WKPageResultCompositorUnavailableUWP;
    view.dirty = false;
    return WKPageResultOkUWP;
}

// Reports whether engine painting is needed before the next present.
extern "C" __declspec(dllexport) unsigned __stdcall WKPageNeedsPaintUWP(void* handle, unsigned* needsPaint)
{
    if (!handle || !needsPaint)
        return WKPageResultInvalidArgumentUWP;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadUWP;
    view.frame().view()->updateLayoutAndStyleIfNeededRecursive();
    *needsPaint = view.dirty || (view.compositorActive && view.compositor->needsFrame());
    return WKPageResultOkUWP;
}

// Copies viewport, document, scroll and history state into the caller struct.
extern "C" __declspec(dllexport) unsigned __stdcall WKGetPageStateUWP(void* handle, WKPageStateUWP* state)
{
    if (!handle || !state)
        return WKPageResultInvalidArgumentUWP;
    auto& view = *host(handle);
    if (!view.validThread())
        return WKPageResultWrongThreadUWP;
    auto& frameView = *view.frame().view();
    auto position = frameView.scrollPosition();
    auto contents = frameView.contentsSize();
    auto& backForward = view.page->backForward();
    *state = { view.width, view.height, position.x(), position.y(), contents.width(), contents.height(), backForward.backCount() > 0, backForward.forwardCount() > 0 };
    return WKPageResultOkUWP;
}
