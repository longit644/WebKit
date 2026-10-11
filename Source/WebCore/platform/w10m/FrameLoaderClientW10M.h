#pragma once

#include "EmptyFrameLoaderClient.h"
#include "PageHostW10M.h"

// Frame loader client for one page. Moved from PageW10M.cpp in task 4.2.
namespace WebCore {

// Owns policy, navigation events, history delegation and the temporary fixture
// suite; all diagnostics funnel through report(). Unchanged from PageW10M.cpp
// as PageFrameLoaderW10M so page creation, history and rendering keep their
// baseline before any behavioral change; the migration map's planned
// FrameLoaderClientW10M name applies from this move. The fixture free functions
// driven only by this client move with it into FrameLoaderClientW10M.cpp.
class FrameLoaderClientW10M final : public EmptyFrameLoaderClient {
public:
    // Binds the loader client to its frame and owning host.
    FrameLoaderClientW10M(FrameLoader& loader, PageHostW10M& owner);

private:
    // Always hosted; enables WebView-specific loader paths.
    bool hasWebView() const final;

    // Only http(s), data: and about: URLs load on this port.
    bool canHandleRequest(const ResourceRequest& request) const final;

    // Defers to the shared MIME registry.
    bool canShowMIMEType(const String& type) const final;

    // Phone-shaped UA string identifying this port.
    String userAgent(const URL&) const final;

    // Intercepts the proceed link, consults the host veto, then allows handled schemes.
    void dispatchDecidePolicyForNavigationAction(const NavigationAction&, const ResourceRequest& request,
        const ResourceResponse&, FormState*, const String&, std::optional<NavigationIdentifier>,
        std::optional<HitTestResult>&&, bool, NavigationUpgradeToHTTPSBehavior, SandboxFlags, PolicyDecisionMode,
        FramePolicyFunction&& completion) final;

    // New windows are never opened on this port.
    void dispatchDecidePolicyForNewWindowAction(const NavigationAction&, const ResourceRequest&, FormState*,
        const String&, std::optional<HitTestResult>&&, FramePolicyFunction&& completion) final;

    // Attachments never display inline; anything viewable loads.
    void dispatchDecidePolicyForResponse(const ResourceResponse& response, const ResourceRequest&, const String&,
        FramePolicyFunction&& completion) final;

    // Counts the provisional load so stale marker probes drop themselves.
    void dispatchDidStartProvisionalLoad() final;

    // Retires the recorded failure: only the current failure can back a proceed gesture.
    void dispatchDidCommitLoad(
        std::optional<HasInsecureContent>, std::optional<UsedLegacyTLS>, std::optional<WasPrivateRelayed>) final;

    // Reports completion and kicks the settle probes for async page scripts.
    void dispatchDidFinishLoad() final;

    // Records the main-document HTTP status so hosts can distinguish an error
    // document that rendered from a transport failure. Uses the response's own
    // status value; no status is invented when the load never got one.
    void NODELETE dispatchDidReceiveResponse(
        DocumentLoader*, ResourceLoaderIdentifier, const ResourceResponse& response) final;

    // Counts server redirect hops for the active navigation so a host can
    // report that a navigation was redirected without scraping log lines.
    void NODELETE dispatchDidReceiveServerRedirectForProvisionalLoad() final;

    // Captures the committed document title as reported by the loader.
    void NODELETE dispatchDidReceiveTitle(const StringWithDirection& title) final;

    // Fragment-only change: same document, new anchor. Replaces inferring this
    // from URL equality, which also matched same-URL reloads.
    void NODELETE dispatchDidChangeLocationWithinPage() final;

    // pushState mutated the URL and added a history entry.
    void NODELETE dispatchDidPushStateWithinPage() final;

    // replaceState mutated the URL without adding a history entry.
    void NODELETE dispatchDidReplaceStateWithinPage() final;

    // History traversal restored an existing same-document entry.
    void NODELETE dispatchDidPopStateWithinPage() final;

    // Records provisional failures for the interstitial and the proceed link.
    void dispatchDidFailProvisionalLoad(const ResourceError& error, WillContinueLoading, WillInternallyHandleFailure) final;

    // Records committed-load failures such as teardown races.
    void dispatchDidFailLoad(const ResourceError& error) final;

    // History traversals always run; same-document forks are handled by core.
    ShouldGoToHistoryItem shouldGoToHistoryItem(HistoryItem&, IsSameDocumentNavigation) const final;

    // Delegates item-tree creation to the frame history.
    RefPtr<HistoryItem> createHistoryItemTree(bool clipAtTarget, BackForwardItemIdentifier itemID) const final;

    // Feeds delivered bytes to the writer; counts them for the parse report.
    void committedLoad(DocumentLoader* loader, const SharedBuffer& data) override;

    // Lays out the finished document; marker probing waits for DOMContentLoaded instead.
    void finishedLoading(DocumentLoader* loader) override;

    // Marks dirty and emits one diagnostic line when a sink is installed.
    void report(const char* stage, unsigned long result);

    // Bytes committed so far; reported per chunk and in total.
    size_t m_parsedBytes { 0 };

    // Owning host holding the page and the diagnostic sink.
    PageHostW10M& m_owner;
};

} // namespace WebCore

