// Opaque certificate-decision store (M10 task 4.8); see
// CertificateExceptionStoreW10M.h.
#include "config.h"
#include "CertificateExceptionStoreW10M.h"

#include "LoaderStrategyW10M.h"
#include <wtf/URL.h>
#include <wtf/text/MakeString.h>

namespace WebCore {

// See CertificateExceptionStoreW10M.h for the session-lifetime contract.
HashMap<PAL::SessionID, CertificateExceptionStoreW10M::SessionConsent>& CertificateExceptionStoreW10M::sessions()
{
    static NeverDestroyed<HashMap<PAL::SessionID, SessionConsent>> consents;
    return consents;
}

// See CertificateExceptionStoreW10M.h for the comparison contract.
String CertificateExceptionStoreW10M::normalizedHost(const String& host)
{
    return host.convertToASCIILowercase();
}

// See CertificateExceptionStoreW10M.h for the scoping contract.
String CertificateExceptionStoreW10M::armedKey(const String& host, uint16_t port)
{
    return makeString(normalizedHost(host), ":"_s, port);
}

// See CertificateExceptionStoreW10M.h for the revision contract.
uint64_t CertificateExceptionStoreW10M::noteFailure(PAL::SessionID sessionID, const URL& failingURL)
{
    String host = failingURL.host().toString();
    if (host.isEmpty())
        return 0;
    auto& consent = sessions().ensure(sessionID, [] {
                                  return SessionConsent {};
                              })
                        .iterator->value;
    consent.pending = { ++consent.lastRevision, normalizedHost(host), effectivePort(failingURL) };
    return consent.lastRevision;
}

// See CertificateExceptionStoreW10M.h for the token contract.
void CertificateExceptionStoreW10M::issuedToken(PAL::SessionID sessionID, uint64_t& issuer, uint64_t& decision)
{
    auto iterator = sessions().find(sessionID);
    if (iterator == sessions().end() || !iterator->value.pending.decision) {
        issuer = 0;
        decision = 0;
        return;
    }
    issuer = sessionID.toUInt64();
    decision = iterator->value.pending.decision;
}

// See CertificateExceptionStoreW10M.h: all four bindings must match at once.
bool CertificateExceptionStoreW10M::consumeToken(PAL::SessionID submittingSession, uint64_t issuer, uint64_t decision, const String& host, uint16_t port)
{
    if (host.isEmpty() || !decision || issuer != submittingSession.toUInt64())
        return false;
    auto iterator = sessions().find(submittingSession);
    if (iterator == sessions().end())
        return false;
    auto& pending = iterator->value.pending;
    if (pending.decision != decision || pending.host != normalizedHost(host) || pending.port != port)
        return false;
    iterator->value.armedHosts.add(armedKey(host, port));
    pending = {};
    return true;
}

// See CertificateExceptionW10M.h: pending dies, armed trust survives.
void CertificateExceptionStoreW10M::invalidatePending(PAL::SessionID sessionID)
{
    auto iterator = sessions().find(sessionID);
    if (iterator == sessions().end())
        return;
    iterator->value.pending = {};
}

// See CertificateExceptionStoreW10M.h for the preservation contract.
void CertificateExceptionStoreW10M::setPreservePending(PAL::SessionID sessionID)
{
    sessions().ensure(sessionID, [] {
                  return SessionConsent {};
              })
        .iterator->value.preservePending
        = true;
}

// See CertificateExceptionStoreW10M.h for the single-consumption contract.
bool CertificateExceptionStoreW10M::takePreservePending(PAL::SessionID sessionID)
{
    auto iterator = sessions().find(sessionID);
    if (iterator == sessions().end() || !iterator->value.preservePending)
        return false;
    iterator->value.preservePending = false;
    return true;
}

// See CertificateExceptionStoreW10M.h: enforcement parses, never consents.
bool CertificateExceptionStoreW10M::allowsRequest(PAL::SessionID sessionID, const URL& url)
{
    auto iterator = sessions().find(sessionID);
    if (iterator == sessions().end())
        return false;
    return iterator->value.armedHosts.contains(armedKey(url.host().toString(), effectivePort(url)));
}

// See CertificateExceptionStoreW10M.h for the teardown contract.
void CertificateExceptionStoreW10M::eraseSession(PAL::SessionID sessionID)
{
    sessions().remove(sessionID);
}

// See CertificateExceptionStoreW10M.h for the port contract.
uint16_t CertificateExceptionStoreW10M::effectivePort(const URL& url)
{
    if (auto port = url.port())
        return *port;
    return WTF::defaultPortForProtocol(url.protocol()).value_or(0);
}

// Session/host/port enforcement backing the transport's trust check.
bool hasProceedException(PAL::SessionID sessionID, const URL& url)
{
    return CertificateExceptionStoreW10M::allowsRequest(sessionID, url);
}

// Session teardown drops every token and exception with it.
void removeProceedExceptions(PAL::SessionID sessionID)
{
    CertificateExceptionStoreW10M::eraseSession(sessionID);
}

} // namespace WebCore

