#pragma once

#include "CurlRequestClient.h"
#include <pal/SessionID.h>
#include <wtf/Forward.h>
#include <wtf/MonotonicTime.h>
#include <wtf/RefCounted.h>
#include <wtf/Seconds.h>
#include <wtf/URL.h>

namespace WebCore {

class CurlRequest;
class ResourceError;
class ResourceLoader;
class ResourceRequest;
class ResourceResponse;
class SharedBuffer;

// Builds an adapter-domain error with a UWPResourceErrorCode value. Shared
// with the strategy's error factories; defined once in ResourceLoadUWP.cpp.
ResourceError loadError(const URL&, int code, const String& description);

// One CurlRequest-backed transfer feeding a WebCore ResourceLoader: cookies,
// redirects, consented trust exceptions, byte budgets and the request
// deadline live here, all bounded by the session's configured policy.
class ResourceLoadUWP final : public RefCounted<ResourceLoadUWP>, public CurlRequestClient {
public:
    // Binds a loader to its page session; the transfer starts on resume.
    static Ref<ResourceLoadUWP> create(ResourceLoader&, PAL::SessionID);

    // RefCounted ownership for cross-thread transfer callbacks.
    void ref() const final;

    // Matches ref() above.
    void deref() const final;

    // Cancels any in-flight transfer before destruction.
    ~ResourceLoadUWP();

    // Starts the transfer unless already running or torn down; true when this
    // call started it, so the strategy counts active transfers exactly once.
    bool resume();

    // Detaches the loader and cancels the transfer; safe to repeat.
    void cancel();

    // Owning page session for cookies, exceptions and policy accounting.
    PAL::SessionID sessionID() const;

private:
    // Binds a loader to its page session; the transfer starts on resume.
    ResourceLoadUWP(ResourceLoader&, PAL::SessionID);

    // True when the callback belongs to the live transfer.
    bool current(CurlRequest&) const;

    // Validates the scheme, attaches cookies and starts the CurlRequest.
    void start(const ResourceRequest&);

    // Fails the loader and detaches; stale callbacks after cancel are dropped.
    void fail(const ResourceError&);

    // Active request age minus suspended spans; the deadline covers enqueue,
    // queue and redirect time, never suspended time.
    Seconds activeAge() const;

    // Fails the transfer when its deadline passed; false when still live.
    bool checkDeadline(const URL&);

    // Arms the deadline timer for the remaining allowance.
    void armDeadlineTimer();

    // Forwards upload progress for the live transfer only.
    void curlDidSendData(CurlRequest&, unsigned long long sent, unsigned long long total) final;

    // Handles redirects within the hop budget and completes policy checks before restarting.
    void curlDidReceiveResponse(CurlRequest&, CurlResponse&&) final;

    // Enforces the byte budget per chunk; oversized resources fail fast.
    void curlDidReceiveData(CurlRequest&, Ref<SharedBuffer>&&) final;

    // Completes the loader when the transfer finishes for the live request.
    void curlDidComplete(CurlRequest&, NetworkLoadMetrics&&) final;

    // Reports transport failures for the live transfer; curl codes pass through as-is.
    void curlDidFailWithError(CurlRequest&, ResourceError&&, CertificateInfo&&) final;

    // Fed loader receiving progress and completion callbacks.
    ResourceLoader* m_loader;

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

    // Enqueue instant; the deadline starts here, not at transfer start.
    MonotonicTime m_enqueueTime { MonotonicTime::now() };

    // Deadline timer generation; restarts invalidate older timers.
    unsigned m_deadlineGeneration { 0 };
};

} // namespace WebCore
