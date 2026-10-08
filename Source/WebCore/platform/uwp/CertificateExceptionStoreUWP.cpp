// Opaque certificate-decision store (M10 task 4.8); see
// CertificateExceptionStoreUWP.h.
#include "config.h"
#include "CertificateExceptionStoreUWP.h"

#include "LoaderStrategyUWP.h"
#include <wtf/URL.h>
#include <wtf/text/MakeString.h>

namespace WebCore {

// See CertificateExceptionStoreUWP.h for the session-lifetime contract.
HashMap<PAL::SessionID, CertificateExceptionStoreUWP::SessionConsent>& CertificateExceptionStoreUWP::sessions()
{
    static NeverDestroyed<HashMap<PAL::SessionID, SessionConsent>> consents;
    return consents;
}

// See CertificateExceptionStoreUWP.h for the comparison contract.
String CertificateExceptionStoreUWP::normalizedHost(const String& host)
{
    return host.convertToASCIILowercase();
}

// See CertificateExceptionStoreUWP.h for the scoping contract.
String CertificateExceptionStoreUWP::armedKey(const String& host, uint16_t port)
{
    return makeString(normalizedHost(host), ":"_s, port);
}

// See CertificateExceptionStoreUWP.h for the revision contract.
uint64_t CertificateExceptionStoreUWP::noteFailure(PAL::SessionID sessionID, const URL& failingURL)
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

// See CertificateExceptionStoreUWP.h for the token contract.
void CertificateExceptionStoreUWP::issuedToken(PAL::SessionID sessionID, uint64_t& issuer, uint64_t& decision)
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

// See CertificateExceptionStoreUWP.h: all four bindings must match at once.
bool CertificateExceptionStoreUWP::consumeToken(PAL::SessionID submittingSession, uint64_t issuer, uint64_t decision, const String& host, uint16_t port)
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

// See CertificateExceptionUWP.h: pending dies, armed trust survives.
void CertificateExceptionStoreUWP::invalidatePending(PAL::SessionID sessionID)
{
    auto iterator = sessions().find(sessionID);
    if (iterator == sessions().end())
        return;
    iterator->value.pending = {};
}

// See CertificateExceptionStoreUWP.h for the preservation contract.
void CertificateExceptionStoreUWP::setPreservePending(PAL::SessionID sessionID)
{
    sessions().ensure(sessionID, [] {
                  return SessionConsent {};
              })
        .iterator->value.preservePending
        = true;
}

// See CertificateExceptionStoreUWP.h for the single-consumption contract.
bool CertificateExceptionStoreUWP::takePreservePending(PAL::SessionID sessionID)
{
    auto iterator = sessions().find(sessionID);
    if (iterator == sessions().end() || !iterator->value.preservePending)
        return false;
    iterator->value.preservePending = false;
    return true;
}

// See CertificateExceptionStoreUWP.h: enforcement parses, never consents.
bool CertificateExceptionStoreUWP::allowsRequest(PAL::SessionID sessionID, const URL& url)
{
    auto iterator = sessions().find(sessionID);
    if (iterator == sessions().end())
        return false;
    return iterator->value.armedHosts.contains(armedKey(url.host().toString(), effectivePort(url)));
}

// See CertificateExceptionStoreUWP.h for the teardown contract.
void CertificateExceptionStoreUWP::eraseSession(PAL::SessionID sessionID)
{
    sessions().remove(sessionID);
}

// See CertificateExceptionStoreUWP.h for the port contract.
uint16_t CertificateExceptionStoreUWP::effectivePort(const URL& url)
{
    if (auto port = url.port())
        return *port;
    return WTF::defaultPortForProtocol(url.protocol()).value_or(0);
}

// Session/host/port enforcement backing the transport's trust check.
bool hasProceedException(PAL::SessionID sessionID, const URL& url)
{
    return CertificateExceptionStoreUWP::allowsRequest(sessionID, url);
}

// Session teardown drops every token and exception with it.
void removeProceedExceptions(PAL::SessionID sessionID)
{
    CertificateExceptionStoreUWP::eraseSession(sessionID);
}

} // namespace WebCore
