#pragma once

#include "PageW10M.h"
#include <wtf/text/CString.h>
#include <wtf/text/WTFString.h>

// Per-page navigation identity, generations and typed signals for the frozen
// V1 WKW10M contract. Moved-out attribution state plus new identifier rules
// (M10 task 4.3): every accepted navigation command allocates a nonzero ID
// that survives redirects and is never reused, the document generation bumps
// only on committed new documents, and source, title, HTTP status, change
// kind, redirect hops and the failing URL travel as typed values instead of
// log-scraped strings. History availability stays a live core read; only the
// V1 getState work wires it, so this controller records no availability cache
// that loader-internal traversals could stale.
// Per-page navigation controller. Owns identity, generations and signals.
namespace WebCore {

// Allocates nonzero navigation IDs, bumps generations only on new-document
// commits, and holds the typed source, title, HTTP, change, redirect and
// failure state the loader signals report.
enum class NavigationOutcomeW10M : unsigned {
    // No terminal recorded yet; the navigation may still be in flight.
    None = 0,
    // A new document committed under this ID.
    Committed = 1,
    // A policy decision refused the navigation before transfer.
    Vetoed = 2,
    // Stop killed the load; recorded from the cancelled failure signal.
    Stopped = 3,
    // A newer command superseded this ID before it reached a terminal.
    Replaced = 4,
    // The provisional load failed without cancellation.
    ProvisionalFailure = 5,
    // The committed load failed afterwards, such as a teardown race.
    CommittedFailure = 6,
};

// Per-page navigation controller. Owns identity, generations, signals and
// the exactly-once terminal ledger: every accepted ID reaches one terminal
// (commit, veto, stop, replacement or failure) and late signals for settled
// IDs are dropped and counted, never re-recorded.
class NavigationControllerW10M {
public:
    // Builds a controller with no active navigation and zero generations.
    NavigationControllerW10M();

    // Non-copyable navigation identity; one controller owns one page's IDs.
    NavigationControllerW10M(const NavigationControllerW10M&) = delete;

    // Non-copyable navigation identity; one controller owns one page's IDs.
    NavigationControllerW10M& operator=(const NavigationControllerW10M&) = delete;

    // Accepts one navigation command (navigate, loadHtml, reload, or the
    // fixture equivalent): allocates the next nonzero ID, records it active
    // and returns it. Returns 0 on exhaustion, which is practically
    // unreachable for 64 bits; a caller that needs a contract result maps 0
    // to InvalidState before Starting, because a failed command allocates no
    // ID. Redirects, same-document updates and history traversals never call
    // this, so an allocated ID survives them by construction. A previous
    // command still without a terminal is recorded Replaced exactly once,
    // because the newer command guarantees the older one can never complete.
    uint64_t beginNavigation();

    // ID of the accepted command in flight, or 0 when none was accepted yet.
    uint64_t activeNavigationId() const;

    // Records a provisional load: bumps the load generation used for probe
    // staleness, clears the per-navigation signals, and returns the new
    // generation. Titles, HTTP status, change kind and redirect hops belong
    // to one navigation attempt, so a new attempt invalidates them all. Also
    // takes ownership of the open provisional for the active ID, so a late
    // failure from a superseded load attributes to its own ID (already
    // terminal, therefore dropped) instead of the newer active one.
    unsigned noteProvisionalLoad();

    // Transport error code of the latest main-frame failure for the current
    // attempt (adapter-domain budget/veto codes or the curl code); 0 when
    // the attempt has not failed. Distinguishes budget, timeout, redirect
    // and policy failures without scraping log lines.
    void setTransportError(int code);

    // Transport error code of the latest main-frame failure; 0 when none.
    int transportError() const;

    // ID of the most recently committed navigation; 0 before the first.
    uint64_t lastCommitId() const;

    // Load generation for probe staleness; deferred probes carry their own.
    unsigned loadGeneration() const;

    // Records a committed new document: bumps the document generation and
    // clears the recorded failure, which only the current failure may back.
    // Same-document updates never commit, so they never reach here and the
    // generation classifies exactly what the contract requires. The commit is
    // the success terminal for the active ID; a commit arriving for an
    // already-terminal ID is dropped and counted, never re-recorded, so a
    // canceled ID can never be resurrected by a late signal.
    void noteDocumentCommit();

    // Records a provisional-load failure for the active ID: cancellation
    // (Stop, replacement) is Stopped, anything else is ProvisionalFailure.
    // Late or repeated failures for an already-terminal ID are dropped and
    // counted, preserving exactly-once.
    void noteProvisionalFailure(bool wasCancelled);

    // Records a committed-load failure for the active ID, such as a teardown
    // race after commit. Already-terminal IDs are dropped and counted. The
    // failure attributes to the ID that committed the current source, because
    // a detached older document's death rattle can arrive after a newer
    // command already began.
    void noteCommittedFailure();

    // Records a terminal outcome for the active ID from a synchronous path
    // that preempts loader signals, such as a policy veto: returns true when
    // newly recorded, false when the ID was already terminal (or none).
    // Later loader signals for a vetoed ID therefore land as counted
    // duplicates, never as a second terminal.
    bool cancelNavigation(NavigationOutcomeW10M outcome);

    // Whether a just-finished load must not be treated as success: true when
    // the active ID already reached a non-commit terminal. Never mutates the
    // ledger; finishing only observes it, so a canceled ID stays canceled no
    // matter how late its signals arrive.
    bool noteLoadFinished() const;

    // Terminal outcome recorded for an ID, or None when it has none yet.
    NavigationOutcomeW10M terminalOutcome(uint64_t navigationId) const;

    // Late or repeated terminal signals dropped so far; evidence that stale
    // loader callbacks arrived and were absorbed without changing outcomes.
    unsigned duplicateTerminalSignals() const;

    // Counts one stale signal absorbed without ledger change: the caller
    // proved the signal belongs to an older document than the current one.
    void noteStaleSignal();

    // Document generation; increments on every committed new document only.
    uint64_t documentGeneration() const;

    // Committed source URI; empty until the first commit.
    void setSource(const String& uri);

    // Committed source URI; empty until the first commit.
    const String& source() const;

    // Committed document title, refreshing the owned UTF-8 copy with it.
    void setTitle(const String& title);

    // Committed document title; empty when the document reported none.
    const String& title() const;

    // Owned UTF-8 copy of the title, valid while this controller lives.
    const char* titleUtf8() const;

    // Main-document HTTP status of the committed response.
    void setHttpStatus(bool hasStatus, unsigned status);

    // Whether a main-document response supplied an HTTP status yet.
    bool hasHttpStatus() const;

    // HTTP status of the committed main-document response; 0 when absent.
    unsigned httpStatus() const;

    // Same-document change classification for the committed navigation.
    void setChange(WKDocumentChangeKindW10M change);

    // Same-document change classification for the committed navigation.
    WKDocumentChangeKindW10M change() const;

    // Counts one server redirect hop for the active navigation.
    void noteRedirect();

    // Server redirect hops reported for the active navigation.
    unsigned redirectHops() const;

    // Records the failing main-frame URL backing a proceed gesture.
    void setFailedUrl(const String& url);

    // Arms failure preservation across the next commit for a warning
    // interstitial; generic content leaves this disarmed so its commit
    // invalidates the recorded failure. Consumed by every commit.
    void setFailurePreserved(bool preserve);

    // Retires the recorded failure; only the current failure may back one.
    void clearFailedUrl();

    // Failing main-frame URL backing the interstitial's proceed link.
    const String& failedUrl() const;

private:
    // Next ID to hand out; starts at 1 because 0 means "no navigation".
    uint64_t m_nextNavigationId { 1 };

    // ID of the accepted command in flight; 0 when none was accepted yet.
    uint64_t m_activeNavigationId { 0 };

    // Whether the active ID already reached its exactly-once terminal.
    bool m_activeSettled { false };

    // Bounded terminal ledger: replaced, vetoed, stopped, committed and
    // failed IDs stay queryable so delayed signals cannot change them.
    // A fixture page issues dozens of navigations at most; the oldest entry
    // is evicted past the cap, which the suite never approaches.
    static constexpr size_t kMaxTerminalEntries = 32;
    uint64_t m_terminalIds[kMaxTerminalEntries] {};
    NavigationOutcomeW10M m_terminalOutcomes[kMaxTerminalEntries] {};
    size_t m_terminalCount { 0 };

    // Late or repeated terminal signals dropped to preserve exactly-once.
    unsigned m_duplicateTerminalSignals { 0 };

    // ID owning the currently open provisional load; 0 when none is open.
    // Commit and provisional-failure signals attribute to this ID rather
    // than the active one, because a superseded load's death rattle routinely
    // arrives after its replacement already began.
    uint64_t m_provisionalOwnerId { 0 };

    // Transport error code of the latest main-frame failure; 0 when none.
    int m_transportError { 0 };

    // ID that committed the current source; committed-load failures
    // attribute to it so a detached older document cannot fail a newer ID.
    uint64_t m_lastCommitId { 0 };

    // Records one terminal for an ID: true when newly recorded, false when
    // the ID was already terminal (counted as a dropped duplicate). Marks
    // the active ID settled when the recorded ID is the active one.
    bool noteTerminalOutcome(uint64_t navigationId, NavigationOutcomeW10M outcome);

    // Bumped per provisional load so deferred probes of replaced documents drop.
    unsigned m_loadGeneration { 0 };

    // Increments on every committed new document, never on same-document change.
    uint64_t m_documentGeneration { 0 };

    // Committed source URI; empty until the first commit.
    String m_source;

    // Committed document title; empty when the document reported none.
    String m_title;

    // Owned UTF-8 copy of the title with call-lifetime validity.
    CString m_titleUtf8;

    // Whether a main-document response supplied an HTTP status yet.
    bool m_hasHttpStatus { false };

    // HTTP status of the committed main-document response; 0 when absent.
    unsigned m_httpStatus { 0 };

    // Same-document change classification for the committed navigation.
    WKDocumentChangeKindW10M m_change { WKDocumentChangeKindNoneW10M };

    // Server redirect hops reported for the active navigation.
    unsigned m_redirectHops { 0 };

    // Failing main-frame URL backing the interstitial's proceed link.
    String m_lastFailedUrl;

    // Preservation armed by a warning interstitial for the next commit only.
    bool m_preserveFailure { false };
};

} // namespace WebCore

