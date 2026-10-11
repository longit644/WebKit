#pragma once

#include "WebKitCertificateFailureToken.h"
#include "WebKitNavigationTypes.h"

#include <atomic>

namespace WebKit {

// Constructors and dispatch controls stay component-internal. Public payloads
// are copied snapshots; only Starting.Cancel is writable during its dispatch.
public ref class WebKitNavigationStartingEventArgs sealed {
public:
    // Engine-allocated ID of this synchronous proposal.
    property unsigned long long NavigationId { unsigned long long get(); }
    // Copied proposed target, including about:blank for host HTML.
    property Windows::Foundation::Uri^ Uri { Windows::Foundation::Uri^ get(); }
    // Origin/loading policy of the proposed document.
    property WebKitNavigationContentKind ContentKind { WebKitNavigationContentKind get(); }
    // Mutable verdict only during this dispatch; late writes report RO_E_CLOSED.
    property bool Cancel { bool get(); void set(bool); }

internal:
    // Copy the proposal fields and bind Cancel mutation to the construction thread.
    WebKitNavigationStartingEventArgs(unsigned long long, Windows::Foundation::Uri^, WebKitNavigationContentKind);
    // Freeze the verdict after the synchronous event invocation unwinds.
    void EndDispatch();

private:
    // Stable engine proposal ID.
    unsigned long long m_navigationId;
    // Immutable proposal target.
    Windows::Foundation::Uri^ m_uri;
    // Copied proposal content policy.
    WebKitNavigationContentKind m_contentKind;
    // Current host veto while dispatch remains active.
    std::atomic<bool> m_cancel { false };
    // Whether this argument still accepts owner-thread Cancel writes.
    std::atomic<bool> m_dispatchActive { true };
    // Thread that owns synchronous policy mutation.
    unsigned long m_ownerThread;
};

// Copied attribution of an actual engine commit or same-document URI change.
public ref class WebKitSourceChangedEventArgs sealed {
public:
    // Effective URI reported by the structured engine signal.
    property Windows::Foundation::Uri^ Uri { Windows::Foundation::Uri^ get(); }
    // True when document generation advanced, independently of traversal kind.
    property bool IsNewDocument { bool get(); }
    // Engine classification of the commit/same-document change.
    property WebKitSourceChangeKind ChangeKind { WebKitSourceChangeKind get(); }

internal:
    // Copy the effective target, classification and generation-derived document flag.
    WebKitSourceChangedEventArgs(Windows::Foundation::Uri^, WebKitSourceChangeKind, bool isNewDocument);

private:
    // Immutable effective target retained for this event.
    Windows::Foundation::Uri^ m_uri;
    // Copied engine attribution.
    WebKitSourceChangeKind m_changeKind;
    // Whether this signal adopted a new document generation.
    bool m_isNewDocument;
};

// Immutable engine-history availability copied for one notification.
public ref class WebKitHistoryChangedEventArgs sealed {
public:
    // Whether the engine cursor has a previous neighbor.
    property bool CanGoBack { bool get(); }
    // Whether the engine cursor has a next neighbor.
    property bool CanGoForward { bool get(); }

internal:
    // Copy both history flags without retaining a mutable page snapshot.
    WebKitHistoryChangedEventArgs(bool canGoBack, bool canGoForward);

private:
    // Copied previous-neighbor availability.
    bool m_canGoBack;
    // Copied next-neighbor availability.
    bool m_canGoForward;
};

// Immutable title copied from the current document's structured signal.
public ref class WebKitDocumentTitleChangedEventArgs sealed {
public:
    // Copied title; absent titles are represented by an empty String.
    property Platform::String^ DocumentTitle { Platform::String^ get(); }

internal:
    // Normalize an absent title to the public empty-string convention.
    WebKitDocumentTitleChangedEventArgs(Platform::String^);

private:
    // Immutable title retained for this notification.
    Platform::String^ m_documentTitle;
};

// Immutable terminal payload; token presence is a snapshot, not future eligibility.
public ref class WebKitNavigationCompletedEventArgs sealed {
public:
    // Engine main ID whose single terminal obligation this event delivers.
    property unsigned long long NavigationId { unsigned long long get(); }
    // Related proposed/effective target, distinct from restored public Source.
    property Windows::Foundation::Uri^ Uri { Windows::Foundation::Uri^ get(); }
    // Origin/loading policy of the completed navigation.
    property WebKitNavigationContentKind ContentKind { WebKitNavigationContentKind get(); }
    // Completed, Failed or Cancelled classification.
    property WebKitNavigationOutcome Outcome { WebKitNavigationOutcome get(); }
    // Namespace in which ErrorCode is meaningful.
    property WebKitNavigationErrorDomain ErrorDomain { WebKitNavigationErrorDomain get(); }
    // Original domain-specific code, including unchanged HRESULT bits.
    property int ErrorCode { int get(); }
    // Whether a main response actually supplied HttpStatus.
    property bool HasHttpStatus { bool get(); }
    // Main HTTP status, or zero when absent.
    property unsigned int HttpStatus { unsigned int get(); }
    // Eligible component-issued decision object, or null.
    property WebKitCertificateFailureToken^ CertificateFailureToken { WebKitCertificateFailureToken^ get(); }
    // Snapshot of whether a token was present at argument creation.
    property bool CanProceedWithCertificateFailure { bool get(); }

internal:
    // Copy one complete terminal payload before any public notification dispatch.
    WebKitNavigationCompletedEventArgs(unsigned long long, Windows::Foundation::Uri^, WebKitNavigationContentKind,
        WebKitNavigationOutcome, WebKitNavigationErrorDomain, int errorCode,
        bool hasHttpStatus, unsigned int httpStatus, WebKitCertificateFailureToken^);

private:
    // Stable terminal main-navigation ID.
    unsigned long long m_navigationId;
    // Immutable related target.
    Windows::Foundation::Uri^ m_uri;
    // Copied origin/loading policy.
    WebKitNavigationContentKind m_contentKind;
    // Recorded terminal outcome.
    WebKitNavigationOutcome m_outcome;
    // Recorded error namespace.
    WebKitNavigationErrorDomain m_errorDomain;
    // Unchanged domain-specific error code.
    int m_errorCode;
    // Actual HTTP presence, never inferred from a numeric status alone.
    bool m_hasHttpStatus;
    // HTTP value normalized to zero when no status was supplied.
    unsigned int m_httpStatus;
    // Retained opaque capability, independent of later controller transitions.
    WebKitCertificateFailureToken^ m_certificateFailureToken;
};

} // namespace WebKit


