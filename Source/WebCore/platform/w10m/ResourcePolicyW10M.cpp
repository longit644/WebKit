// Bounded loading policies (M10 task 4.9); see ResourcePolicyW10M.h.
#include "config.h"
#include "ResourcePolicyW10M.h"

namespace WebCore {

// See ResourcePolicyW10M.h for the contract-default meaning.
ResourcePolicyLimits ResourcePolicyW10M::defaults()
{
    return ResourcePolicyLimits {};
}

// See ResourcePolicyW10M.h: fixed caps are accepted, ranges are checked.
bool ResourcePolicyW10M::isValidLimits(const ResourcePolicyLimits& limits)
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

// See ResourcePolicyW10M.h for the override meaning.
ResourcePolicyLimits ResourcePolicyW10M::limitsFor(PAL::SessionID sessionID)
{
    auto iterator = sessions().find(sessionID);
    if (iterator == sessions().end())
        return defaults();
    return iterator->value.limits;
}

// See ResourcePolicyW10M.h: rejection leaves the installed policy alone.
bool ResourcePolicyW10M::setSessionLimits(PAL::SessionID sessionID, const ResourcePolicyLimits& limits)
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

// See ResourcePolicyW10M.h: fixture-only, bypasses range validation.
void ResourcePolicyW10M::setSessionLimitsForTesting(PAL::SessionID sessionID, const ResourcePolicyLimits& limits)
{
    sessions().ensure(sessionID, [] {
                  return SessionPolicy {};
              })
        .iterator->value.limits
        = limits;
}

// See ResourcePolicyW10M.h for the restore meaning.
void ResourcePolicyW10M::resetSessionLimits(PAL::SessionID sessionID)
{
    sessions().remove(sessionID);
}

// See ResourcePolicyW10M.h: over-cap charges change nothing.
bool ResourcePolicyW10M::chargeTransient(PAL::SessionID sessionID, uint64_t bytes)
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

// See ResourcePolicyW10M.h: releases saturate instead of underflowing.
void ResourcePolicyW10M::releaseTransient(PAL::SessionID sessionID, uint64_t bytes)
{
    auto iterator = sessions().find(sessionID);
    if (iterator == sessions().end())
        return;
    auto& transientBytes = iterator->value.transientBytes;
    transientBytes = bytes > transientBytes ? 0 : transientBytes - bytes;
}

// See ResourcePolicyW10M.h for the session-lifetime contract.
HashMap<PAL::SessionID, ResourcePolicyW10M::SessionPolicy>& ResourcePolicyW10M::sessions()
{
    static NeverDestroyed<HashMap<PAL::SessionID, SessionPolicy>> policies;
    return policies;
}

// See ResourcePolicyW10M.h for the suspension-clock contract.
ResourcePolicyW10M::SuspendClock& ResourcePolicyW10M::suspendClock()
{
    static NeverDestroyed<SuspendClock> clock;
    return clock;
}

// See ResourcePolicyW10M.h: an open span starts at most once.
void ResourcePolicyW10M::noteSuspendBegin(MonotonicTime now)
{
    auto& clock = suspendClock();
    if (clock.open)
        return;
    clock.open = true;
    clock.openStart = now;
}

// See ResourcePolicyW10M.h: closing records one bounded span.
void ResourcePolicyW10M::noteSuspendEnd(MonotonicTime now)
{
    auto& clock = suspendClock();
    if (!clock.open)
        return;
    clock.open = false;
    if (clock.spans.size() == clock.spans.capacity())
        clock.spans.removeAt(0);
    clock.spans.append({ clock.openStart, now });
}

// See ResourcePolicyW10M.h: completed spans plus the open one, if any.
Seconds ResourcePolicyW10M::suspendedOverlap(MonotonicTime from, MonotonicTime to)
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

