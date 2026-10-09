#include "NavigationStateUWP.h"

#include <utility>

namespace WebKit {

// Apply requested/null Source while preserving engine IDs and committed history.
SourceActionUWP NavigationStateUWP::assignSource(const std::wstring& uri)
{
    if (uri.empty()) {
        m_initialSource.clear();
        m_snapshot.source = m_committedSource;
        return m_controlPhase == ControlPhaseUWP::Ready ? SourceActionUWP::Stop : SourceActionUWP::None;
    }
    if (uri == m_snapshot.source)
        return SourceActionUWP::None;
    m_snapshot.source = uri;
    if (m_controlPhase == ControlPhaseUWP::Created || m_controlPhase == ControlPhaseUWP::Faulted) {
        m_initialSource = uri;
        beginInitialization();
        return SourceActionUWP::Initialize;
    }
    if (m_controlPhase == ControlPhaseUWP::Initializing) {
        m_initialSource = uri;
        return SourceActionUWP::None;
    }
    m_initialSource.clear();
    return SourceActionUWP::Navigate;
}

// Begin one lazy attempt from a created/faulted instance, without allocating an ID.
bool NavigationStateUWP::beginInitialization()
{
    if (m_controlPhase != ControlPhaseUWP::Created && m_controlPhase != ControlPhaseUWP::Faulted)
        return false;
    m_controlPhase = ControlPhaseUWP::Initializing;
    m_initializationError = S_OK;
    return true;
}

// Adopt readiness or restore the prior commit while retaining the original error.
bool NavigationStateUWP::finishInitialization(uint64_t page, HRESULT error)
{
    if (m_controlPhase != ControlPhaseUWP::Initializing || (SUCCEEDED(error) && !page))
        return false;
    m_initializationError = error;
    if (FAILED(error)) {
        m_controlPhase = ControlPhaseUWP::Faulted;
        m_snapshot.source = m_committedSource;
        m_initialSource.clear();
    } else {
        m_controlPhase = ControlPhaseUWP::Ready;
        m_pageId = page;
    }
    return true;
}

// Remove the latest initialization target once the controller is ready.
std::wstring NavigationStateUWP::takeInitialSource()
{
    if (m_controlPhase != ControlPhaseUWP::Ready)
        return { };
    return std::exchange(m_initialSource, { });
}

// Reject foreign, duplicate, unordered and post-close notifications before mutation.
bool NavigationStateUWP::acceptSequence(const WKUWPPageEvent& event)
{
    if (m_controlPhase == ControlPhaseUWP::Closed || !m_pageId || event.pageId != m_pageId
        || event.callbackSequence <= m_callbackSequence)
        return false;
    m_callbackSequence = event.callbackSequence;
    return true;
}

// Adopt a fresh engine proposal and create its loading/terminal obligation.
bool NavigationStateUWP::starting(const WKUWPPageEvent& event, const std::wstring& uri)
{
    if (m_controlPhase != ControlPhaseUWP::Ready || !event.navigationId || event.navigationId <= m_highestNavigationId
        || m_activeNavigationId || event.documentGeneration != m_documentGeneration)
        return false;
    m_highestNavigationId = event.navigationId;
    m_activeNavigationId = event.navigationId;
    m_activeContentKind = event.contentKind;
    m_activeTarget = uri;
    m_navigationPhase = NavigationPhaseUWP::Starting;
    m_snapshot.source = uri;
    m_snapshot.isLoading = true;
    return true;
}

// Advance an allowed synchronous policy dispatch into provisional loading.
void NavigationStateUWP::acceptedStarting()
{
    if (m_navigationPhase == NavigationPhaseUWP::Starting)
        m_navigationPhase = NavigationPhaseUWP::Provisional;
}

// Publish a current commit/same-document URI while protecting a pending proposal.
bool NavigationStateUWP::sourceChanged(const WKUWPPageEvent& event, const std::wstring& uri, const std::wstring& title)
{
    if (event.documentGeneration < m_documentGeneration
        || (event.navigationId && event.navigationId != m_activeNavigationId)
        || (event.documentGeneration > m_documentGeneration && (!event.navigationId || event.navigationId != m_activeNavigationId)))
        return false;
    m_committedSource = uri;
    m_documentGeneration = event.documentGeneration;
    if (event.navigationId)
        m_navigationPhase = NavigationPhaseUWP::Committed;
    // A same-document signal from the old committed document during a pending
    // proposal updates the restoration target, not the published requested URI.
    if (!m_activeNavigationId || m_navigationPhase == NavigationPhaseUWP::Committed)
        m_snapshot.source = uri;
    m_snapshot.title = title;
    m_snapshot.canGoBack = !!event.canGoBack;
    m_snapshot.canGoForward = !!event.canGoForward;
    return true;
}

// Copy history availability only from the current document generation.
bool NavigationStateUWP::historyChanged(const WKUWPPageEvent& event)
{
    if (event.documentGeneration != m_documentGeneration)
        return false;
    m_snapshot.canGoBack = !!event.canGoBack;
    m_snapshot.canGoForward = !!event.canGoForward;
    return true;
}

// Copy the current document title without accepting stale document work.
bool NavigationStateUWP::titleChanged(const WKUWPPageEvent& event, const std::wstring& title)
{
    if (event.documentGeneration != m_documentGeneration)
        return false;
    m_snapshot.title = title;
    return true;
}

// Mark an active ID terminal once and restore Source after an uncommitted load.
bool NavigationStateUWP::completed(const WKUWPPageEvent& event)
{
    if (!event.navigationId || event.navigationId != m_activeNavigationId
        || event.contentKind != m_activeContentKind || event.documentGeneration != m_documentGeneration)
        return false;
    if (m_navigationPhase != NavigationPhaseUWP::Committed)
        m_snapshot.source = m_committedSource;
    m_activeNavigationId = 0;
    m_navigationPhase = NavigationPhaseUWP::None;
    m_snapshot.isLoading = false;
    return true;
}

// Reject commands immediately while keeping public getters on their transaction.
void NavigationStateUWP::markClosing()
{
    m_controlPhase = ControlPhaseUWP::Closing;
    m_initialSource.clear();
}

// Finalize the instance with a stable committed/nonloading observable snapshot.
void NavigationStateUWP::markClosed()
{
    m_controlPhase = ControlPhaseUWP::Closed;
    m_initialSource.clear();
    m_snapshot.source = m_committedSource;
    m_snapshot.isLoading = false;
}

} // namespace WebKit
