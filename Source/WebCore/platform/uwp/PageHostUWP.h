#pragma once

#include "LoaderStrategyUWP.h"
#include "NavigationControllerUWP.h"
#include "Page.h"
#include "PageUWP.h"
#include "StorageSessionProviderUWP.h"
#include "TextureMapperCompositorUWP.h"
#include "TransientPageUWP.h"
#include <memory>
#include <wtf/RefPtr.h>
#include <wtf/text/CString.h>
#include <wtf/text/WTFString.h>

// Mutable engine-side page state. Moved from PageUWP.cpp in task 4.2.
namespace WebCore {

// Behind the WK*PageUWP entry points; owned by the creating thread and torn
// down with the page. Navigation identity, generations and typed signals live
// in the controller below (task 4.3); the fixture-suite fields stay here
// because the suite reads them.
struct PageHostUWP {
    // Backing WebCore page; null after teardown.
    RefPtr<Page> page;

    // Host-owned session lifetime; cached CookieJars may retain the provider
    // after page destruction, but teardown explicitly closes its storage.
    RefPtr<StorageSessionProviderUWP> storageProvider;

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

    // Stops loaders, releases the page and its session-scoped resources,
    // and explicitly closes cookie storage even if cached jars outlive us.
    ~PageHostUWP();

    // True on the creating engine thread; every entry point revalidates.
    bool validThread() const { return owner == GetCurrentThreadId(); }

    // Main frame of the backing page; valid while page is set.
    LocalFrame& frame() const { return *page->localMainFrame(); }

    // Navigation identity, generations and typed source/title/HTTP signals.
    NavigationControllerUWP navigation;

    // Transient substitute-data host HTML; history-hidden by construction.
    TransientPageUWP transient { *this };

    // Deferred diagnostics must also expire when the owning page is destroyed.
    std::shared_ptr<unsigned> probeLifetime { std::make_shared<unsigned>(0) };

    // Whether the temporary fixture suite has started for this page.
    bool fixtureSuiteStarted { false };

    // Whether the suite's history-back step succeeded.
    bool fixtureBackSucceeded { false };

    // Navigation ID allocated by the suite's most recent command, for
    // redirect/traversal stability assertions; 0 before the first command.
    uint64_t fixtureNavigationId { 0 };

    // Document generation observed with that allocation, for monotonicity.
    uint64_t fixtureNavigationGeneration { 0 };

    // Fixture server origin the suite navigates relative to.
    String fixtureBase;

    // Load generation captured before the suite's reload step.
    unsigned fixtureReloadGeneration { 0 };

    // Scroll offset captured before the suite's scroll-restoration step.
    int fixtureScrollBefore { 0 };

    // Vetoed navigation ID for the exactly-once veto probe (task 4.4).
    uint64_t fixtureVetoId { 0 };

    // Redirect-loop navigation ID for the provisional-failure probe.
    uint64_t fixtureLoopId { 0 };

    // Download navigation ID for the response-veto probe.
    uint64_t fixtureDownloadId { 0 };

    // Superseded navigation ID for the replacement-race probe: must stay
    // Replaced no matter how late its signals arrive.
    uint64_t fixtureRaceId { 0 };

    // Winning navigation ID of the replacement race; must commit normally.
    uint64_t fixtureRaceGoodId { 0 };

    // Replaced-while-loading navigation ID from the replacement phase: its
    // document committed before replacement, so it must stay Committed.
    uint64_t fixtureReplacedId { 0 };

    // Recorded failure captured before the transient probes, for the
    // warning-preservation versus generic-invalidation contrast.
    String fixtureFailedUrlBefore;

    // History counts captured before the transient probes; substitute
    // documents must not move them.
    unsigned fixtureBackCount { 0 };
    unsigned fixtureForwardCount { 0 };

    // Current history item URL captured before the transient probes; the
    // transient cursor must stay on it.
    String fixtureCurrentUrl;

    // Certificate-decision token halves for the cross-issued probe: the
    // owner's eligible token at the time of its failure.
    uint64_t fixtureCertIssuerA { 0 };
    uint64_t fixtureCertDecisionA { 0 };

    // The peer's eligible token at the time of its failure.
    uint64_t fixtureCertIssuerB { 0 };
    uint64_t fixtureCertDecisionB { 0 };

    // Owner trust-bundle hash captured before peer teardown, for the
    // post-teardown intactness contrast.
    String fixtureTlsOwnerHash;

    // Optional host veto consulted on every navigation policy check, after
    // the proceed interception. Null means allow per the normal rules.
    WKNavigationPolicyCallback policyHandler { nullptr };

    // Opaque context passed through to the policy handler.
    void* policyContext { nullptr };
};

} // namespace WebCore
