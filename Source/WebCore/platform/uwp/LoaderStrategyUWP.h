#pragma once

#include "LoaderStrategy.h"
#include "ResourceError.h"
#include "ResourceResponse.h"
#include <pal/SessionID.h>
#include <wtf/HashMap.h>

namespace WebCore {

class ResourceLoadUWP;
class NetworkStorageSession;

// Adapter-local ResourceError codes (domain "UWPNetwork"); transport failures
// carry the CURLcode instead. FirstPageUWP reuses the same values under its
// own domain, so keep the numbers stable.
enum UWPResourceErrorCode {
    // Superseded or explicitly stopped load; the shell ignores it because the real navigation follows.
    UWPResourceErrorCancelled = 1,

    // Refused by policy, content blocker or loopback isolation; no transport ran.
    UWPResourceErrorBlocked = 2,

    // Refused by an installed content blocker.
    UWPResourceErrorBlockedByContentBlocker = 3,

    // Non-http(s) URL the adapter cannot fetch.
    UWPResourceErrorUnsupportedURL = 4,

    // Load interrupted by a mid-flight policy change; the new decision governs.
    UWPResourceErrorInterruptedForPolicyChange = 5,

    // Response MIME type WebKit cannot display.
    UWPResourceErrorUnsupportedMIMEType = 6,

    // Local file URL with no such file behind it.
    UWPResourceErrorFileDoesNotExist = 7,

    // Redirect chain broken: too many hops, loop or cancelled policy re-issue.
    UWPResourceErrorRedirectFailed = 8,

    // Plain-http navigation refused under the HTTPS-only policy.
    UWPResourceErrorHTTPSOnly = 9,

    // A plugin claimed the load; the engine stands down.
    UWPResourceErrorPluginWillHandleLoad = 10,

    // Single resource grew past the 32 MB transfer budget.
    UWPResourceErrorByteBudgetExceeded = 11,

    // Blocking load attempted; the adapter is async-only.
    UWPResourceErrorSynchronousUnsupported = 12
};

// Owning pages register their session for the single-process transport.
NetworkStorageSession* storageSessionUWP(PAL::SessionID);

// Per-page, session-scoped certificate-exception hosts for the explicit
// user-consented proceed flow (warning page → Advanced → proceed). The bypass
// applies only to the recorded host while its page lives; it is never set
// silently, never persists, and never applies to other hosts. Sessions are
// ephemeral per page in this port, so erasing on page teardown bounds it.
void addProceedException(PAL::SessionID, const String& host);

// Removes all explicitly consented hosts when a page session ends.
void removeProceedExceptions(PAL::SessionID);

// Tests whether this request host has an exception in its own session.
bool hasProceedException(PAL::SessionID, const URL&);

using LoaderStrategyUWPLog = void(__stdcall*)(const char*, unsigned long);

// Installs the transfer-event sink; null silences network logging.
void setLoaderStrategyUWPLog(LoaderStrategyUWPLog);

// Single-process adapter over WebCore's existing asynchronous CurlRequest.
// Snapshot-only clients must not be used as the browser's strategy provider.
class LoaderStrategyUWP final : public LoaderStrategy {
public:
    // Creates and destroys the per-page transport; pages own their sessions.
    LoaderStrategyUWP();

    // Cancels in-flight loads and drops session state.
    ~LoaderStrategyUWP();

    // Starts an async CurlRequest-backed load for a subresource.
    void loadResource(LocalFrame&, CachedResource&, ResourceRequest&&, const ResourceLoaderOptions&, CompletionHandler<void(RefPtr<SubresourceLoader>&&)>&&) final;

    // Rejects blocking loads; the adapter is async-only.
    void loadResourceSynchronously(FrameLoader&, ResourceLoaderIdentifier, const ResourceRequest&, ClientCredentialPolicy, const FetchOptions&, const HTTPHeaderMap&, ResourceError&, ResourceResponse&, Vector<uint8_t>&) final;

    // No-op: session teardown needs no network action.
    void pageLoadCompleted(Page&) final { }

    // No-op: sessions are per-page and die with the page.
    void browsingContextRemoved(LocalFrame&) final { }

    // Drops a finished or cancelled loader from the active map.
    void remove(ResourceLoader*) final;

    // Starts this loader when its deferral ends and the strategy is not suspended.
    void setDefersLoading(ResourceLoader&, bool) final;

    // No-op: cross-origin redirects need no extra bookkeeping here.
    void crossOriginRedirectReceived(ResourceLoader*, const URL&) final { }

    // No-op: requests start immediately, nothing stays pending.
    void servePendingRequests(ResourceLoadPriority = ResourceLoadPriority::VeryLow) final { }

    // Prevents new transfers from starting until the matching resume.
    void suspendPendingRequests() final;

    // Starts pending transfers when the outermost suspension ends.
    void resumePendingRequests() final;

    // Ping loads are disabled; beacons report unsupported.
    bool usePingLoad() const final { return false; }

    // Rejects ping loads; the adapter runs no beacon transport.
    void startPingLoad(LocalFrame&, ResourceRequest&, const HTTPHeaderMap&, const FetchOptions&, ContentSecurityPolicyImposition, PingLoadCompletionHandler&&) final;

    // Preconnect is a no-op; each load opens its own transfer.
    void preconnectTo(FrameLoader&, ResourceRequest&&, StoredCredentialsPolicy, ShouldPreconnectAsFirstParty, PreconnectCompletionHandler&&) final;

    // No-op: extra metrics capture stays off.
    void setCaptureExtraNetworkLoadMetricsEnabled(bool) final { }

    // Always online; the phone radio state is outside this adapter.
    bool isOnLine() const final { return true; }

    // No-op: online transitions are never delivered.
    void addOnlineStateChangeListener(Function<void(bool)>&&) final { }

    // Reports completion through the finished callback.
    void isResourceLoadFinished(CachedResource&, CompletionHandler<void(bool)>&&) final;

    // Superseded-load error for the cancelled code.
    ResourceError cancelledError(const ResourceRequest&) const final;

    // Policy/content-blocker refusal error for the blocked code.
    ResourceError blockedError(const ResourceRequest&) const final;

    // True for adapter-domain blocked errors only.
    bool isBlockedError(const ResourceError&) const final;

    // Content-blocker refusal mapped onto the blocked code.
    ResourceError blockedByContentBlockerError(const ResourceRequest&) const final;

    // Unsupported-scheme error for the unsupported-URL code.
    ResourceError cannotShowURLError(const ResourceRequest&) const final;

    // Mid-flight policy change mapped onto the cancelled code.
    ResourceError interruptedForPolicyChangeError(const ResourceRequest&) const final;

    // Undisplayable MIME error for the MIME code.
    ResourceError cannotShowMIMETypeError(const ResourceResponse&) const final;

    // Missing local file error for the file code.
    ResourceError fileDoesNotExistError(const ResourceResponse&) const final;

    // HTTPS upgrade loop mapped onto the redirect code.
    ResourceError httpsUpgradeRedirectLoopError(const ResourceRequest&) const final;

    // HTTPS-only refusal error for the HTTPS-only code.
    ResourceError httpNavigationWithHTTPSOnlyError(const ResourceRequest&) const final;

    // True for adapter-domain HTTPS-only errors only.
    bool isHttpNavigationWithHTTPSOnlyError(const ResourceError&) const final;

    // Plugin loads are unsupported on this port.
    ResourceError pluginWillHandleLoadError(const ResourceResponse&) const final;

private:
    // Active transfers by loader; erased on finish, cancel or teardown.
    HashMap<ResourceLoader*, Ref<ResourceLoadUWP>> m_loads;

    // Suspend nesting depth from suspend/resumePendingRequests.
    unsigned m_suspended { 0 };
};
}
