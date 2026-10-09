#pragma once

#include "WKUWPEngine.h"

namespace WebKit {

// A decision capability issued by this component, never activatable by a host.
public ref class WebKitCertificateFailureToken sealed {
public:
    // Diagnostic page-local ID of the eligible failed remote navigation.
    property unsigned long long FailedNavigationId { unsigned long long get(); }
    // Diagnostic immutable HTTPS target to which the hidden decision is bound.
    property Windows::Foundation::Uri^ FailedUri { Windows::Foundation::Uri^ get(); }

internal:
    // Issue a component-only capability after validating nonzero control/engine bindings.
    WebKitCertificateFailureToken(unsigned long long controlIdentity, WKUWPCertificateToken,
        unsigned long long failedNavigationId, Windows::Foundation::Uri^ failedUri);
    // Check hidden control identity independently of equal page-local navigation IDs.
    bool IssuedBy(unsigned long long controlIdentity);
    // Return the engine's opaque pair only to component-internal controller code.
    WKUWPCertificateToken EngineToken();

private:
    // Component identity that prevents cross-control capability submission.
    unsigned long long m_controlIdentity;
    // Native issuer/decision capability, never exposed in public metadata.
    WKUWPCertificateToken m_engineToken;
    // Read-only diagnostic navigation ID; it is not a consent capability.
    unsigned long long m_failedNavigationId;
    // Immutable failed target, retained for diagnostics and bound retry.
    Windows::Foundation::Uri^ m_failedUri;
};

} // namespace WebKit
