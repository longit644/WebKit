// CurlRequest-backed resource transfer (M10 task 4.6); see ResourceLoadW10M.h.
#include "config.h"
#include "ResourceLoadW10M.h"

#include "CertificateInfo.h"
#include "CurlRequest.h"
#include "CurlResponse.h"
#include "HTTPHeaderNames.h"
#include "HTTPStatusCodes.h"
#include "LoaderStrategyW10M.h"
#include "NetworkLoadMetrics.h"
#include "NetworkStorageSession.h"
#include "ResourceError.h"
#include "ResourceLoader.h"
#include "ResourcePolicyW10M.h"
#include "ResourceRequest.h"
#include "ResourceResponse.h"
#include "SameSiteInfo.h"
#include "SharedBuffer.h"
#include "TlsPolicyW10M.h"
#include <wtf/MainThread.h>
#include <wtf/RunLoop.h>
#include <wtf/text/MakeString.h>

namespace WebCore {

// Process-wide transfer-event sink installed by setLoaderStrategyW10MLog.
static LoaderStrategyW10MLog g_log { nullptr };

// Installs the transfer-event sink; null silences network logging.
void setLoaderStrategyW10MLog(LoaderStrategyW10MLog log) { g_log = log; }

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

// See ResourceLoadW10M.h for the error-domain contract.
ResourceError loadError(const URL& url, int code, const String& description)
{
    return { "W10MNetwork"_s, code, url, description };
}

// See ResourceLoadW10M.h for the binding contract.
Ref<ResourceLoadW10M> ResourceLoadW10M::create(ResourceLoader& loader, PAL::SessionID sessionID) { return adoptRef(*new ResourceLoadW10M(loader, sessionID)); }

// See ResourceLoadW10M.h for the ownership contract.
void ResourceLoadW10M::ref() const { RefCounted::ref(); }

// See ResourceLoadW10M.h for the ownership contract.
void ResourceLoadW10M::deref() const { RefCounted::deref(); }

// See ResourceLoadW10M.h for the teardown contract.
ResourceLoadW10M::~ResourceLoadW10M() { cancel(); }

// See ResourceLoadW10M.h for the start contract.
bool ResourceLoadW10M::resume()
{
    if (!m_loader || m_started)
        return false;
    m_started = true;
    start(m_loader->request());
    return true;
}

// See ResourceLoadW10M.h for the teardown contract.
void ResourceLoadW10M::cancel()
{
    m_loader = nullptr;
    if (m_received) {
        ResourcePolicyW10M::releaseTransient(m_sessionID, m_received);
        m_received = 0;
    }
    if (auto request = std::exchange(m_request, nullptr)) {
        request->invalidateClient();
        request->cancel();
    }
}

// See ResourceLoadW10M.h for the session contract.
PAL::SessionID ResourceLoadW10M::sessionID() const
{
    return m_sessionID;
}

// See ResourceLoadW10M.h for the binding contract.
ResourceLoadW10M::ResourceLoadW10M(ResourceLoader& loader, PAL::SessionID sessionID)
    : m_loader(&loader)
    , m_sessionID(sessionID)
{
}

// See ResourceLoadW10M.h for the liveness contract.
bool ResourceLoadW10M::current(CurlRequest& request) const { return m_loader && m_request.get() == &request && !m_loader->reachedTerminalState(); }

// See ResourceLoadW10M.h for the start contract.
void ResourceLoadW10M::start(const ResourceRequest& request)
{
    if (!m_loader)
        return;
    if (!request.url().protocolIsInHTTPFamily()) {
        fail(loadError(request.url(), W10MResourceErrorUnsupportedURL, "Unsupported network URL scheme"_s));
        return;
    }
    if (checkDeadline(request.url()))
        return;

    // CurlRequest owns transfer scheduling, decompression and main-thread
    // callbacks. Do not call the unimplemented CURL ResourceHandle::start.
    reportTransferURL("w10m-load-start", request.url(), request.url().string().length());
    auto transportRequest = request;
    // Match NetworkDataTaskCurl's cookie handling, preserving credential
    // omission and secure-cookie filtering. Recompute on each redirect.
    transportRequest.removeHTTPHeaderField(HTTPHeaderName::Cookie);
    if (request.allowCookies()) {
        if (auto* session = storageSessionW10M(m_sessionID)) {
            reportTransfer("w10m-cookie-query-enter", 0);
            auto includeSecure = request.url().protocolIs("https"_s) ? IncludeSecureCookies::Yes : IncludeSecureCookies::No;
            auto cookie = session->cookieRequestHeaderFieldValue(request.firstPartyForCookies(), SameSiteInfo::create(request), request.url(), std::nullopt, std::nullopt, includeSecure, ApplyTrackingPrevention::Yes, ShouldRelaxThirdPartyCookieBlocking::No, IsKnownCrossSiteTracker::No).first;
            reportTransfer("w10m-cookie-query-exit", cookie.length());
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
    // Session trust snapshot for HTTPS, staged before scheduling so handle
    // setup on the worker thread enforces it: the bundle file comes from the
    // immutable snapshot, never from the mutable process singleton and never
    // augmented by the native OS store. Connections are never shared across
    // sessions: every HTTPS transfer opens its own connection and closes it
    // afterwards, so TLS state cannot leak between controls even under
    // connection reuse attempts.
    if (transportRequest.url().protocolIs("https"_s)) {
        String caCertPath;
        if (auto* snapshot = TlsPolicyW10M::snapshotFor(m_sessionID))
            caCertPath = snapshot->caPath;
        reportTransferURL("w10m-tls-bundle", transportRequest.url(), !caCertPath.isEmpty());
        m_request->setRequestSSLPolicyState(caCertPath, false, false);
    }
    m_request->resume();
    armDeadlineTimer();
}

// Transport timeout code: CURLE_OPERATION_TIMEDOUT is 28 in curl/curl.h. The
// engine reports its own deadline expiry with the same code so timeouts stay
// distinguishable from budgets, vetoes and redirect failures.
constexpr int kCurlOperationTimedOut = 28;

// See ResourceLoadW10M.h for the deadline contract.
Seconds ResourceLoadW10M::activeAge() const
{
    auto now = MonotonicTime::now();
    Seconds age = now - m_enqueueTime - ResourcePolicyW10M::suspendedOverlap(m_enqueueTime, now);
    if (age < 0_s)
        return 0_s;
    return age;
}

// See ResourceLoadW10M.h for the deadline contract.
bool ResourceLoadW10M::checkDeadline(const URL& url)
{
    auto allowance = Seconds::fromMilliseconds(ResourcePolicyW10M::limitsFor(m_sessionID).timeoutMilliseconds);
    if (activeAge() < allowance)
        return false;
    fail(loadError(url, kCurlOperationTimedOut, "Request deadline exceeded"_s));
    return true;
}

// See ResourceLoadW10M.h for the deadline contract.
void ResourceLoadW10M::armDeadlineTimer()
{
    auto allowance = Seconds::fromMilliseconds(ResourcePolicyW10M::limitsFor(m_sessionID).timeoutMilliseconds);
    Seconds remaining = allowance - activeAge();
    if (remaining <= 0_s)
        remaining = 0_s;
    auto generation = ++m_deadlineGeneration;
    Ref protectedThis { *this };
    RunLoop::currentSingleton().dispatchAfter(remaining, [protectedThis, generation] {
        if (generation != protectedThis->m_deadlineGeneration || !protectedThis->m_loader)
            return;
        if (protectedThis->checkDeadline(protectedThis->m_loader->request().url()))
            return;
        protectedThis->armDeadlineTimer();
    });
}

// See ResourceLoadW10M.h for the failure contract.
void ResourceLoadW10M::fail(const ResourceError& error)
{
    Ref protectedThis { *this };
    RefPtr loader = m_loader;
    cancel();
    if (loader && !loader->reachedTerminalState())
        loader->didFail(error);
}

// See ResourceLoadW10M.h for the progress contract.
void ResourceLoadW10M::curlDidSendData(CurlRequest& request, unsigned long long sent, unsigned long long total)
{
    if (current(request))
        m_loader->didSendData(sent, total);
}

// See ResourceLoadW10M.h for the redirect contract.
void ResourceLoadW10M::curlDidReceiveResponse(CurlRequest& request, CurlResponse&& response)
{
    ASSERT(isMainThread());
    Ref protectedThis { *this };
    if (!current(request))
        return;
    // Keep separate Set-Cookie fields separate (Expires can contain commas).
    if (request.resourceRequest().allowCookies()) {
        if (auto* session = storageSessionW10M(m_sessionID)) {
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
        // Cycles and runaway chains die at the configured hop budget.
        auto maxRedirectHops = ResourcePolicyW10M::limitsFor(m_sessionID).maxRedirectHops;
        if (++m_redirects > maxRedirectHops) {
            reportTransferURL("w10m-load-redirect-hops", resourceResponse.url(), m_redirects);
            fail(loadError(resourceResponse.url(), W10MResourceErrorRedirectFailed, "Redirect limit exceeded"_s));
            return;
        }
        auto next = request.resourceRequest();
        URL target(resourceResponse.url(), resourceResponse.httpHeaderField(HTTPHeaderName::Location));
        if (!target.protocolIsInHTTPFamily()) {
            fail(loadError(target, W10MResourceErrorUnsupportedURL, "Redirect to unsupported URL scheme"_s));
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
                protectedThis->fail(loadError(old->resourceRequest().url(), W10MResourceErrorCancelled, "Redirect cancelled"_s));
                return;
            }
            protectedThis->start(allowed);
        });
        return;
    }
    reportTransferURL("w10m-load-response", resourceResponse.url(), static_cast<unsigned long>(status));
    reportTransferURL("w10m-load-mime", resourceResponse.url(), static_cast<unsigned long>(resourceResponse.mimeType().length()));
    RefPtr loader = m_loader;
    loader->didReceiveResponse(WTF::move(resourceResponse), [protectedThis = Ref { *this }, request = Ref { request }] {
        if (protectedThis->current(request))
            request->completeDidReceiveResponse();
    });
}

// See ResourceLoadW10M.h for the budget contract.
void ResourceLoadW10M::curlDidReceiveData(CurlRequest& request, Ref<SharedBuffer>&& buffer)
{
    ASSERT(isMainThread());
    if (!current(request))
        return;
    auto& url = request.resourceRequest().url();
    // The deadline covers queue and redirect time, so a stalled transfer
    // fails here too, before another byte is accounted or copied.
    if (checkDeadline(url))
        return;
    size_t chunk = buffer->size();
    size_t newReceived = m_received + chunk;
    auto limits = ResourcePolicyW10M::limitsFor(m_sessionID);
    // A stuck or malicious server must not grow one resource without bound;
    // the check runs before the chunk is accounted or delivered.
    if (newReceived > limits.maxResourceBytes) {
        fail(loadError(url, W10MResourceErrorByteBudgetExceeded, "Resource byte budget exceeded"_s));
        return;
    }

    // The session transient account bounds concurrently undelivered bytes;
    // a refused charge changes nothing, so accounting stays exact.
    if (!ResourcePolicyW10M::chargeTransient(m_sessionID, chunk)) {
        fail(loadError(url, W10MResourceErrorTransientBufferExceeded, "Session transient buffer budget exceeded"_s));
        return;
    }
    m_received = newReceived;
    reportTransferURL("w10m-load-data", url, static_cast<unsigned long>(chunk));
    m_loader->didReceiveData(buffer, buffer->size(), DataPayloadBytes);
}

// See ResourceLoadW10M.h for the completion contract.
void ResourceLoadW10M::curlDidComplete(CurlRequest& request, NetworkLoadMetrics&& metrics)
{
    ASSERT(isMainThread());
    Ref protectedThis { *this };
    if (!current(request))
        return;
    reportTransferURL("w10m-load-complete", request.resourceRequest().url(), static_cast<unsigned long>(m_received));
    RefPtr loader = m_loader;
    cancel();
    loader->didFinishLoading(metrics);
}

// See ResourceLoadW10M.h for the transport-error contract.
void ResourceLoadW10M::curlDidFailWithError(CurlRequest& request, ResourceError&& error, CertificateInfo&&)
{
    ASSERT(isMainThread());
    if (current(request)) {
        reportTransferURL("w10m-load-error", request.resourceRequest().url(), static_cast<unsigned long>(error.errorCode()));
        fail(error);
    }
}

} // namespace WebCore

