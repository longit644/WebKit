#pragma once

#include "PageW10M.h"
#include <wtf/text/WTFString.h>

// Transient substitute-data host HTML for the frozen V1 WKW10M contract (M10
// task 4.5): app-supplied markup displayed without touching session history,
// the network document, or the opaque origin. A warning interstitial keeps
// the recorded failure behind the proceed gesture; generic host HTML
// invalidates it, failing closed.
namespace WebCore {

struct PageHostW10M;

// Kind of transient content on screen; the kind decides whether the recorded
// failure survives the substitute commit.
enum class TransientContentKindW10M : unsigned {
    // No transient content; the state below is Idle.
    None = 0,
    // Generic app-supplied markup; invalidates the recorded failure.
    GenericHTML = 1,
    // Warning interstitial; preserves the recorded failure for proceed.
    Warning = 2,
};

// Lifecycle of the transient document; terminals ride the navigation
// controller's exactly-once ledger under the transient navigation ID.
enum class TransientPageStateW10M : unsigned {
    // No transient content shown or starting.
    Idle = 0,
    // Substitute load issued, commit not observed yet.
    Starting = 1,
    // Substitute document committed and current.
    Shown = 2,
};

// Transient substitute-data host HTML. Owns kind, lifecycle state and the
// transient navigation ID; the substitute load itself flows through the
// ordinary frame loader with session-history-hidden data.
class TransientPageW10M {
public:
    // Binds the transient controller to its owning host.
    explicit TransientPageW10M(PageHostW10M& owner);

    // Non-copyable transient state; one controller owns one page's display.
    TransientPageW10M(const TransientPageW10M&) = delete;

    // Non-copyable transient state; one controller owns one page's display.
    TransientPageW10M& operator=(const TransientPageW10M&) = delete;

    // Shows app-supplied markup as a history-hidden about:blank substitute
    // document: records the kind, enters Starting, allocates a navigation ID
    // whose ledger terminal classifies the outcome, and loads. The host veto
    // is consulted synchronously first (substitute loads skip loader
    // policy); refusal records Vetoed and changes nothing else. The failing
    // URL stays empty so urlForHistory() yields empty: no entry is added and
    // the current item is left alone. Warnings preserve the recorded failure
    // across the substitute commit; generic markup lets the commit
    // invalidate it.
    void show(TransientContentKindW10M kind, const String& html);

    // Records the substitute commit observed by the loader client: Starting
    // becomes Shown. Non-transient commits dismiss instead (see below).
    void noteCommitted();

    // Returns to Idle when a non-transient document commits over the
    // transient one; the navigation away is an ordinary history load.
    void noteDismissed();

    // Kind on screen or starting; None when Idle.
    TransientContentKindW10M kind() const;

    // Lifecycle state of the transient document.
    TransientPageStateW10M state() const;

    // Navigation ID allocated by the most recent show; 0 before the first.
    uint64_t transientNavigationId() const;

private:
    // Owning host; outlives this controller.
    PageHostW10M* m_owner;

    // Kind on screen or starting; None when Idle.
    TransientContentKindW10M m_kind { TransientContentKindW10M::None };

    // Lifecycle state of the transient document.
    TransientPageStateW10M m_state { TransientPageStateW10M::Idle };

    // Navigation ID allocated by the most recent show; 0 before the first.
    uint64_t m_transientNavigationId { 0 };
};

} // namespace WebCore

