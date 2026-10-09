// Host contract checks for the native value state used by the real projection.
#include "NavigationStateUWP.h"

#include <cstdio>

using namespace WebKit;
// Assertions evaluated by this native-state run.
static unsigned checks;
// Failed assertions; any nonzero count fails the process.
static unsigned failures;
// Print and aggregate a state-contract assertion.
static void check(bool result, const char* description)
{
    ++checks;
    if (!result) ++failures;
    std::printf("%s: %s\n", result ? "PASS" : "FAIL", description);
}

// Build a frozen V1 signal with explicit identity/generation/ordering for this page.
static WKUWPPageEvent event(uint64_t id, uint64_t generation, uint64_t sequence, uint32_t kind)
{
    WKUWPPageEvent value { };
    value.header = { sizeof(value), WKUWP_STRUCTURE_VERSION };
    value.pageId = 1;
    value.navigationId = id;
    value.documentGeneration = generation;
    value.callbackSequence = sequence;
    value.eventKind = kind;
    return value;
}

// Verify the native null-state table, identity guards and candidate-publication behavior.
int main()
{
    NavigationStateUWP state;
    check(state.snapshot().source.empty() && state.snapshot().title.empty() && !state.snapshot().isLoading
            && !state.snapshot().canGoBack && !state.snapshot().canGoForward, "initial null/false/empty snapshot");
    check(state.assignSource({ }) == SourceActionUWP::None && state.controlPhase() == ControlPhaseUWP::Created, "Created null performs no initialization");
    check(state.assignSource(L"https://example.test/a") == SourceActionUWP::Initialize && !state.activeNavigationId(), "first Source starts one initialization without main ID");
    check(state.assignSource(L"https://example.test/b") == SourceActionUWP::None && state.snapshot().source == L"https://example.test/b", "Initializing Source keeps latest target");
    check(state.assignSource({ }) == SourceActionUWP::None && state.controlPhase() == ControlPhaseUWP::Initializing, "Initializing null does not cancel attempt");
    check(state.finishInitialization(1, S_OK) && state.takeInitialSource().empty(), "cleared queue produces no navigation at readiness");

    NavigationStateUWP fault;
    fault.assignSource(L"https://example.test/a");
    check(fault.finishInitialization(0, E_ACCESSDENIED) && fault.controlPhase() == ControlPhaseUWP::Faulted && fault.snapshot().source.empty(), "initialization error restores prior null snapshot");
    check(fault.initializationError() == E_ACCESSDENIED && fault.assignSource({ }) == SourceActionUWP::None, "Faulted null retains error and starts no retry");
    check(fault.assignSource(L"https://example.test/retry") == SourceActionUWP::Initialize, "explicit Source retry begins new attempt");
    fault.assignSource(L"https://example.test/latest");
    check(fault.finishInitialization(1, S_OK) && fault.takeInitialSource() == L"https://example.test/latest", "retry/readiness adopts only latest pending URI");
    check(fault.takeInitialSource().empty(), "initial target consumed once");

    auto start = event(1, 0, 1, WKUWPEventStarting);
    check(state.acceptSequence(start) && state.starting(start, L"https://example.test/a") && state.snapshot().isLoading, "Starting ID publishes loading and requested URI");
    check(state.assignSource({ }) == SourceActionUWP::Stop && state.snapshot().source.empty(), "Starting null restores committed null and requires cancellation");
    auto cancelled = event(1, 0, 2, WKUWPEventCompleted);
    check(state.acceptSequence(cancelled) && state.completed(cancelled) && !state.snapshot().isLoading, "cancelled proposal reaches one terminal");
    check(!state.completed(cancelled), "duplicate terminal cannot change state");
    start = event(2, 0, 3, WKUWPEventStarting);
    state.acceptSequence(start);
    state.starting(start, L"https://example.test/b");
    state.acceptedStarting();
    auto commit = event(2, 1, 4, WKUWPEventSourceChanged);
    state.acceptSequence(commit);
    check(state.sourceChanged(commit, L"https://example.test/redirected", L"Title") && state.navigationPhase() == NavigationPhaseUWP::Committed, "commit adopts effective URI/document generation");
    check(state.assignSource({ }) == SourceActionUWP::Stop && state.snapshot().source == L"https://example.test/redirected", "committed-loading null preserves current document");
    cancelled = event(2, 1, 5, WKUWPEventCompleted);
    state.acceptSequence(cancelled);
    check(state.completed(cancelled) && !state.snapshot().isLoading && state.snapshot().title == L"Title", "committed cancellation retains copied document title");
    check(state.assignSource({ }) == SourceActionUWP::Stop && !state.activeNavigationId(), "terminal-document null requests subresource stop without main ID");
    check(state.assignSource(L"https://example.test/redirected") == SourceActionUWP::None, "same URI never invents Reload");
    start = event(3, 1, 6, WKUWPEventStarting);
    state.assignSource(L"https://example.test/fail");
    state.acceptSequence(start);
    state.starting(start, L"https://example.test/fail");
    state.acceptedStarting();
    auto same = event(0, 1, 7, WKUWPEventSourceChanged);
    check(state.sourceChanged(same, L"https://example.test/redirected#old", L"Title") && state.snapshot().source == L"https://example.test/fail", "old-document same-page change cannot overwrite pending requested Source");
    cancelled = event(3, 1, 8, WKUWPEventCompleted);
    check(state.completed(cancelled) && state.snapshot().source == L"https://example.test/redirected#old", "precommit failure restores latest committed same-document URI");
    auto stale = event(0, 0, 9, WKUWPEventTitleChanged);
    check(!state.titleChanged(stale, L"Stale") && state.snapshot().title == L"Title", "old generation cannot overwrite title");
    auto history = event(0, 1, 10, WKUWPEventHistoryChanged);
    history.canGoBack = 1;
    check(state.historyChanged(history) && state.snapshot().canGoBack && !state.snapshot().canGoForward, "typed history snapshot");
    auto foreign = history;
    foreign.pageId = 2;
    check(!state.acceptSequence(foreign), "foreign page does not alter callback ordering");
    state.acceptSequence(history);
    check(!state.acceptSequence(history), "duplicate callback sequence rejected");
    auto candidate = state;
    candidate.assignSource(L"https://example.test/candidate");
    check(state.snapshot().source != candidate.snapshot().source, "candidate state preparation cannot mutate published snapshot");
    state.markClosing();
    check(state.controlPhase() == ControlPhaseUWP::Closing, "closing marker is immediate");
    state.markClosed();
    check(state.controlPhase() == ControlPhaseUWP::Closed && !state.snapshot().isLoading
            && state.snapshot().source == L"https://example.test/redirected#old", "closed final snapshot stays committed and readable");
    history.callbackSequence = 99;
    check(!state.acceptSequence(history), "late event cannot revive closed state");
    std::printf("M10 native Source state: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
