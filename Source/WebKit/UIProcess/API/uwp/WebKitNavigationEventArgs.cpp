#include "WebKitNavigationEventArgs.h"

#include <windows.h>

namespace WebKit {

// Copy the proposal and bind its mutable veto to this owner thread.
WebKitNavigationStartingEventArgs::WebKitNavigationStartingEventArgs(unsigned long long id,
    Windows::Foundation::Uri^ uri, WebKitNavigationContentKind kind)
    : m_navigationId(id)
    , m_uri(uri)
    , m_contentKind(kind)
    , m_ownerThread(GetCurrentThreadId())
{
}

// Return the copied engine main ID.
unsigned long long WebKitNavigationStartingEventArgs::NavigationId::get() { return m_navigationId; }
// Return the copied proposed target.
Windows::Foundation::Uri^ WebKitNavigationStartingEventArgs::Uri::get() { return m_uri; }
// Return the proposal's origin/loading policy.
WebKitNavigationContentKind WebKitNavigationStartingEventArgs::ContentKind::get() { return m_contentKind; }
// Read the current synchronous host veto.
bool WebKitNavigationStartingEventArgs::Cancel::get() { return m_cancel.load(); }
// Accept an owner-thread veto only until EndDispatch freezes this argument.
void WebKitNavigationStartingEventArgs::Cancel::set(bool value)
{
    if (GetCurrentThreadId() != m_ownerThread)
        throw ref new Platform::WrongThreadException();
    if (!m_dispatchActive.load())
        throw ref new Platform::ObjectDisposedException(L"NavigationStarting dispatch has ended");
    m_cancel.store(value);
}

// Close the mutation window after the synchronous invocation unwinds.
void WebKitNavigationStartingEventArgs::EndDispatch() { m_dispatchActive.store(false); }

// Copy an effective URI and its independent kind/new-document classifications.
WebKitSourceChangedEventArgs::WebKitSourceChangedEventArgs(Windows::Foundation::Uri^ uri, WebKitSourceChangeKind kind, bool isNewDocument)
    : m_uri(uri)
    , m_changeKind(kind)
    , m_isNewDocument(isNewDocument)
{
}

// Return the copied effective URI.
Windows::Foundation::Uri^ WebKitSourceChangedEventArgs::Uri::get() { return m_uri; }
// Report generation advancement rather than inferring it from traversal kind.
bool WebKitSourceChangedEventArgs::IsNewDocument::get() { return m_isNewDocument; }
// Return the copied engine attribution.
WebKitSourceChangeKind WebKitSourceChangedEventArgs::ChangeKind::get() { return m_changeKind; }

// Copy both engine-history availability flags.
WebKitHistoryChangedEventArgs::WebKitHistoryChangedEventArgs(bool back, bool forward)
    : m_canGoBack(back)
    , m_canGoForward(forward)
{
}

// Return copied previous-neighbor availability.
bool WebKitHistoryChangedEventArgs::CanGoBack::get() { return m_canGoBack; }
// Return copied next-neighbor availability.
bool WebKitHistoryChangedEventArgs::CanGoForward::get() { return m_canGoForward; }

// Retain the copied title with an explicit empty value for absence.
WebKitDocumentTitleChangedEventArgs::WebKitDocumentTitleChangedEventArgs(Platform::String^ title)
    : m_documentTitle(title ? title : L"")
{
}

// Return the immutable document-title payload.
Platform::String^ WebKitDocumentTitleChangedEventArgs::DocumentTitle::get() { return m_documentTitle; }

// Copy terminal classification/status/capability fields before public dispatch.
WebKitNavigationCompletedEventArgs::WebKitNavigationCompletedEventArgs(unsigned long long id, Windows::Foundation::Uri^ uri,
    WebKitNavigationContentKind kind, WebKitNavigationOutcome outcome, WebKitNavigationErrorDomain domain,
    int code, bool hasStatus, unsigned int status, WebKitCertificateFailureToken^ token)
    : m_navigationId(id)
    , m_uri(uri)
    , m_contentKind(kind)
    , m_outcome(outcome)
    , m_errorDomain(domain)
    , m_errorCode(code)
    , m_hasHttpStatus(hasStatus)
    , m_httpStatus(hasStatus ? status : 0)
    , m_certificateFailureToken(token)
{
}

// Return the engine ID whose terminal obligation is delivered.
unsigned long long WebKitNavigationCompletedEventArgs::NavigationId::get() { return m_navigationId; }
// Return the terminal's related target, not a mutable control Source lookup.
Windows::Foundation::Uri^ WebKitNavigationCompletedEventArgs::Uri::get() { return m_uri; }
// Return the copied origin/loading policy.
WebKitNavigationContentKind WebKitNavigationCompletedEventArgs::ContentKind::get() { return m_contentKind; }
// Return the recorded terminal classification.
WebKitNavigationOutcome WebKitNavigationCompletedEventArgs::Outcome::get() { return m_outcome; }
// Return the namespace of the original error code.
WebKitNavigationErrorDomain WebKitNavigationCompletedEventArgs::ErrorDomain::get() { return m_errorDomain; }
// Return the unchanged domain-specific code.
int WebKitNavigationCompletedEventArgs::ErrorCode::get() { return m_errorCode; }
// Report whether the main response supplied an HTTP status.
bool WebKitNavigationCompletedEventArgs::HasHttpStatus::get() { return m_hasHttpStatus; }
// Return HTTP status or the normalized zero absence value.
unsigned int WebKitNavigationCompletedEventArgs::HttpStatus::get() { return m_httpStatus; }
// Return the copied capability object, without asserting current validity.
WebKitCertificateFailureToken^ WebKitNavigationCompletedEventArgs::CertificateFailureToken::get() { return m_certificateFailureToken; }
// Report token presence at payload construction, independently of future validity.
bool WebKitNavigationCompletedEventArgs::CanProceedWithCertificateFailure::get() { return m_certificateFailureToken != nullptr; }

} // namespace WebKit
