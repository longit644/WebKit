#pragma once

#include "WKUWPNavigation.h"

#include <string>
#include <windows.h>

namespace WebKit {

// Instance lifetime, separate from main-navigation progress and visual loading.
enum class ControlPhaseUWP {
    // No initialization attempt has begun.
    Created,
    // One shared initialization attempt owns the latest queued Source.
    Initializing,
    // A page/controller is ready to accept commands.
    Ready,
    // Initialization failed; an explicit request can retry frozen options.
    Faulted,
    // New work is rejected while owed delivery and cleanup unwind.
    Closing,
    // Instance cleanup finished; final observable state remains readable.
    Closed
};

// Progress of the one nonterminal main navigation, not the control lifetime.
enum class NavigationPhaseUWP {
    // No main navigation has an outstanding terminal obligation.
    None,
    // A synchronous policy dispatch is evaluating the proposal.
    Starting,
    // The accepted proposal has not committed a document.
    Provisional,
    // Its document committed but the main navigation is still loading.
    Committed
};

// Controller work required by a Source or lifecycle transition.
enum class SourceActionUWP {
    // Publish state only, without native work.
    None,
    // Begin or join the instance's lazy initialization.
    Initialize,
    // Propose the supplied remote target through the negotiated page controller.
    Navigate,
    // Cancel outstanding work without inventing a main-navigation ID.
    Stop,
    // Cancel/detach the instance after public dispatch unwinds.
    Close
};

// The controller owns the context through call/dispatch unwind. This local port
// is not the exported engine ABI; page work behind it uses the negotiated table.
using SourceCommandHandlerUWP = HRESULT(__stdcall*)(void*, SourceActionUWP, const WKUWPString* uri);

// Copied observable values published together for one notification transaction.
struct NavigationSnapshotUWP {
    // Requested target while pending; effective/restored URI otherwise; empty is null.
    std::wstring source;
    // Current document title, empty when no title was reported.
    std::wstring title;
    // Availability of the engine's previous history item.
    bool canGoBack { false };
    // Availability of the engine's next history item.
    bool canGoForward { false };
    // True only while a started main navigation still owes its terminal outcome.
    bool isLoading { false };
};

// Owner-thread value state. The projection prepares a candidate copy and all
// WinRT payloads before publishing it, then freezes that snapshot for dispatch.
class NavigationStateUWP {
public:
    // Borrow the immutable observable view until the next owner-thread publication.
    const NavigationSnapshotUWP& snapshot() const { return m_snapshot; }
    // Read the instance's initialization/cleanup phase.
    ControlPhaseUWP controlPhase() const { return m_controlPhase; }
    // Read main-navigation progress independently of initialization.
    NavigationPhaseUWP navigationPhase() const { return m_navigationPhase; }
    // Read the page identity adopted at successful readiness.
    uint64_t pageId() const { return m_pageId; }
    // Read the outstanding main ID, or zero after its terminal obligation is marked.
    uint64_t activeNavigationId() const { return m_activeNavigationId; }
    // Read the highest engine-admitted ID to distinguish rejection from started work.
    uint64_t highestNavigationId() const { return m_highestNavigationId; }
    // Read the policy of the active proposal, preventing mismatched terminal kinds.
    uint32_t activeContentKind() const { return m_activeContentKind; }
    // Borrow the active proposal's copied target for terminal URI fallback.
    const std::wstring& activeTarget() const { return m_activeTarget; }
    // Read the committed document generation used to reject stale signals.
    uint64_t documentGeneration() const { return m_documentGeneration; }
    // Report whether an actual committed remote/transient document exists.
    bool hasDocument() const { return !m_committedSource.empty(); }
    // Read the exact failure retained for the last initialization attempt.
    HRESULT initializationError() const { return m_initializationError; }

    // Apply validated requested/null Source semantics and identify required native work.
    SourceActionUWP assignSource(const std::wstring&);
    // Restore only a rejected requested URI, leaving current document/loading identity intact.
    void restoreRequestedSource(const std::wstring& source) { m_snapshot.source = source; }
    // Enter Initializing once from Created/Faulted, preserving the queued target.
    bool beginInitialization();
    // Adopt page readiness or restore the committed URI on initialization failure.
    bool finishInitialization(uint64_t pageId, HRESULT);
    // Consume the latest initial target once, after readiness/joiner resolution.
    std::wstring takeInitialSource();
    // Admit only this live page's strictly increasing callback sequences.
    bool acceptSequence(const WKUWPPageEvent&);
    // Allocate no ID locally; adopt the engine's new proposal and publish loading.
    bool starting(const WKUWPPageEvent&, const std::wstring& uri);
    // Move an allowed policy proposal into provisional loading.
    void acceptedStarting();
    // Adopt a valid commit/same-document signal and update the restoration target.
    bool sourceChanged(const WKUWPPageEvent&, const std::wstring& uri, const std::wstring& title);
    // Copy current-generation engine history availability.
    bool historyChanged(const WKUWPPageEvent&);
    // Copy a current-generation document title without touching Source.
    bool titleChanged(const WKUWPPageEvent&, const std::wstring&);
    // Mark one active main ID terminal and restore Source when it never committed.
    bool completed(const WKUWPPageEvent&);
    // Reject future work immediately without changing the published transaction.
    void markClosing();
    // Finish cleanup with committed Source and a nonloading final snapshot.
    void markClosed();

private:
    // Observable values; mutations are installed as one projection transaction.
    NavigationSnapshotUWP m_snapshot;
    // Initialization/cleanup state of this control.
    ControlPhaseUWP m_controlPhase { ControlPhaseUWP::Created };
    // Loading state of the one outstanding engine proposal.
    NavigationPhaseUWP m_navigationPhase { NavigationPhaseUWP::None };
    // Last actual committed URI, including same-document changes.
    std::wstring m_committedSource;
    // Latest Source queued during initialization; empty means canceled.
    std::wstring m_initialSource;
    // Identity of the page whose structured signals are accepted.
    uint64_t m_pageId { 0 };
    // Main ID still owing a terminal outcome.
    uint64_t m_activeNavigationId { 0 };
    // Content policy fixed when the main proposal starts.
    uint32_t m_activeContentKind { WKUWPContentRemote };
    // Copied proposed URI, independent of public committed/restored Source.
    std::wstring m_activeTarget;
    // Highest admitted main ID; IDs never repeat within this page.
    uint64_t m_highestNavigationId { 0 };
    // Current committed document generation.
    uint64_t m_documentGeneration { 0 };
    // Highest admitted callback sequence for this page.
    uint64_t m_callbackSequence { 0 };
    // Originating initialization HRESULT, retained across null Source assignment.
    HRESULT m_initializationError { S_OK };
};

} // namespace WebKit
