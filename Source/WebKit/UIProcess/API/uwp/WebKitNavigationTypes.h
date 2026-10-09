#pragma once

namespace WebKit {

// M10 revision-2 terminal classifications, independent of engine result codes.
public enum class WebKitNavigationOutcome {
    // The started navigation finished successfully.
    Completed = 0,
    // The started navigation terminated with a domain-specific failure.
    Failed = 1,
    // Policy, Stop, replacement or Close canceled the started navigation.
    Cancelled = 2
};

// Namespace in which NavigationCompleted.ErrorCode is interpreted.
public enum class WebKitNavigationErrorDomain {
    // No error code accompanies the outcome.
    None = 0,
    // The code is a CURLcode from the transport backend.
    Transport = 1,
    // The code identifies an adapter resource-loading failure.
    Network = 2,
    // The code identifies an engine failure.
    Engine = 3,
    // The code preserves the originating HRESULT bits.
    Platform = 4
};

// Engine attribution of a committed URI change; generation identifies new documents.
public enum class WebKitSourceChangeKind {
    // A normal main-document navigation committed.
    NewDocument = 0,
    // The current document's fragment changed.
    Fragment = 1,
    // The document used history.pushState.
    PushState = 2,
    // The document used history.replaceState.
    ReplaceState = 3,
    // Engine session-history traversal changed the current URI.
    HistoryTraversal = 4
};

// Origin/loading policy of a proposed or completed document.
public enum class WebKitNavigationContentKind {
    // An HTTP(S) response loaded through the engine's network path.
    Remote = 0,
    // Host markup loaded as opaque-origin about:blank substitute data.
    HostHtml = 1
};

} // namespace WebKit
