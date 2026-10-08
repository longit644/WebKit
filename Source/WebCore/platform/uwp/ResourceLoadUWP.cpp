// CurlRequest-backed resource transfer (M10 task 4.6); see ResourceLoadUWP.h.
#include "config.h"
#include "ResourceLoadUWP.h"

#include "CertificateInfo.h"
#include "CurlRequest.h"
#include "CurlResponse.h"
#include "HTTPHeaderNames.h"
#include "HTTPStatusCodes.h"
#include "LoaderStrategyUWP.h"
#include "NetworkLoadMetrics.h"
#include "NetworkStorageSession.h"
#include "ResourceError.h"
#include "ResourceLoader.h"
#include "ResourcePolicyUWP.h"
#include "ResourceRequest.h"
#include "ResourceResponse.h"
#include "SameSiteInfo.h"
#include "SharedBuffer.h"
#include "TlsPolicyUWP.h"
#include <wtf/MainThread.h>
#include <wtf/RunLoop.h>
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

// See ResourceLoadUWP.h for the error-domain contract.
ResourceError loadError(const URL& url, int code, const String& description)
{
    return { "UWPNetwork"_s, code, url, description };
}

// See ResourceLoadUWP.h for the binding contract.
Ref<ResourceLoadUWP> ResourceLoadUWP::create(ResourceLoader& loader, PAL::SessionID sessionID) { return adoptRef(*new ResourceLoadUWP(loader, sessionID)); }

// See ResourceLoadUWP.h for the ownership contract.
void ResourceLoadUWP::ref() const { RefCounted::ref(); }

// See ResourceLoadUWP.h for the ownership contract.
void ResourceLoadUWP::deref() const { RefCounted::deref(); }

// See ResourceLoadUWP.h for the teardown contract.
ResourceLoadUWP::~ResourceLoadUWP() { cancel(); }

// See ResourceLoadUWP.h for the start contract.
bool ResourceLoadUWP::resume()
{
    if (!m_loader || m_started)
        return false;
    m_started = true;
    start(m_loader->request());
    return true;
}

// See ResourceLoadUWP.h for the teardown contract.
void ResourceLoadUWP::cancel()
{
    m_loader = nullptr;
    if (m_received) {
        ResourcePolicyUWP::releaseTransient(m_sessionID, m_received);
        m_received = 0;
    }
    if (auto request = std::exchange(m_request, nullptr)) {
        request->invalidateClient();
        request->cancel();
    }
}

// See ResourceLoadUWP.h for the session contract.
PAL::SessionID ResourceLoadUWP::sessionID() const
{
    return m_sessionID;
}

// See ResourceLoadUWP.h for the binding contract.
ResourceLoadUWP::ResourceLoadUWP(ResourceLoader& loader, PAL::SessionID sessionID)
    : m_loader(&loader)
    , m_sessionID(sessionID)
{
}

// See ResourceLoadUWP.h for the liveness contract.
bool ResourceLoadUWP::current(CurlRequest& request) const { return m_loader && m_request.get() == &request && !m_loader->reachedTerminalState(); }

// See ResourceLoadUWP.h for the start contract.
void ResourceLoadUWP::start(const ResourceRequest& request)
{
    if (!m_loader)
        return;
    if (!request.url().protocolIsInHTTPFamily()) {
        fail(loadError(request.url(), UWPResourceErrorUnsupportedURL, "Unsupported network URL scheme"_s));
        return;
    }
    if (checkDeadline(request.url()))
        return;

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
    // Session trust snapshot for HTTPS, staged before scheduling so handle
    // setup on the worker thread enforces it: the bundle file comes from the
    // immutable snapshot, never from the mutable process singleton and never
    // augmented by the native OS store. Connections are never shared across
    // sessions: every HTTPS transfer opens its own connection and closes it
    // afterwards, so TLS state cannot leak between controls even under
    // connection reuse attempts.
    if (transportRequest.url().protocolIs("https"_s)) {
        String caCertPath;
        if (auto* snapshot = TlsPolicyUWP::snapshotFor(m_sessionID))
            caCertPath = snapshot->caPath;
        reportTransferURL("uwp-tls-bundle", transportRequest.url(), !caCertPath.isEmpty());
        m_request->setRequestSSLPolicyState(caCertPath, false, false);
    }
    m_request->resume();
    armDeadlineTimer();
}

// Transport timeout code: CURLE_OPERATION_TIMEDOUT is 28 in curl/curl.h. The
// engine reports its own deadline expiry with the same code so timeouts stay
// distinguishable from budgets, vetoes and redirect failures.
constexpr int kCurlOperationTimedOut = 28;

// See ResourceLoadUWP.h for the deadline contract.
Seconds ResourceLoadUWP::activeAge() const
{
    auto now = MonotonicTime::now();
    Seconds age = now - m_enqueueTime - ResourcePolicyUWP::suspendedOverlap(m_enqueueTime, now);
    if (age < 0_s)
        return 0_s;
    return age;
}

// See ResourceLoadUWP.h for the deadline contract.
bool ResourceLoadUWP::checkDeadline(const URL& url)
{
    auto allowance = Seconds::fromMilliseconds(ResourcePolicyUWP::limitsFor(m_sessionID).timeoutMilliseconds);
    if (activeAge() < allowance)
        return false;
    fail(loadError(url, kCurlOperationTimedOut, "Request deadline exceeded"_s));
    return true;
}

// See ResourceLoadUWP.h for the deadline contract.
void ResourceLoadUWP::armDeadlineTimer()
{
    auto allowance = Seconds::fromMilliseconds(ResourcePolicyUWP::limitsFor(m_sessionID).timeoutMilliseconds);
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

// See ResourceLoadUWP.h for the failure contract.
void ResourceLoadUWP::fail(const ResourceError& error)
{
    Ref protectedThis { *this };
    RefPtr loader = m_loader;
    cancel();
    if (loader && !loader->reachedTerminalState())
        loader->didFail(error);
}

// See ResourceLoadUWP.h for the progress contract.
void ResourceLoadUWP::curlDidSendData(CurlRequest& request, unsigned long long sent, unsigned long long total)
{
    if (current(request))
        m_loader->didSendData(sent, total);
}

// See ResourceLoadUWP.h for the redirect contract.
void ResourceLoadUWP::curlDidReceiveResponse(CurlRequest& request, CurlResponse&& response)
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
        // Cycles and runaway chains die at the configured hop budget.
        auto maxRedirectHops = ResourcePolicyUWP::limitsFor(m_sessionID).maxRedirectHops;
        if (++m_redirects > maxRedirectHops) {
            reportTransferURL("uwp-load-redirect-hops", resourceResponse.url(), m_redirects);
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

// See ResourceLoadUWP.h for the budget contract.
void ResourceLoadUWP::curlDidReceiveData(CurlRequest& request, Ref<SharedBuffer>&& buffer)
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
    auto limits = ResourcePolicyUWP::limitsFor(m_sessionID);
    // A stuck or malicious server must not grow one resource without bound;
    // the check runs before the chunk is accounted or delivered.
    if (newReceived > limits.maxResourceBytes) {
        fail(loadError(url, UWPResourceErrorByteBudgetExceeded, "Resource byte budget exceeded"_s));
        return;
    }

    // The session transient account bounds concurrently undelivered bytes;
    // a refused charge changes nothing, so accounting stays exact.
    if (!ResourcePolicyUWP::chargeTransient(m_sessionID, chunk)) {
        fail(loadError(url, UWPResourceErrorTransientBufferExceeded, "Session transient buffer budget exceeded"_s));
        return;
    }
    m_received = newReceived;
    reportTransferURL("uwp-load-data", url, static_cast<unsigned long>(chunk));
    m_loader->didReceiveData(buffer, buffer->size(), DataPayloadBytes);
}

// See ResourceLoadUWP.h for the completion contract.
void ResourceLoadUWP::curlDidComplete(CurlRequest& request, NetworkLoadMetrics&& metrics)
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

// See ResourceLoadUWP.h for the transport-error contract.
void ResourceLoadUWP::curlDidFailWithError(CurlRequest& request, ResourceError&& error, CertificateInfo&&)
{
    ASSERT(isMainThread());
    if (current(request)) {
        reportTransferURL("uwp-load-error", request.resourceRequest().url(), static_cast<unsigned long>(error.errorCode()));
        fail(error);
    }
}

} // namespace WebCore
