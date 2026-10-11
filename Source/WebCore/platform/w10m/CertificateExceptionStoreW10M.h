#pragma once

#include <pal/SessionID.h>
#include <wtf/Forward.h>
#include <wtf/HashMap.h>
#include <wtf/HashSet.h>
#include <wtf/NeverDestroyed.h>
#include <wtf/URL.h>
#include <wtf/text/WTFString.h>

// Opaque certificate-decision store for the frozen V1 WKW10M contract (M10
// task 4.8): TLS verification fails closed, and only an explicitly consumed
// token bound to its issuing session, failure revision, host and effective
// port arms a session-scoped exception. Numeric page-local navigation IDs
// never establish token identity, and no reserved document URL grants
// consent. All entry points run on the owner thread, so check-and-consume is
// indivisible without locking.
namespace WebCore {

// Opaque certificate-decision store. Owns per-session failure revisions,
// pending tokens and armed host/port exceptions; all entry points run on the
// owner thread, so check-and-consume is indivisible without locking.
class CertificateExceptionStoreW10M {
public:
    // Records a main-frame failure for a session and returns its monotonic
    // failure revision. Supersedes any pending token: only the newest failure
    // may back consent, so older tokens stay stale by construction.
    static uint64_t noteFailure(PAL::SessionID, const URL& failingURL);

    // Issues the session's eligible token as issuer/decision: the issuing
    // session's handle and its pending failure revision. Zero/zero means no
    // eligible decision, matching the V1 token contract.
    static void issuedToken(PAL::SessionID, uint64_t& issuer, uint64_t& decision);

    // Consumes one eligible token for an explicit host and port: succeeds
    // only for a pending token whose issuer, revision, host and effective
    // port all match the submitting session. Success arms a
    // session/host/port exception and kills the token; failure changes
    // nothing, so probing an attacker-guessed token cannot mutate state.
    static bool consumeToken(PAL::SessionID submittingSession, uint64_t issuer, uint64_t decision, const String& host, uint16_t port);

    // Drops the session's pending token without touching armed exceptions:
    // remote, history, reload, Stop and generic-content commands invalidate
    // eligibility on execution, while already-armed trust survives.
    static void invalidatePending(PAL::SessionID);

    // Arms preservation of the pending token across the next provisional
    // load, for token-associated warning content only.
    static void setPreservePending(PAL::SessionID);

    // Consumes one preservation arming; returns whether it was armed.
    static bool takePreservePending(PAL::SessionID);

    // Whether the session holds an armed exception for this request's host
    // and effective port. Enforcement only; parsing a URL is never consent.
    static bool allowsRequest(PAL::SessionID, const URL&);

    // Drops every token and exception for a session at teardown.
    static void eraseSession(PAL::SessionID);

    // Request port, or the scheme default when absent.
    static uint16_t effectivePort(const URL&);

private:
    // Pending consent for one session: the failure revision that may back it
    // plus the host and port it was recorded for.
    struct PendingToken {
        uint64_t decision { 0 };
        String host;
        uint16_t port { 0 };
    };

    // Per-session consent state: revision counter, at most one pending token,
    // one preservation arming, and armed host/port exceptions.
    struct SessionConsent {
        uint64_t lastRevision { 0 };
        PendingToken pending;
        bool preservePending { false };
        HashSet<String> armedHosts;
    };

    // Live consent by session; entries vanish at teardown, never by timeout.
    static HashMap<PAL::SessionID, SessionConsent>& sessions();

    // Lowercase host for case-insensitive comparison.
    static String normalizedHost(const String& host);

    // Armed-exception key for one host and effective port.
    static String armedKey(const String& host, uint16_t port);
};

} // namespace WebCore

