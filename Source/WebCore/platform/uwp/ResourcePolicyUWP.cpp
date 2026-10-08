// Bounded loading policies (M10 task 4.9); see ResourcePolicyUWP.h.
#include "config.h"
#include "ResourcePolicyUWP.h"

namespace WebCore {

// See ResourcePolicyUWP.h for the contract-default meaning.
ResourcePolicyLimits ResourcePolicyUWP::defaults()
{
    return ResourcePolicyLimits {};
}

// See ResourcePolicyUWP.h: fixed caps are accepted, ranges are checked.
bool ResourcePolicyUWP::isValidLimits(const ResourcePolicyLimits& limits)
{
    constexpr uint32_t kMinConcurrentTransfers = 1;
    constexpr uint32_t kMaxConcurrentTransfers = 16;
    constexpr uint64_t kMinResourceBytes = 64 * 1024;
    constexpr uint64_t kMaxResourceBytes = 64 * 1024 * 1024;
    constexpr uint64_t kMinBufferedBytes = 1024 * 1024;
    constexpr uint64_t kMaxBufferedBytes = 32 * 1024 * 1024;
    constexpr uint32_t kMinTimeoutMilliseconds = 1000;
    constexpr uint32_t kMaxTimeoutMilliseconds = 120000;
    return limits.maxConcurrentTransfers >= kMinConcurrentTransfers
        && limits.maxConcurrentTransfers <= kMaxConcurrentTransfers
        && limits.maxResourceBytes >= kMinResourceBytes
        && limits.maxResourceBytes <= kMaxResourceBytes
        && limits.maxBufferedBytes >= kMinBufferedBytes
        && limits.maxBufferedBytes <= kMaxBufferedBytes
        && limits.timeoutMilliseconds >= kMinTimeoutMilliseconds
        && limits.timeoutMilliseconds <= kMaxTimeoutMilliseconds
        && limits.maxQueuedRequests == 128
        && limits.maxRedirectHops == 10;
}

// See ResourcePolicyUWP.h for the override meaning.
ResourcePolicyLimits ResourcePolicyUWP::limitsFor(PAL::SessionID sessionID)
{
    auto iterator = sessions().find(sessionID);
    if (iterator == sessions().end())
        return defaults();
    return iterator->value.limits;
}

// See ResourcePolicyUWP.h: rejection leaves the installed policy alone.
bool ResourcePolicyUWP::setSessionLimits(PAL::SessionID sessionID, const ResourcePolicyLimits& limits)
{
    if (!isValidLimits(limits))
        return false;
    sessions().ensure(sessionID, [] {
                  return SessionPolicy {};
              })
        .iterator->value.limits
        = limits;
    return true;
}

// See ResourcePolicyUWP.h: fixture-only, bypasses range validation.
void ResourcePolicyUWP::setSessionLimitsForTesting(PAL::SessionID sessionID, const ResourcePolicyLimits& limits)
{
    sessions().ensure(sessionID, [] {
                  return SessionPolicy {};
              })
        .iterator->value.limits
        = limits;
}

// See ResourcePolicyUWP.h for the restore meaning.
void ResourcePolicyUWP::resetSessionLimits(PAL::SessionID sessionID)
{
    sessions().remove(sessionID);
}

// See ResourcePolicyUWP.h: over-cap charges change nothing.
bool ResourcePolicyUWP::chargeTransient(PAL::SessionID sessionID, uint64_t bytes)
{
    auto& policy = sessions().ensure(sessionID, [] {
                                 return SessionPolicy {};
                             })
                       .iterator->value;
    if (policy.transientBytes + bytes > policy.limits.maxBufferedBytes)
        return false;
    policy.transientBytes += bytes;
    return true;
}

// See ResourcePolicyUWP.h: releases saturate instead of underflowing.
void ResourcePolicyUWP::releaseTransient(PAL::SessionID sessionID, uint64_t bytes)
{
    auto iterator = sessions().find(sessionID);
    if (iterator == sessions().end())
        return;
    auto& transientBytes = iterator->value.transientBytes;
    transientBytes = bytes > transientBytes ? 0 : transientBytes - bytes;
}

// See ResourcePolicyUWP.h for the session-lifetime contract.
HashMap<PAL::SessionID, ResourcePolicyUWP::SessionPolicy>& ResourcePolicyUWP::sessions()
{
    static NeverDestroyed<HashMap<PAL::SessionID, SessionPolicy>> policies;
    return policies;
}

// See ResourcePolicyUWP.h for the suspension-clock contract.
ResourcePolicyUWP::SuspendClock& ResourcePolicyUWP::suspendClock()
{
    static NeverDestroyed<SuspendClock> clock;
    return clock;
}

// See ResourcePolicyUWP.h: an open span starts at most once.
void ResourcePolicyUWP::noteSuspendBegin(MonotonicTime now)
{
    auto& clock = suspendClock();
    if (clock.open)
        return;
    clock.open = true;
    clock.openStart = now;
}

// See ResourcePolicyUWP.h: closing records one bounded span.
void ResourcePolicyUWP::noteSuspendEnd(MonotonicTime now)
{
    auto& clock = suspendClock();
    if (!clock.open)
        return;
    clock.open = false;
    if (clock.spans.size() == clock.spans.capacity())
        clock.spans.removeAt(0);
    clock.spans.append({ clock.openStart, now });
}

// See ResourcePolicyUWP.h: completed spans plus the open one, if any.
Seconds ResourcePolicyUWP::suspendedOverlap(MonotonicTime from, MonotonicTime to)
{
    auto& clock = suspendClock();
    Seconds overlapped = 0_s;
    auto accumulate = [&](MonotonicTime begin, MonotonicTime end) {
        auto start = begin > from ? begin : from;
        auto finish = end < to ? end : to;
        if (finish > start)
            overlapped += finish - start;
    };
    for (auto& span : clock.spans)
        accumulate(span.begin, span.end);
    if (clock.open)
        accumulate(clock.openStart, to);
    return overlapped;
}

} // namespace WebCore
