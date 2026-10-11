#include "config.h"
#include "LoaderStrategyW10M.h"

#include "CachedResource.h"
#include "DocumentPage.h"
#include "LocalFrame.h"
#include "Page.h"
#include "ResourceLoadW10M.h"
#include "ResourceLoader.h"
#include "ResourcePolicyW10M.h"
#include "ResourceRequest.h"
#include "ResourceResponse.h"
#include "SubresourceLoader.h"
#include <wtf/URL.h>

#include <optional>

namespace WebCore {

// Starts with no active transfers and no deferred starts.
LoaderStrategyW10M::LoaderStrategyW10M() = default;

// Releases retained transfer owners when the strategy is destroyed.
LoaderStrategyW10M::~LoaderStrategyW10M() = default;

// Creates the WebCore loader before starting its session-scoped curl transfer.
void LoaderStrategyW10M::loadResource(LocalFrame& frame, CachedResource& resource, ResourceRequest&& request, const ResourceLoaderOptions& options, CompletionHandler<void(RefPtr<SubresourceLoader>&&)>&& completion)
{
    auto sessionID = frame.page()->sessionID();
    SubresourceLoader::create(frame, resource, WTF::move(request), options, [this, sessionID, completion = WTF::move(completion)](RefPtr<SubresourceLoader>&& loader) mutable {
        RefPtr pending = loader;
        completion(WTF::move(loader));
        if (!pending || pending->reachedTerminalState())
            return;
        m_loads.set(pending.get(), ResourceLoadW10M::create(*pending, sessionID));
        admitOrQueue(*pending, sessionID);
    });
}

// Admits a transfer to a concurrency slot or queues it FIFO; queue overflow
// fails fast with a distinguishable code instead of growing without bound.
void LoaderStrategyW10M::admitOrQueue(ResourceLoader& loader, PAL::SessionID sessionID)
{
    auto limits = ResourcePolicyW10M::limitsFor(sessionID);
    if (!m_suspended && !loader.defersLoading() && activeTransferCount(sessionID) < limits.maxConcurrentTransfers) {
        startNow(loader);
        return;
    }
    if (queuedLoadCount(sessionID) >= limits.maxQueuedRequests) {
        if (auto transfer = m_loads.take(&loader))
            transfer->cancel();
        loader.didFail(loadError(loader.request().url(), W10MResourceErrorQueueOverflow, "Session request queue overflow"_s));
        return;
    }
    m_queuedLoads.append({ &loader, sessionID, MonotonicTime::now() });
}

// Starts one loader when suspension, deferral and the concurrency bound all
// permit; drops it from the queue on success, leaves it queued on refusal.
bool LoaderStrategyW10M::startNow(ResourceLoader& loader)
{
    auto transfer = m_loads.get(&loader);
    if (!transfer || m_suspended || loader.defersLoading())
        return false;
    auto sessionID = transfer->sessionID();
    if (activeTransferCount(sessionID) >= ResourcePolicyW10M::limitsFor(sessionID).maxConcurrentTransfers)
        return false;
    for (size_t i = 0; i < m_queuedLoads.size(); ++i) {
        if (m_queuedLoads[i].loader == &loader) {
            m_queuedLoads.removeAt(i);
            break;
        }
    }
    m_activeLoaders.add(&loader);
    transfer->resume();
    return true;
}

// Starts queued loaders for a session while concurrency slots stay free.
void LoaderStrategyW10M::startQueuedLoads(PAL::SessionID sessionID)
{
    auto limits = ResourcePolicyW10M::limitsFor(sessionID);
    for (size_t i = 0; i < m_queuedLoads.size() && activeTransferCount(sessionID) < limits.maxConcurrentTransfers;) {
        if (m_queuedLoads[i].session == sessionID && m_queuedLoads[i].loader && startNow(*m_queuedLoads[i].loader))
            continue;
        ++i;
    }
}

// Counts running transfers for one session.
unsigned LoaderStrategyW10M::activeTransferCount(PAL::SessionID sessionID) const
{
    unsigned count = 0;
    for (auto* loader : m_activeLoaders) {
        if (auto transfer = m_loads.get(loader)) {
            if (transfer->sessionID() == sessionID)
                ++count;
        }
    }
    return count;
}

// Counts queued loaders for one session.
unsigned LoaderStrategyW10M::queuedLoadCount(PAL::SessionID sessionID) const
{
    unsigned count = 0;
    for (auto& queued : m_queuedLoads) {
        if (queued.session == sessionID)
            ++count;
    }
    return count;
}

// Removes and cancels the transfer retained for this loader.
void LoaderStrategyW10M::remove(ResourceLoader* loader)
{
    auto* existing = m_loads.get(loader);
    bool wasActive = m_activeLoaders.remove(loader);
    std::optional<PAL::SessionID> freedSession;
    if (existing)
        freedSession = existing->sessionID();
    for (size_t i = 0; i < m_queuedLoads.size();) {
        if (m_queuedLoads[i].loader == loader)
            m_queuedLoads.removeAt(i);
        else
            ++i;
    }
    if (auto taken = m_loads.take(loader))
        taken->cancel();
    if (wasActive && freedSession)
        startQueuedLoads(*freedSession);
}

// Starts a deferred transfer once both loader and strategy permit starting it.
void LoaderStrategyW10M::setDefersLoading(ResourceLoader& loader, bool defers)
{
    if (defers || m_suspended)
        return;
    startNow(loader);
}

// Increments the nesting depth preventing new transfers from starting.
void LoaderStrategyW10M::suspendPendingRequests()
{
    if (!m_suspended)
        ResourcePolicyW10M::noteSuspendBegin(MonotonicTime::now());
    ++m_suspended;
}

// Starts retained pending transfers after the outermost suspension ends.
// Deadline exclusion is absolute: transfers subtract every recorded suspend
// span overlapping their own lifetime, so no per-transfer extension or timer
// surgery is needed here.
void LoaderStrategyW10M::resumePendingRequests()
{
    if (!m_suspended || --m_suspended)
        return;
    ResourcePolicyW10M::noteSuspendEnd(MonotonicTime::now());
    Vector<PAL::SessionID> sessions;
    for (auto& queued : m_queuedLoads) {
        if (!sessions.contains(queued.session))
            sessions.append(queued.session);
    }
    for (auto& session : sessions)
        startQueuedLoads(session);
}

// Reports whether the resource has no loader or its loader reached a terminal state.
void LoaderStrategyW10M::isResourceLoadFinished(CachedResource& resource, CompletionHandler<void(bool)>&& completion)
{
    completion(!resource.loader() || resource.loader()->reachedTerminalState());
}

// Returns an adapter error because blocking network loads are unsupported.
void LoaderStrategyW10M::loadResourceSynchronously(FrameLoader&, ResourceLoaderIdentifier, const ResourceRequest& request, ClientCredentialPolicy, const FetchOptions&, const HTTPHeaderMap&, ResourceError& error, ResourceResponse&, Vector<uint8_t>&)
{
    error = loadError(request.url(), W10MResourceErrorSynchronousUnsupported, "Synchronous network requests are not supported by the W10M adapter"_s);
}

// Completes unsupported ping requests with an error instead of leaving callers pending.
void LoaderStrategyW10M::startPingLoad(LocalFrame&, ResourceRequest& request, const HTTPHeaderMap&, const FetchOptions&, ContentSecurityPolicyImposition, PingLoadCompletionHandler&& completion)
{
    if (completion)
        completion(cannotShowURLError(request), {});
}

// Completes the optional preconnect hint without opening a speculative connection.
void LoaderStrategyW10M::preconnectTo(FrameLoader&, ResourceRequest&&, StoredCredentialsPolicy, ShouldPreconnectAsFirstParty, PreconnectCompletionHandler&& completion)
{
    completion({}); // Optional optimization; ordinary requests still load.
}

// Identifies an explicitly stopped or superseded request.
ResourceError LoaderStrategyW10M::cancelledError(const ResourceRequest& request) const { return loadError(request.url(), W10MResourceErrorCancelled, "Load cancelled"_s); }

// Identifies a request refused before transport begins.
ResourceError LoaderStrategyW10M::blockedError(const ResourceRequest& request) const { return loadError(request.url(), W10MResourceErrorBlocked, "Load blocked"_s); }

// Checks both domain and code to distinguish policy refusal from curl failures.
bool LoaderStrategyW10M::isBlockedError(const ResourceError& error) const { return error.domain() == "W10MNetwork"_s && error.errorCode() == W10MResourceErrorBlocked; }

// Uses the adapter's blocked error for content-blocker refusals.
ResourceError LoaderStrategyW10M::blockedByContentBlockerError(const ResourceRequest& request) const { return blockedError(request); }

// Identifies a URL scheme the adapter cannot fetch.
ResourceError LoaderStrategyW10M::cannotShowURLError(const ResourceRequest& request) const { return loadError(request.url(), W10MResourceErrorUnsupportedURL, "Unsupported URL"_s); }

// Treats a policy-interrupted request as cancelled.
ResourceError LoaderStrategyW10M::interruptedForPolicyChangeError(const ResourceRequest& request) const { return cancelledError(request); }

// Identifies a response type the page cannot display.
ResourceError LoaderStrategyW10M::cannotShowMIMETypeError(const ResourceResponse& response) const { return loadError(response.url(), W10MResourceErrorUnsupportedMIMEType, "Unsupported MIME type"_s); }

// Identifies a missing file using its response URL.
ResourceError LoaderStrategyW10M::fileDoesNotExistError(const ResourceResponse& response) const { return loadError(response.url(), W10MResourceErrorFileDoesNotExist, "File does not exist"_s); }

// Identifies a redirect loop caused by HTTPS upgrading.
ResourceError LoaderStrategyW10M::httpsUpgradeRedirectLoopError(const ResourceRequest& request) const { return loadError(request.url(), W10MResourceErrorRedirectFailed, "HTTPS upgrade redirect loop"_s); }

// Identifies HTTP navigation refused under the HTTPS-only policy.
ResourceError LoaderStrategyW10M::httpNavigationWithHTTPSOnlyError(const ResourceRequest& request) const { return loadError(request.url(), W10MResourceErrorHTTPSOnly, "HTTPS-only navigation policy"_s); }

// Checks both domain and code for HTTPS-only refusal.
bool LoaderStrategyW10M::isHttpNavigationWithHTTPSOnlyError(const ResourceError& error) const { return error.domain() == "W10MNetwork"_s && error.errorCode() == W10MResourceErrorHTTPSOnly; }

// Reports plugin-owned content as undisplayable on this port.
ResourceError LoaderStrategyW10M::pluginWillHandleLoadError(const ResourceResponse& response) const { return cannotShowMIMETypeError(response); }

}

