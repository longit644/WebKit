#include "WebKitCertificateFailureToken.h"

namespace WebKit {

// Construct an internal decision object with explicit control, revision and HTTPS binding.
WebKitCertificateFailureToken::WebKitCertificateFailureToken(unsigned long long controlIdentity,
    WKW10MCertificateToken token, unsigned long long navigationId, Windows::Foundation::Uri^ uri)
    : m_controlIdentity(controlIdentity)
    , m_engineToken(token)
    , m_failedNavigationId(navigationId)
    , m_failedUri(uri)
{
    if (!controlIdentity || !token.issuer || !token.decision || !navigationId || !uri || uri->SchemeName != L"https")
        throw ref new Platform::InvalidArgumentException(L"Invalid certificate-failure binding");
}

// Return the diagnostic local ID without exposing decision identity.
unsigned long long WebKitCertificateFailureToken::FailedNavigationId::get() { return m_failedNavigationId; }
// Return the immutable failed HTTPS target.
Windows::Foundation::Uri^ WebKitCertificateFailureToken::FailedUri::get() { return m_failedUri; }
// Validate the hidden issuing-control identity.
bool WebKitCertificateFailureToken::IssuedBy(unsigned long long controlIdentity) { return m_controlIdentity == controlIdentity; }
// Provide the opaque native pair to internal controller validation only.
WKW10MCertificateToken WebKitCertificateFailureToken::EngineToken() { return m_engineToken; }

} // namespace WebKit


