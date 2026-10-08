#pragma once

#include <pal/SessionID.h>
#include <wtf/Forward.h>
#include <wtf/HashMap.h>
#include <wtf/NeverDestroyed.h>

// Bounded loading policies for the frozen V1 WKUWP contract (M10 task 4.9):
// configured concurrency, decoded-byte, transient-buffer and deadline ranges
// with session-scoped enforcement. Defaults match artifact/public-api.md;
// the public options surface applies overrides in task 5.2. All entry points
// run on the owner thread, so accounting needs no locking.
namespace WebCore {

// Configured loading ranges; fixed caps are validated, not ranged.
struct ResourcePolicyLimits {
    // Active transfers per page session; 1..16.
    uint32_t maxConcurrentTransfers { 8 };

    // Decoded response-body bytes per resource; 64 KiB..64 MiB.
    uint64_t maxResourceBytes { 32 * 1024 * 1024 };

    // Transient undelivered network bytes per session; 1 MiB..32 MiB.
    uint64_t maxBufferedBytes { 16 * 1024 * 1024 };

    // Deadline per request in milliseconds, starting at enqueue and
    // including queue and redirect time; 1000..120000. Explicit suspension
    // pauses it; visual unload does not.
    uint32_t timeoutMilliseconds { 30000 };

    // Queued requests per session; fixed, not ranged.
    uint32_t maxQueuedRequests { 128 };

    // Redirect hops per request; fixed, not ranged.
    uint32_t maxRedirectHops { 10 };
};

// Bounded loading policies with configured defaults, ranges, session
// overrides and transient accounting. All entry points run on the owner
// thread, so accounting needs no locking.
class ResourcePolicyUWP {
public:
    // Contract defaults from artifact/public-api.md.
    static ResourcePolicyLimits defaults();

    // Whether every ranged member sits inside its configured range. Fixed
    // caps are accepted as documented, not ranged.
    static bool isValidLimits(const ResourcePolicyLimits&);

    // Effective limits for a session: its override, or the defaults.
    static ResourcePolicyLimits limitsFor(PAL::SessionID);

    // Installs validated session limits; rejects out-of-range values without
    // touching the installed policy.
    static bool setSessionLimits(PAL::SessionID, const ResourcePolicyLimits&);

    // Installs session limits without range validation for fixture probes
    // that must trigger a bound below the public floor.
    static void setSessionLimitsForTesting(PAL::SessionID, const ResourcePolicyLimits&);

    // Restores the defaults for a session and drops its transient account.
    static void resetSessionLimits(PAL::SessionID);

    // Charge bytes to the session's transient account; false when the
    // charge would exceed the buffered cap, in which case nothing is charged.
    static bool chargeTransient(PAL::SessionID, uint64_t bytes);

    // Releases a transfer's whole transient account on completion, failure
    // or cancel; saturates instead of underflowing.
    static void releaseTransient(PAL::SessionID, uint64_t bytes);

    // Records an explicit suspension span start for deadline exclusion.
    static void noteSuspendBegin(MonotonicTime);

    // Closes the open suspension span for deadline exclusion.
    static void noteSuspendEnd(MonotonicTime);

    // Suspended time overlapped with [from, to]; explicit suspension pauses
    // deadlines while visual unload never suspends, so request age excludes
    // exactly these spans no matter when transfers enqueue, start or stall.
    static Seconds suspendedOverlap(MonotonicTime from, MonotonicTime to);

private:
    // Installed override plus transient in-flight bytes for one session.
    struct SessionPolicy {
        ResourcePolicyLimits limits;
        uint64_t transientBytes { 0 };
    };

    // Live session policies; entries vanish when reset or torn down.
    static HashMap<PAL::SessionID, SessionPolicy>& sessions();

    // One completed explicit-suspension span for deadline exclusion.
    struct SuspendSpan {
        MonotonicTime begin;
        MonotonicTime end;
    };

    // Explicit-suspension clock: completed spans plus at most one open span.
    // Bounded, because only recent overlaps can intersect a live request age.
    struct SuspendClock {
        bool open { false };
        MonotonicTime openStart;
        Vector<SuspendSpan, 16> spans;
    };

    // Process suspension clock shared by every session's deadline math.
    static SuspendClock& suspendClock();
};

} // namespace WebCore
