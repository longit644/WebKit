// Transient substitute-data host HTML (M10 task 4.5); see TransientPageW10M.h.
#include "config.h"
#include "TransientPageW10M.h"
#include "WKW10MPageForwardW10M.h"

#include "CertificateExceptionStoreW10M.h"
#include "FrameLoadRequest.h"
#include "FrameLoader.h"
#include "LocalFrame.h"
#include "PageHostW10M.h"
#include <WebCore/ResourceResponse.h>
#include <WebCore/SharedBuffer.h>
#include <WebCore/SubstituteData.h>

namespace WebCore {

// See TransientPageW10M.h for the ownership contract.
TransientPageW10M::TransientPageW10M(PageHostW10M& owner)
    : m_owner(&owner)
{
}

// See TransientPageW10M.h: history-hidden about:blank substitute load.
void TransientPageW10M::show(TransientContentKindW10M kind, const String& html)
{
    // Every show is a command with its own ledger ID, even a refused one.
    m_transientNavigationId = m_owner->navigation.beginNavigation();
    m_owner->engineContentKind = WKW10MContentHostHtml;
    m_owner->engineIsLoading = true;
    m_owner->engineTerminalId = 0;
    uint32_t cancel = 0;
    auto policy = startEngineNavigationW10M(*m_owner, m_transientNavigationId,
        WKW10MContentHostHtml, "about:blank", &cancel);
    if (policy != WKW10MResultOK || cancel) {
        if (m_owner->navigation.cancelNavigation(NavigationOutcomeW10M::Vetoed) && m_owner->log)
            m_owner->log("navigation-vetoed", m_transientNavigationId);
        notifyEngineNavigationCompletedW10M(*m_owner, WKW10MContentHostHtml,
            WKW10MOutcomeCancelled);
        return;
    }

    // Host consent gates transient content like NavigationStarting gates
    // navigations: substitute loads skip loader policy, so the host veto is
    // consulted here, synchronously, before any transfer or commit. A
    // refusal records Vetoed and changes nothing else: kind, state, failure
    // flag and document all stand as they were.
    if (m_owner->policyHandler) {
        if (!m_owner->policyHandler(m_owner->policyContext, "about:blank")) {
            if (m_owner->navigation.cancelNavigation(NavigationOutcomeW10M::Vetoed) && m_owner->log)
                m_owner->log("navigation-vetoed", m_transientNavigationId);
            notifyEngineNavigationCompletedW10M(*m_owner, WKW10MContentHostHtml,
                WKW10MOutcomeCancelled);
            return;
        }
    }
    m_kind = kind;
    m_state = TransientPageStateW10M::Starting;
    // Warnings keep the recorded failure behind the proceed gesture across
    // the substitute commit; generic markup lets the commit invalidate it.
    // The certificate token follows the same split: only a token-associated
    // warning preserves eligibility across its provisional and commit.
    m_owner->navigation.setFailurePreserved(kind == TransientContentKindW10M::Warning);
    if (kind == TransientContentKindW10M::Warning && m_owner->page)
        CertificateExceptionStoreW10M::setPreservePending(m_owner->page->sessionID());
    auto utf8 = html.utf8();
    std::span<const uint8_t> bytes { reinterpret_cast<const uint8_t*>(utf8.data()), utf8.length() };
    URL blankURL = WTF::aboutBlankURL();
    ResourceResponse response(URL(blankURL), "text/html"_s, static_cast<long long>(bytes.size()), "utf-8"_s);
    // Empty failing URL: urlForHistory() then yields empty, so the hidden
    // substitute adds no history entry and leaves the current item alone. A
    // non-empty failing URL would do the opposite: unreachable pages keep
    // their history entry by design.
    SubstituteData data(SharedBuffer::create(bytes), URL(), WTF::move(response), SessionHistoryVisibility::Hidden);
    ResourceRequest request(WTF::move(blankURL));
    auto& frame = m_owner->frame();
    frame.loader().load(FrameLoadRequest(frame, WTF::move(request), WTF::move(data)));
}

// See TransientPageW10M.h for the lifecycle meaning.
void TransientPageW10M::noteCommitted()
{
    if (m_state == TransientPageStateW10M::Starting)
        m_state = TransientPageStateW10M::Shown;
}

// See TransientPageW10M.h for the dismissal meaning.
void TransientPageW10M::noteDismissed()
{
    m_state = TransientPageStateW10M::Idle;
    m_kind = TransientContentKindW10M::None;
}

// See TransientPageW10M.h for the kind meaning.
TransientContentKindW10M TransientPageW10M::kind() const
{
    return m_kind;
}

// See TransientPageW10M.h for the state meaning.
TransientPageStateW10M TransientPageW10M::state() const
{
    return m_state;
}

// See TransientPageW10M.h for the ID meaning.
uint64_t TransientPageW10M::transientNavigationId() const
{
    return m_transientNavigationId;
}

} // namespace WebCore

