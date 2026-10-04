#include "config.h"
#include "LoaderStrategyUWP.h"

#include "CachedResource.h"
#include "CertificateInfo.h"
#include "CurlRequest.h"
#include "CurlRequestClient.h"
#include "CurlResponse.h"
#include "DocumentPage.h"
#include "HTTPHeaderNames.h"
#include "HTTPStatusCodes.h"
#include "LocalFrame.h"
#include "NetworkLoadMetrics.h"
#include "NetworkStorageSession.h"
#include "Page.h"
#include "ResourceLoader.h"
#include "SameSiteInfo.h"
#include "SharedBuffer.h"
#include "SubresourceLoader.h"
#include <wtf/MainThread.h>
#include <wtf/RefCounted.h>
#include <wtf/text/MakeString.h>

namespace WebCore {

// Process-wide transfer-event sink installed by setLoaderStrategyUWPLog.
static LoaderStrategyUWPLog g_log { nullptr };

// Installs the transfer-event sink; null silences network logging.
void setLoaderStrategyUWPLog(LoaderStrategyUWPLog log) { g_log = log; }

// Emits one transfer counter; drops it when no sink is installed.
static void reportTransfer(const char* stage, unsigned long value)
{
    if (g_log)
        g_log(stage, value);
}

// The stage name carries the request URL so subresource transfers are
// identifiable in the host log; the value stays the numeric byte/status code.
static void reportTransferURL(const char* stage, const URL& url, unsigned long value)
{
    if (!g_log)
        return;
    String text = makeString(String::fromUTF8(stage), ": "_s, url.string());
    g_log(text.utf8().data(), value);
}

// Builds an adapter-domain error with a UWPResourceErrorCode value.
static ResourceError loadError(const URL& url, int code, const String& description)
{
    return { "UWPNetwork"_s, code, url, description };
}

// One CurlRequest-backed transfer feeding a WebCore ResourceLoader: cookies,
// redirects, consented trust exceptions and byte budgets live here.
class ResourceLoadUWP final : public RefCounted<ResourceLoadUWP>, public CurlRequestClient {
public:
    // Binds a loader to its page session; the transfer starts on resume.
    static Ref<ResourceLoadUWP> create(ResourceLoader& loader, PAL::SessionID sessionID) { return adoptRef(*new ResourceLoadUWP(loader, sessionID)); }

    // RefCounted ownership for cross-thread transfer callbacks.
    void ref() const final { RefCounted::ref(); }

    // Matches ref() above.
    void deref() const final { RefCounted::deref(); }

    // Cancels any in-flight transfer before destruction.
    ~ResourceLoadUWP() { cancel(); }

    // Starts the transfer unless already running or torn down.
    void resume()
    {
        if (!m_loader || m_started)
            return;
        m_started = true;
        start(m_loader->request());
    }

    // Detaches the loader and cancels the transfer; safe to repeat.
    void cancel()
    {
        m_loader = nullptr;
        if (auto request = std::exchange(m_request, nullptr)) {
            request->invalidateClient();
            request->cancel();
        }
    }

private:
    // Binds a loader to its page session; the transfer starts on resume.
    ResourceLoadUWP(ResourceLoader& loader, PAL::SessionID sessionID)
        : m_loader(&loader)
        , m_sessionID(sessionID)
    {
    }

    // True when the callback belongs to the live transfer.
    bool current(CurlRequest& request) const { return m_loader && m_request.get() == &request && !m_loader->reachedTerminalState(); }

    // Validates the scheme, attaches cookies and starts the CurlRequest.
    void start(const ResourceRequest& request)
    {
        if (!m_loader)
            return;
        if (!request.url().protocolIsInHTTPFamily()) {
            fail(loadError(request.url(), UWPResourceErrorUnsupportedURL, "Unsupported network URL scheme"_s));
            return;
        }

        // CurlRequest owns transfer scheduling, decompression and main-thread
        // callbacks. Do not call the unimplemented CURL ResourceHandle::start.
        reportTransferURL("uwp-load-start", request.url(), request.url().string().length());
        auto transportRequest = request;
        // Match NetworkDataTaskCurl's cookie handling, preserving credential
        // omission and secure-cookie filtering. Recompute on each redirect.
        transportRequest.removeHTTPHeaderField(HTTPHeaderName::Cookie);
        if (request.allowCookies()) {
            if (auto* session = storageSessionUWP(m_sessionID)) {
                reportTransfer("uwp-cookie-query-enter", 0);
                auto includeSecure = request.url().protocolIs("https"_s) ? IncludeSecureCookies::Yes : IncludeSecureCookies::No;
                auto cookie = session->cookieRequestHeaderFieldValue(request.firstPartyForCookies(), SameSiteInfo::create(request), request.url(), std::nullopt, std::nullopt, includeSecure, ApplyTrackingPrevention::Yes, ShouldRelaxThirdPartyCookieBlocking::No, IsKnownCrossSiteTracker::No).first;
                reportTransfer("uwp-cookie-query-exit", cookie.length());
                if (!cookie.isEmpty())
                    transportRequest.setHTTPHeaderField(HTTPHeaderName::Cookie, cookie);
            }
        }
        m_request = CurlRequest::create(transportRequest, *this);
        // Consented per-site exception only (warning page → Advanced → proceed):
        // bypass server-trust evaluation for this request's host while its page
        // lives. Everything else verifies normally; the global flag is untouched.
        if (hasProceedException(m_sessionID, transportRequest.url()))
            m_request->disableServerTrustEvaluation();
        m_request->resume();
    }

    // Fails the loader and detaches; stale callbacks after cancel are dropped.
    void fail(const ResourceError& error)
    {
        Ref protectedThis { *this };
        RefPtr loader = m_loader;
        cancel();
        if (loader && !loader->reachedTerminalState())
            loader->didFail(error);
    }

    // Forwards upload progress for the live transfer only.
    void curlDidSendData(CurlRequest& request, unsigned long long sent, unsigned long long total) final
    {
        if (current(request))
            m_loader->didSendData(sent, total);
    }

    // Handles redirects within the hop budget and completes policy checks before restarting.
    void curlDidReceiveResponse(CurlRequest& request, CurlResponse&& response) final
    {
        ASSERT(isMainThread());
        Ref protectedThis { *this };
        if (!current(request))
            return;
        // Keep separate Set-Cookie fields separate (Expires can contain commas).
        if (request.resourceRequest().allowCookies()) {
            if (auto* session = storageSessionUWP(m_sessionID)) {
                static constexpr auto prefix = "set-cookie: "_s;
                for (auto& header : response.headers) {
                    if (header.startsWithIgnoringASCIICase(prefix))
                        session->setCookiesFromHTTPResponse(request.resourceRequest().firstPartyForCookies(), response.url, header.right(header.length() - prefix.length()));
                }
            }
        }
        ResourceResponse resourceResponse(response);
        auto status = resourceResponse.httpStatusCode();
        // Redirect statuses that carry a new location; anything else completes here.
        if ((status == httpStatus301MovedPermanently || status == httpStatus302Found || status == httpStatus303SeeOther || status == httpStatus307TemporaryRedirect || status == httpStatus308PermanentRedirect)
            && !resourceResponse.httpHeaderField(HTTPHeaderName::Location).isEmpty()) {
            // Cycles and runaway chains die here instead of looping forever.
            constexpr unsigned kMaxRedirectHops = 20;
            if (++m_redirects > kMaxRedirectHops) {
                fail(loadError(resourceResponse.url(), UWPResourceErrorRedirectFailed, "Redirect limit exceeded"_s));
                return;
            }
            auto next = request.resourceRequest();
            URL target(resourceResponse.url(), resourceResponse.httpHeaderField(HTTPHeaderName::Location));
            if (!target.protocolIsInHTTPFamily()) {
                fail(loadError(target, UWPResourceErrorUnsupportedURL, "Redirect to unsupported URL scheme"_s));
                return;
            }
            bool crossOrigin = !protocolHostAndPortAreEqual(next.url(), target);
            if (crossOrigin) {
                next.clearHTTPAuthorization();
                next.clearHTTPOrigin();
            }
            if (next.url().protocolIs("https"_s) && !target.protocolIs("https"_s))
                next.clearHTTPReferrer();
            // 303 always rewrites to GET (except HEAD); 301/302 rewrite only a POST.
            if ((status == httpStatus303SeeOther && next.httpMethod() != "HEAD"_s) || ((status == httpStatus301MovedPermanently || status == httpStatus302Found) && next.httpMethod() == "POST"_s)) {
                next.setHTTPMethod("GET"_s);
                next.setHTTPBody(nullptr);
                next.clearHTTPContentType();
            }
            next.setURL(WTF::move(target));
            // WebCore handles redirect policy/origin checks before another
            // transfer starts. The old response remains paused until cancelled.
            m_loader->willSendRequest(WTF::move(next), resourceResponse, [protectedThis = Ref { *this }, old = Ref { request }](ResourceRequest&& allowed) mutable {
                if (!protectedThis->current(old))
                    return;
                old->invalidateClient();
                old->cancel();
                protectedThis->m_request = nullptr;
                if (allowed.isNull()) {
                    protectedThis->fail(loadError(old->resourceRequest().url(), UWPResourceErrorCancelled, "Redirect cancelled"_s));
                    return;
                }
                protectedThis->start(allowed);
            });
            return;
        }
        reportTransferURL("uwp-load-response", resourceResponse.url(), static_cast<unsigned long>(status));
        reportTransferURL("uwp-load-mime", resourceResponse.url(), static_cast<unsigned long>(resourceResponse.mimeType().length()));
        RefPtr loader = m_loader;
        loader->didReceiveResponse(WTF::move(resourceResponse), [protectedThis = Ref { *this }, request = Ref { request }] {
            if (protectedThis->current(request))
                request->completeDidReceiveResponse();
        });
    }

    // Enforces the byte budget per chunk; oversized resources fail fast.
    void curlDidReceiveData(CurlRequest& request, Ref<SharedBuffer>&& buffer) final
    {
        ASSERT(isMainThread());
        if (!current(request))
            return;
        m_received += buffer->size();
        // A stuck or malicious server must not grow one resource without bound.
        constexpr size_t kMaxResourceBytes = 32 * 1024 * 1024;
        if (m_received > kMaxResourceBytes) {
            fail(loadError(request.resourceRequest().url(), UWPResourceErrorByteBudgetExceeded, "Resource byte budget exceeded"_s));
            return;
        }
        reportTransferURL("uwp-load-data", request.resourceRequest().url(), static_cast<unsigned long>(buffer->size()));
        m_loader->didReceiveData(buffer, buffer->size(), DataPayloadBytes);
    }

    // Completes the loader when the transfer finishes for the live request.
    void curlDidComplete(CurlRequest& request, NetworkLoadMetrics&& metrics) final
    {
        ASSERT(isMainThread());
        Ref protectedThis { *this };
        if (!current(request))
            return;
        reportTransferURL("uwp-load-complete", request.resourceRequest().url(), static_cast<unsigned long>(m_received));
        RefPtr loader = m_loader;
        cancel();
        loader->didFinishLoading(metrics);
    }

    // Reports transport failures for the live transfer; curl codes pass through as-is.
    void curlDidFailWithError(CurlRequest& request, ResourceError&& error, CertificateInfo&&) final
    {
        ASSERT(isMainThread());
        if (current(request)) {
            reportTransferURL("uwp-load-error", request.resourceRequest().url(), static_cast<unsigned long>(error.errorCode()));
            fail(error);
        }
    }

    // Fed loader receiving progress and completion callbacks.
    RefPtr<ResourceLoader> m_loader;

    // Live CurlRequest; null before start and after cancel.
    RefPtr<CurlRequest> m_request;

    // Owning page session for cookies and proceed exceptions.
    PAL::SessionID m_sessionID;

    // Redirect hops followed; the transfer fails past the hop budget.
    unsigned m_redirects { 0 };

    // Body bytes received; the transfer fails past the byte budget.
    size_t m_received { 0 };

    // Guards resume against double start after suspend cycles.
    bool m_started { false };
};

// Starts with no active transfers and no deferred starts.
LoaderStrategyUWP::LoaderStrategyUWP() = default;

// Releases retained transfer owners when the strategy is destroyed.
LoaderStrategyUWP::~LoaderStrategyUWP() = default;

// Creates the WebCore loader before starting its session-scoped curl transfer.
void LoaderStrategyUWP::loadResource(LocalFrame& frame, CachedResource& resource, ResourceRequest&& request, const ResourceLoaderOptions& options, CompletionHandler<void(RefPtr<SubresourceLoader>&&)>&& completion)
{
    auto sessionID = frame.page()->sessionID();
    SubresourceLoader::create(frame, resource, WTF::move(request), options, [this, sessionID, completion = WTF::move(completion)](RefPtr<SubresourceLoader>&& loader) mutable {
        RefPtr pending = loader;
        completion(WTF::move(loader));
        if (!pending || pending->reachedTerminalState())
            return;
        auto transfer = ResourceLoadUWP::create(*pending, sessionID);
        m_loads.set(pending.get(), transfer.copyRef());
        if (!m_suspended && !pending->defersLoading())
            transfer->resume();
    });
}

// Removes and cancels the transfer retained for this loader.
void LoaderStrategyUWP::remove(ResourceLoader* loader)
{
    if (auto transfer = m_loads.take(loader))
        transfer->cancel();
}

// Starts a deferred transfer once both loader and strategy permit starting it.
void LoaderStrategyUWP::setDefersLoading(ResourceLoader& loader, bool defers)
{
    if (!defers && !m_suspended) {
        if (auto transfer = m_loads.get(&loader))
            transfer->resume();
    }
}

// Increments the nesting depth preventing new transfers from starting.
void LoaderStrategyUWP::suspendPendingRequests() { ++m_suspended; }

// Starts retained pending transfers after the outermost suspension ends.
void LoaderStrategyUWP::resumePendingRequests()
{
    if (!m_suspended || --m_suspended)
        return;
    Vector<Ref<ResourceLoadUWP>> pending;
    for (auto& transfer : m_loads.values())
        pending.append(transfer.copyRef());
    for (auto& transfer : pending)
        transfer->resume();
}

// Reports whether the resource has no loader or its loader reached a terminal state.
void LoaderStrategyUWP::isResourceLoadFinished(CachedResource& resource, CompletionHandler<void(bool)>&& completion)
{
    completion(!resource.loader() || resource.loader()->reachedTerminalState());
}

// Returns an adapter error because blocking network loads are unsupported.
void LoaderStrategyUWP::loadResourceSynchronously(FrameLoader&, ResourceLoaderIdentifier, const ResourceRequest& request, ClientCredentialPolicy, const FetchOptions&, const HTTPHeaderMap&, ResourceError& error, ResourceResponse&, Vector<uint8_t>&)
{
    error = loadError(request.url(), UWPResourceErrorSynchronousUnsupported, "Synchronous network requests are not supported by the UWP adapter"_s);
}

// Completes unsupported ping requests with an error instead of leaving callers pending.
void LoaderStrategyUWP::startPingLoad(LocalFrame&, ResourceRequest& request, const HTTPHeaderMap&, const FetchOptions&, ContentSecurityPolicyImposition, PingLoadCompletionHandler&& completion)
{
    if (completion)
        completion(cannotShowURLError(request), {});
}

// Completes the optional preconnect hint without opening a speculative connection.
void LoaderStrategyUWP::preconnectTo(FrameLoader&, ResourceRequest&&, StoredCredentialsPolicy, ShouldPreconnectAsFirstParty, PreconnectCompletionHandler&& completion)
{
    completion({}); // Optional optimization; ordinary requests still load.
}

// Identifies an explicitly stopped or superseded request.
ResourceError LoaderStrategyUWP::cancelledError(const ResourceRequest& request) const { return loadError(request.url(), UWPResourceErrorCancelled, "Load cancelled"_s); }

// Identifies a request refused before transport begins.
ResourceError LoaderStrategyUWP::blockedError(const ResourceRequest& request) const { return loadError(request.url(), UWPResourceErrorBlocked, "Load blocked"_s); }

// Checks both domain and code to distinguish policy refusal from curl failures.
bool LoaderStrategyUWP::isBlockedError(const ResourceError& error) const { return error.domain() == "UWPNetwork"_s && error.errorCode() == UWPResourceErrorBlocked; }

// Uses the adapter's blocked error for content-blocker refusals.
ResourceError LoaderStrategyUWP::blockedByContentBlockerError(const ResourceRequest& request) const { return blockedError(request); }

// Identifies a URL scheme the adapter cannot fetch.
ResourceError LoaderStrategyUWP::cannotShowURLError(const ResourceRequest& request) const { return loadError(request.url(), UWPResourceErrorUnsupportedURL, "Unsupported URL"_s); }

// Treats a policy-interrupted request as cancelled.
ResourceError LoaderStrategyUWP::interruptedForPolicyChangeError(const ResourceRequest& request) const { return cancelledError(request); }

// Identifies a response type the page cannot display.
ResourceError LoaderStrategyUWP::cannotShowMIMETypeError(const ResourceResponse& response) const { return loadError(response.url(), UWPResourceErrorUnsupportedMIMEType, "Unsupported MIME type"_s); }

// Identifies a missing file using its response URL.
ResourceError LoaderStrategyUWP::fileDoesNotExistError(const ResourceResponse& response) const { return loadError(response.url(), UWPResourceErrorFileDoesNotExist, "File does not exist"_s); }

// Identifies a redirect loop caused by HTTPS upgrading.
ResourceError LoaderStrategyUWP::httpsUpgradeRedirectLoopError(const ResourceRequest& request) const { return loadError(request.url(), UWPResourceErrorRedirectFailed, "HTTPS upgrade redirect loop"_s); }

// Identifies HTTP navigation refused by HTTPS-only policy.
ResourceError LoaderStrategyUWP::httpNavigationWithHTTPSOnlyError(const ResourceRequest& request) const { return loadError(request.url(), UWPResourceErrorHTTPSOnly, "HTTPS-only navigation policy"_s); }

// Checks both domain and code for HTTPS-only refusal.
bool LoaderStrategyUWP::isHttpNavigationWithHTTPSOnlyError(const ResourceError& error) const { return error.domain() == "UWPNetwork"_s && error.errorCode() == UWPResourceErrorHTTPSOnly; }

// Reports plugin-owned content as undisplayable on this port.
ResourceError LoaderStrategyUWP::pluginWillHandleLoadError(const ResourceResponse& response) const { return cannotShowMIMETypeError(response); }

// Consented certificate-exception hosts by page session; cleared on page teardown.
static HashMap<PAL::SessionID, HashSet<String>>& proceedExceptions()
{
    static NeverDestroyed<HashMap<PAL::SessionID, HashSet<String>>> exceptions;
    return exceptions;
}

// Records an explicitly consented host exception in the owning page session.
void addProceedException(PAL::SessionID sessionID, const String& host)
{
    proceedExceptions().ensure(sessionID, [] {
                           return HashSet<String> {};
                       })
        .iterator->value.add(host.convertToASCIILowercase());
}

// Erases every host exception when the owning page session ends.
void removeProceedExceptions(PAL::SessionID sessionID)
{
    proceedExceptions().remove(sessionID);
}

// Looks up the request host case-insensitively within its own page session.
bool hasProceedException(PAL::SessionID sessionID, const URL& url)
{
    auto iterator = proceedExceptions().find(sessionID);
    if (iterator == proceedExceptions().end())
        return false;
    return iterator->value.contains(url.host().toString().convertToASCIILowercase());
}

}
