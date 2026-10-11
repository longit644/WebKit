// Per-page navigation identity, generations and typed signals. Moved-out
// attribution state plus new identifier rules (M10 task 4.3); see
// NavigationControllerW10M.h.
#include "config.h"
#include "NavigationControllerW10M.h"

#include <cstring>

namespace WebCore {

// Builds a controller with no active navigation and zero generations.
NavigationControllerW10M::NavigationControllerW10M() = default;

// See NavigationControllerW10M.h for the allocation contract.
uint64_t NavigationControllerW10M::beginNavigation()
{
    if (m_nextNavigationId == 0)
        return 0;
    if (m_activeNavigationId != 0 && !m_activeSettled)
        noteTerminalOutcome(m_activeNavigationId, NavigationOutcomeW10M::Replaced);
    m_activeNavigationId = m_nextNavigationId++;
    m_activeSettled = false;
    return m_activeNavigationId;
}

// See NavigationControllerW10M.h for the active-ID meaning.
uint64_t NavigationControllerW10M::activeNavigationId() const
{
    return m_activeNavigationId;
}

// See NavigationControllerW10M.h: a new attempt invalidates per-navigation signals.
unsigned NavigationControllerW10M::noteProvisionalLoad()
{
    m_hasHttpStatus = false;
    m_httpStatus = 0;
    m_change = WKDocumentChangeKindNoneW10M;
    m_redirectHops = 0;
    m_title = String();
    m_titleUtf8 = CString();
    m_provisionalOwnerId = m_activeNavigationId;
    m_transportError = 0;
    return ++m_loadGeneration;
}

// See NavigationControllerW10M.h for the error-code meaning.
void NavigationControllerW10M::setTransportError(int code)
{
    m_transportError = code;
}

// See NavigationControllerW10M.h for the error-code meaning.
int NavigationControllerW10M::transportError() const
{
    return m_transportError;
}

// See NavigationControllerW10M.h for the ID meaning.
uint64_t NavigationControllerW10M::lastCommitId() const
{
    return m_lastCommitId;
}

// See NavigationControllerW10M.h for the generation meaning.
unsigned NavigationControllerW10M::loadGeneration() const
{
    return m_loadGeneration;
}

// See NavigationControllerW10M.h: only committed new documents bump the generation.
void NavigationControllerW10M::noteDocumentCommit()
{
    ++m_documentGeneration;
    // Only the current failure may back a proceed gesture, except a warning
    // interstitial, which preserves it across its own substitute commit.
    if (!m_preserveFailure)
        m_lastFailedUrl = String();
    m_preserveFailure = false;
    uint64_t targetId = m_provisionalOwnerId ? m_provisionalOwnerId : m_activeNavigationId;
    m_provisionalOwnerId = 0;
    if (targetId != 0 && noteTerminalOutcome(targetId, NavigationOutcomeW10M::Committed))
        m_lastCommitId = targetId;
}

// See NavigationControllerW10M.h: cancellation is Stop, the rest is failure.
void NavigationControllerW10M::noteProvisionalFailure(bool wasCancelled)
{
    uint64_t targetId = m_provisionalOwnerId ? m_provisionalOwnerId : m_activeNavigationId;
    m_provisionalOwnerId = 0;
    if (targetId == 0)
        return;
    noteTerminalOutcome(targetId,
        wasCancelled ? NavigationOutcomeW10M::Stopped : NavigationOutcomeW10M::ProvisionalFailure);
}

// See NavigationControllerW10M.h: failures after commit are their own terminal.
void NavigationControllerW10M::noteCommittedFailure()
{
    uint64_t targetId = m_lastCommitId ? m_lastCommitId : m_activeNavigationId;
    if (targetId == 0)
        return;
    noteTerminalOutcome(targetId, NavigationOutcomeW10M::CommittedFailure);
}

// See NavigationControllerW10M.h: synchronous preemption records first.
bool NavigationControllerW10M::cancelNavigation(NavigationOutcomeW10M outcome)
{
    if (m_activeNavigationId == 0)
        return false;
    return noteTerminalOutcome(m_activeNavigationId, outcome);
}

// See NavigationControllerW10M.h: finishing observes, never records.
bool NavigationControllerW10M::noteLoadFinished() const
{
    if (m_activeNavigationId == 0 || !m_activeSettled)
        return false;
    return terminalOutcome(m_activeNavigationId) != NavigationOutcomeW10M::Committed;
}

// See NavigationControllerW10M.h for the ledger meaning.
NavigationOutcomeW10M NavigationControllerW10M::terminalOutcome(uint64_t navigationId) const
{
    for (size_t i = 0; i < m_terminalCount; ++i) {
        if (m_terminalIds[i] == navigationId)
            return m_terminalOutcomes[i];
    }
    return NavigationOutcomeW10M::None;
}

// See NavigationControllerW10M.h for the counter meaning.
unsigned NavigationControllerW10M::duplicateTerminalSignals() const
{
    return m_duplicateTerminalSignals;
}

// See NavigationControllerW10M.h: a proven-stale signal changes nothing.
void NavigationControllerW10M::noteStaleSignal()
{
    ++m_duplicateTerminalSignals;
}

// See NavigationControllerW10M.h: exactly one terminal per ID, ever.
bool NavigationControllerW10M::noteTerminalOutcome(uint64_t navigationId, NavigationOutcomeW10M outcome)
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

// See NavigationControllerW10M.h for the generation meaning.
uint64_t NavigationControllerW10M::documentGeneration() const
{
    return m_documentGeneration;
}

// See NavigationControllerW10M.h for the source meaning.
void NavigationControllerW10M::setSource(const String& uri)
{
    m_source = uri;
}

// See NavigationControllerW10M.h for the source meaning.
const String& NavigationControllerW10M::source() const
{
    return m_source;
}

// See NavigationControllerW10M.h: the UTF-8 copy refreshes with the title.
void NavigationControllerW10M::setTitle(const String& title)
{
    m_title = title;
    m_titleUtf8 = title.utf8();
}

// See NavigationControllerW10M.h for the title meaning.
const String& NavigationControllerW10M::title() const
{
    return m_title;
}

// See NavigationControllerW10M.h for the copy lifetime.
const char* NavigationControllerW10M::titleUtf8() const
{
    return m_titleUtf8.data();
}

// See NavigationControllerW10M.h for the status meaning.
void NavigationControllerW10M::setHttpStatus(bool hasStatus, unsigned status)
{
    m_hasHttpStatus = hasStatus;
    m_httpStatus = status;
}

// See NavigationControllerW10M.h for the status meaning.
bool NavigationControllerW10M::hasHttpStatus() const
{
    return m_hasHttpStatus;
}

// See NavigationControllerW10M.h for the status meaning.
unsigned NavigationControllerW10M::httpStatus() const
{
    return m_httpStatus;
}

// See NavigationControllerW10M.h for the classification meaning.
void NavigationControllerW10M::setChange(WKDocumentChangeKindW10M change)
{
    m_change = change;
}

// See NavigationControllerW10M.h for the classification meaning.
WKDocumentChangeKindW10M NavigationControllerW10M::change() const
{
    return m_change;
}

// See NavigationControllerW10M.h for the hop meaning.
void NavigationControllerW10M::noteRedirect()
{
    ++m_redirectHops;
}

// See NavigationControllerW10M.h for the hop meaning.
unsigned NavigationControllerW10M::redirectHops() const
{
    return m_redirectHops;
}

// See NavigationControllerW10M.h for the failure-token meaning.
void NavigationControllerW10M::setFailedUrl(const String& url)
{
    m_lastFailedUrl = url;
}

// See NavigationControllerW10M.h for the preservation meaning.
void NavigationControllerW10M::setFailurePreserved(bool preserve)
{
    m_preserveFailure = preserve;
}

// See NavigationControllerW10M.h for the failure-token meaning.
void NavigationControllerW10M::clearFailedUrl()
{
    m_lastFailedUrl = String();
}

// See NavigationControllerW10M.h for the failure-token meaning.
const String& NavigationControllerW10M::failedUrl() const
{
    return m_lastFailedUrl;
}

} // namespace WebCore

