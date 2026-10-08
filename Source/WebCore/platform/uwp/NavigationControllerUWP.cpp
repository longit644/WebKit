// Per-page navigation identity, generations and typed signals. Moved-out
// attribution state plus new identifier rules (M10 task 4.3); see
// NavigationControllerUWP.h.
#include "config.h"
#include "NavigationControllerUWP.h"

#include <cstring>

namespace WebCore {

// Builds a controller with no active navigation and zero generations.
NavigationControllerUWP::NavigationControllerUWP() = default;

// See NavigationControllerUWP.h for the allocation contract.
uint64_t NavigationControllerUWP::beginNavigation()
{
    if (m_nextNavigationId == 0)
        return 0;
    if (m_activeNavigationId != 0 && !m_activeSettled)
        noteTerminalOutcome(m_activeNavigationId, NavigationOutcomeUWP::Replaced);
    m_activeNavigationId = m_nextNavigationId++;
    m_activeSettled = false;
    return m_activeNavigationId;
}

// See NavigationControllerUWP.h for the active-ID meaning.
uint64_t NavigationControllerUWP::activeNavigationId() const
{
    return m_activeNavigationId;
}

// See NavigationControllerUWP.h: a new attempt invalidates per-navigation signals.
unsigned NavigationControllerUWP::noteProvisionalLoad()
{
    m_hasHttpStatus = false;
    m_httpStatus = 0;
    m_change = WKDocumentChangeKindNoneUWP;
    m_redirectHops = 0;
    m_title = String();
    m_titleUtf8 = CString();
    m_provisionalOwnerId = m_activeNavigationId;
    m_transportError = 0;
    return ++m_loadGeneration;
}

// See NavigationControllerUWP.h for the error-code meaning.
void NavigationControllerUWP::setTransportError(int code)
{
    m_transportError = code;
}

// See NavigationControllerUWP.h for the error-code meaning.
int NavigationControllerUWP::transportError() const
{
    return m_transportError;
}

// See NavigationControllerUWP.h for the ID meaning.
uint64_t NavigationControllerUWP::lastCommitId() const
{
    return m_lastCommitId;
}

// See NavigationControllerUWP.h for the generation meaning.
unsigned NavigationControllerUWP::loadGeneration() const
{
    return m_loadGeneration;
}

// See NavigationControllerUWP.h: only committed new documents bump the generation.
void NavigationControllerUWP::noteDocumentCommit()
{
    ++m_documentGeneration;
    // Only the current failure may back a proceed gesture, except a warning
    // interstitial, which preserves it across its own substitute commit.
    if (!m_preserveFailure)
        m_lastFailedUrl = String();
    m_preserveFailure = false;
    uint64_t targetId = m_provisionalOwnerId ? m_provisionalOwnerId : m_activeNavigationId;
    m_provisionalOwnerId = 0;
    if (targetId != 0 && noteTerminalOutcome(targetId, NavigationOutcomeUWP::Committed))
        m_lastCommitId = targetId;
}

// See NavigationControllerUWP.h: cancellation is Stop, the rest is failure.
void NavigationControllerUWP::noteProvisionalFailure(bool wasCancelled)
{
    uint64_t targetId = m_provisionalOwnerId ? m_provisionalOwnerId : m_activeNavigationId;
    m_provisionalOwnerId = 0;
    if (targetId == 0)
        return;
    noteTerminalOutcome(targetId,
        wasCancelled ? NavigationOutcomeUWP::Stopped : NavigationOutcomeUWP::ProvisionalFailure);
}

// See NavigationControllerUWP.h: failures after commit are their own terminal.
void NavigationControllerUWP::noteCommittedFailure()
{
    uint64_t targetId = m_lastCommitId ? m_lastCommitId : m_activeNavigationId;
    if (targetId == 0)
        return;
    noteTerminalOutcome(targetId, NavigationOutcomeUWP::CommittedFailure);
}

// See NavigationControllerUWP.h: synchronous preemption records first.
bool NavigationControllerUWP::cancelNavigation(NavigationOutcomeUWP outcome)
{
    if (m_activeNavigationId == 0)
        return false;
    return noteTerminalOutcome(m_activeNavigationId, outcome);
}

// See NavigationControllerUWP.h: finishing observes, never records.
bool NavigationControllerUWP::noteLoadFinished() const
{
    if (m_activeNavigationId == 0 || !m_activeSettled)
        return false;
    return terminalOutcome(m_activeNavigationId) != NavigationOutcomeUWP::Committed;
}

// See NavigationControllerUWP.h for the ledger meaning.
NavigationOutcomeUWP NavigationControllerUWP::terminalOutcome(uint64_t navigationId) const
{
    for (size_t i = 0; i < m_terminalCount; ++i) {
        if (m_terminalIds[i] == navigationId)
            return m_terminalOutcomes[i];
    }
    return NavigationOutcomeUWP::None;
}

// See NavigationControllerUWP.h for the counter meaning.
unsigned NavigationControllerUWP::duplicateTerminalSignals() const
{
    return m_duplicateTerminalSignals;
}

// See NavigationControllerUWP.h: a proven-stale signal changes nothing.
void NavigationControllerUWP::noteStaleSignal()
{
    ++m_duplicateTerminalSignals;
}

// See NavigationControllerUWP.h: exactly one terminal per ID, ever.
bool NavigationControllerUWP::noteTerminalOutcome(uint64_t navigationId, NavigationOutcomeUWP outcome)
{
    if (navigationId == 0)
        return false;
    for (size_t i = 0; i < m_terminalCount; ++i) {
        if (m_terminalIds[i] == navigationId) {
            ++m_duplicateTerminalSignals;
            return false;
        }
    }
    if (m_terminalCount == kMaxTerminalEntries) {
        memmove(m_terminalIds, m_terminalIds + 1, sizeof(m_terminalIds) - sizeof(m_terminalIds[0]));
        memmove(m_terminalOutcomes, m_terminalOutcomes + 1, sizeof(m_terminalOutcomes) - sizeof(m_terminalOutcomes[0]));
        --m_terminalCount;
    }
    m_terminalIds[m_terminalCount] = navigationId;
    m_terminalOutcomes[m_terminalCount] = outcome;
    ++m_terminalCount;
    if (navigationId == m_activeNavigationId)
        m_activeSettled = true;
    return true;
}

// See NavigationControllerUWP.h for the generation meaning.
uint64_t NavigationControllerUWP::documentGeneration() const
{
    return m_documentGeneration;
}

// See NavigationControllerUWP.h for the source meaning.
void NavigationControllerUWP::setSource(const String& uri)
{
    m_source = uri;
}

// See NavigationControllerUWP.h for the source meaning.
const String& NavigationControllerUWP::source() const
{
    return m_source;
}

// See NavigationControllerUWP.h: the UTF-8 copy refreshes with the title.
void NavigationControllerUWP::setTitle(const String& title)
{
    m_title = title;
    m_titleUtf8 = title.utf8();
}

// See NavigationControllerUWP.h for the title meaning.
const String& NavigationControllerUWP::title() const
{
    return m_title;
}

// See NavigationControllerUWP.h for the copy lifetime.
const char* NavigationControllerUWP::titleUtf8() const
{
    return m_titleUtf8.data();
}

// See NavigationControllerUWP.h for the status meaning.
void NavigationControllerUWP::setHttpStatus(bool hasStatus, unsigned status)
{
    m_hasHttpStatus = hasStatus;
    m_httpStatus = status;
}

// See NavigationControllerUWP.h for the status meaning.
bool NavigationControllerUWP::hasHttpStatus() const
{
    return m_hasHttpStatus;
}

// See NavigationControllerUWP.h for the status meaning.
unsigned NavigationControllerUWP::httpStatus() const
{
    return m_httpStatus;
}

// See NavigationControllerUWP.h for the classification meaning.
void NavigationControllerUWP::setChange(WKDocumentChangeKindUWP change)
{
    m_change = change;
}

// See NavigationControllerUWP.h for the classification meaning.
WKDocumentChangeKindUWP NavigationControllerUWP::change() const
{
    return m_change;
}

// See NavigationControllerUWP.h for the hop meaning.
void NavigationControllerUWP::noteRedirect()
{
    ++m_redirectHops;
}

// See NavigationControllerUWP.h for the hop meaning.
unsigned NavigationControllerUWP::redirectHops() const
{
    return m_redirectHops;
}

// See NavigationControllerUWP.h for the failure-token meaning.
void NavigationControllerUWP::setFailedUrl(const String& url)
{
    m_lastFailedUrl = url;
}

// See NavigationControllerUWP.h for the preservation meaning.
void NavigationControllerUWP::setFailurePreserved(bool preserve)
{
    m_preserveFailure = preserve;
}

// See NavigationControllerUWP.h for the failure-token meaning.
void NavigationControllerUWP::clearFailedUrl()
{
    m_lastFailedUrl = String();
}

// See NavigationControllerUWP.h for the failure-token meaning.
const String& NavigationControllerUWP::failedUrl() const
{
    return m_lastFailedUrl;
}

} // namespace WebCore
