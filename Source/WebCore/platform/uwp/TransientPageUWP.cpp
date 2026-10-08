// Transient substitute-data host HTML (M10 task 4.5); see TransientPageUWP.h.
#include "config.h"
#include "TransientPageUWP.h"

#include "CertificateExceptionStoreUWP.h"
#include "FrameLoadRequest.h"
#include "FrameLoader.h"
#include "LocalFrame.h"
#include "PageHostUWP.h"
#include <WebCore/ResourceResponse.h>
#include <WebCore/SharedBuffer.h>
#include <WebCore/SubstituteData.h>

namespace WebCore {

// See TransientPageUWP.h for the ownership contract.
TransientPageUWP::TransientPageUWP(PageHostUWP& owner)
    : m_owner(&owner)
{
}

// See TransientPageUWP.h: history-hidden about:blank substitute load.
void TransientPageUWP::show(TransientContentKindUWP kind, const String& html)
{
    // Every show is a command with its own ledger ID, even a refused one.
    m_transientNavigationId = m_owner->navigation.beginNavigation();
    // Host consent gates transient content like NavigationStarting gates
    // navigations: substitute loads skip loader policy, so the host veto is
    // consulted here, synchronously, before any transfer or commit. A
    // refusal records Vetoed and changes nothing else: kind, state, failure
    // flag and document all stand as they were.
    if (m_owner->policyHandler) {
        if (!m_owner->policyHandler(m_owner->policyContext, "about:blank")) {
            if (m_owner->navigation.cancelNavigation(NavigationOutcomeUWP::Vetoed) && m_owner->log)
                m_owner->log("navigation-vetoed", m_transientNavigationId);
            return;
        }
    }
    m_kind = kind;
    m_state = TransientPageStateUWP::Starting;
    // Warnings keep the recorded failure behind the proceed gesture across
    // the substitute commit; generic markup lets the commit invalidate it.
    // The certificate token follows the same split: only a token-associated
    // warning preserves eligibility across its provisional and commit.
    m_owner->navigation.setFailurePreserved(kind == TransientContentKindUWP::Warning);
    if (kind == TransientContentKindUWP::Warning && m_owner->page)
        CertificateExceptionStoreUWP::setPreservePending(m_owner->page->sessionID());
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

// See TransientPageUWP.h for the lifecycle meaning.
void TransientPageUWP::noteCommitted()
{
    if (m_state == TransientPageStateUWP::Starting)
        m_state = TransientPageStateUWP::Shown;
}

// See TransientPageUWP.h for the dismissal meaning.
void TransientPageUWP::noteDismissed()
{
    m_state = TransientPageStateUWP::Idle;
    m_kind = TransientContentKindUWP::None;
}

// See TransientPageUWP.h for the kind meaning.
TransientContentKindUWP TransientPageUWP::kind() const
{
    return m_kind;
}

// See TransientPageUWP.h for the state meaning.
TransientPageStateUWP TransientPageUWP::state() const
{
    return m_state;
}

// See TransientPageUWP.h for the ID meaning.
uint64_t TransientPageUWP::transientNavigationId() const
{
    return m_transientNavigationId;
}

} // namespace WebCore
