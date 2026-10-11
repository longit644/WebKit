#include "NavigationStateW10M.h"

#include <utility>

namespace WebKit {

// Apply requested/null Source while preserving engine IDs and committed history.
SourceActionW10M NavigationStateW10M::assignSource(const std::wstring& uri)
{
    if (uri.empty()) {
        m_initialSource.clear();
        m_snapshot.source = m_committedSource;
        return m_controlPhase == ControlPhaseW10M::Ready ? SourceActionW10M::Stop : SourceActionW10M::None;
    }
    if (uri == m_snapshot.source)
        return SourceActionW10M::None;
    m_snapshot.source = uri;
    if (m_controlPhase == ControlPhaseW10M::Created || m_controlPhase == ControlPhaseW10M::Faulted) {
        m_initialSource = uri;
        beginInitialization();
        return SourceActionW10M::Initialize;
    }
    if (m_controlPhase == ControlPhaseW10M::Initializing) {
        m_initialSource = uri;
        return SourceActionW10M::None;
    }
    m_initialSource.clear();
    return SourceActionW10M::Navigate;
}

// Begin one lazy attempt from a created/faulted instance, without allocating an ID.
bool NavigationStateW10M::beginInitialization()
{
    if (m_controlPhase != ControlPhaseW10M::Created && m_controlPhase != ControlPhaseW10M::Faulted)
        return false;
    m_controlPhase = ControlPhaseW10M::Initializing;
    m_initializationError = S_OK;
    return true;
}

// Adopt readiness or restore the prior commit while retaining the original error.
bool NavigationStateW10M::finishInitialization(uint64_t page, HRESULT error)
{
    if (m_controlPhase != ControlPhaseW10M::Initializing || (SUCCEEDED(error) && !page))
        return false;
    m_initializationError = error;
    if (FAILED(error)) {
        m_controlPhase = ControlPhaseW10M::Faulted;
        m_snapshot.source = m_committedSource;
        m_initialSource.clear();
    } else {
        m_controlPhase = ControlPhaseW10M::Ready;
        m_pageId = page;
    }
    return true;
}

// Remove the latest initialization target once the controller is ready.
std::wstring NavigationStateW10M::takeInitialSource()
{
    if (m_controlPhase != ControlPhaseW10M::Ready)
        return { };
    return std::exchange(m_initialSource, { });
}

// Reject foreign, duplicate, unordered and post-close notifications before mutation.
bool NavigationStateW10M::acceptSequence(const WKW10MPageEvent& event)
{
    if (m_controlPhase == ControlPhaseW10M::Closed || !m_pageId || event.pageId != m_pageId
        || event.callbackSequence <= m_callbackSequence)
        return false;
    m_callbackSequence = event.callbackSequence;
    return true;
}

// Adopt a fresh engine proposal and create its loading/terminal obligation.
bool NavigationStateW10M::starting(const WKW10MPageEvent& event, const std::wstring& uri)
{
    if (m_controlPhase != ControlPhaseW10M::Ready || !event.navigationId || event.navigationId <= m_highestNavigationId
        || m_activeNavigationId || event.documentGeneration != m_documentGeneration)
        return false;
    m_highestNavigationId = event.navigationId;
    m_activeNavigationId = event.navigationId;
    m_activeContentKind = event.contentKind;
    m_activeTarget = uri;
    m_navigationPhase = NavigationPhaseW10M::Starting;
    m_snapshot.source = uri;
    m_snapshot.isLoading = true;
    return true;
}

// Advance an allowed synchronous policy dispatch into provisional loading.
void NavigationStateW10M::acceptedStarting()
{
    if (m_navigationPhase == NavigationPhaseW10M::Starting)
        m_navigationPhase = NavigationPhaseW10M::Provisional;
}

// Publish a current commit/same-document URI while protecting a pending proposal.
bool NavigationStateW10M::sourceChanged(const WKW10MPageEvent& event, const std::wstring& uri, const std::wstring& title)
{
    if (event.documentGeneration < m_documentGeneration
        || (event.navigationId && event.navigationId != m_activeNavigationId)
        || (event.documentGeneration > m_documentGeneration && (!event.navigationId || event.navigationId != m_activeNavigationId)))
        return false;
    m_committedSource = uri;
    m_documentGeneration = event.documentGeneration;
    if (event.navigationId)
        m_navigationPhase = NavigationPhaseW10M::Committed;
    // A same-document signal from the old committed document during a pending
    // proposal updates the restoration target, not the published requested URI.
    if (!m_activeNavigationId || m_navigationPhase == NavigationPhaseW10M::Committed)
        m_snapshot.source = uri;
    m_snapshot.title = title;
    m_snapshot.canGoBack = !!event.canGoBack;
    m_snapshot.canGoForward = !!event.canGoForward;
    return true;
}

// Copy history availability only from the current document generation.
bool NavigationStateW10M::historyChanged(const WKW10MPageEvent& event)
{
    if (event.documentGeneration != m_documentGeneration)
        return false;
    m_snapshot.canGoBack = !!event.canGoBack;
    m_snapshot.canGoForward = !!event.canGoForward;
    return true;
}

// Copy the current document title without accepting stale document work.
bool NavigationStateW10M::titleChanged(const WKW10MPageEvent& event, const std::wstring& title)
{
    if (event.documentGeneration != m_documentGeneration)
        return false;
    m_snapshot.title = title;
    return true;
}

// Mark an active ID terminal once and restore Source after an uncommitted load.
bool NavigationStateW10M::completed(const WKW10MPageEvent& event)
{
    if (!event.navigationId || event.navigationId != m_activeNavigationId
        || event.contentKind != m_activeContentKind || event.documentGeneration != m_documentGeneration)
        return false;
    if (m_navigationPhase != NavigationPhaseW10M::Committed)
        m_snapshot.source = m_committedSource;
    m_activeNavigationId = 0;
    m_navigationPhase = NavigationPhaseW10M::None;
    m_snapshot.isLoading = false;
    return true;
}

// Reject commands immediately while keeping public getters on their transaction.
void NavigationStateW10M::markClosing()
{
    m_controlPhase = ControlPhaseW10M::Closing;
    m_initialSource.clear();
}

// Finalize the instance with a stable committed/nonloading observable snapshot.
void NavigationStateW10M::markClosed()
{
    m_controlPhase = ControlPhaseW10M::Closed;
    m_initialSource.clear();
    m_snapshot.source = m_committedSource;
    m_snapshot.isLoading = false;
}

} // namespace WebKit

