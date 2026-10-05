# M10 acceptance and evidence matrix

Requirements live under [../specs/](../specs/); implementation work is tracked in
[../tasks.md](../tasks.md). Supporting contracts are siblings in `artifact/`.

All rows are pending. Each record includes task/scenario, source/build identity,
compiler/SDK/ABI/metadata versions, package/version, server fixture version,
device build, run ID, commands/exit status, observations and result. Historical
M9 records inform regression selection but do not pass an M10 row.

| Gate | Required evidence | Failure means |
|---|---|---|
| G0 Baseline | Worktree inventory including uncommitted work, known-good engine/package, M9 transfer ledger | Cannot remove or overwrite prototype baseline |
| G1 CX feasibility | ARM DLL link, runtime import closure, WinMD inspection, generated XAML provider, phone activation/QI/compiled markup/layout/binding/destruction and constructor failure | Block broad app/component migration |
| G2 Neutral ABI | Frozen exact schema/ARM layout, negotiation and snapshot retry, copied strings, two pages, context-release acknowledgment and CPU/GPU/service ownership | Block public event/control integration |
| G3 Navigation/control | Distinct init actions/readiness, null/invalid Source table, reference event traces, opaque token and warning HTML lifecycle, Close/wrong-thread | Block consumer migration |
| G4 Presentation/platform | ALL tasks 6.1-6.6 and 7.1-7.5: input/DPI/idle-load/unload/suspend, clipboard completion, failed enqueue/shutdown/global-hook revocation and two views | Partial rendering proof cannot complete combined G4 |
| G5 Apps/fixtures | Declarative MiniBrowser, no downloader/shadow history/log polling, CX diagnostic hosts, per-assertion replacement oracle comparison | Block old harness/prototype removal |
| G6 Package/acceptance | Matching dependencies/ABI/WinMD/assets, negative staging tests, phone controlled regressions, live-site observations and owner acceptance | M10 remains incomplete |

## Navigation and state cases

- Requested Source before init, latest-request replacement, invalid URI DP rollback,
  every null-state transition, commit/redirect and pre-commit Source restoration.
- Separate concurrent IAsyncAction handlers, one caller Cancel/Close without
  canceling others, control Close, readiness-before-first-network-result and retry.
- Same URL Reload -> new document lifecycle/ID; fragment/push/replace/history ->
  engine same-document classification; history branch truncation and scroll restoration.
- Policy veto, slow replacement, Stop before/after commit, committed failure, HTTP
  404 HTML, failed image: exactly-once IDs and domain/status attribution.
- Navigation from PropertyChanged/Starting/Completed, Close from each handler, callbacks
  queued after detach, two instances with interleaved completions and one closing.
- NavigateToString cancellable HostHtml lifecycle, about:blank, absent HTTP status,
  no remote privilege and unchanged engine history cursor/items; Back/Forward/Reload
  exact transient-document semantics; no hidden remote HTML injection.

## Resource/security cases

- External CSS screenshot/computed-style oracle, ordered scripts, PNG decode,
  font appearance plus internal renderer-private font test.
- Same-origin fetch/XHR, CORS allow/deny, CSP inline/image block, HTTPS mixed content.
- Cookie pass/fail structural report and two-session isolation; no cookie value logs.
- MIME/charset fidelity, relative resources, redirect limits, HTML main-document
  budget route, distinct octet-stream unsupported/download route, concurrency/
  buffer exhaustion and timeout without UI blocking.
- Valid/invalid TLS, no automatic bypass, explicit eligible consent, stale/used/
  cross-control token rejection, session close, cross-host/port redirect isolation.
- A and B both fail at local ID 1: A's opaque token must fail on B. Warning HTML
  preserving overload must retain A's token; generic HTML/remote/Stop/null/Close
  must invalidate; invalid rejected args must not mutate token state.
- Concurrent different CA bundles must produce correct independent trust outcomes;
  native store must not augment bundle, shared TLS/connection reuse disabled.
  Conflicting canonical font path/content must fail without changing another page.
- Compressed/deferred response bounds, queued-request cap, immutable options ranges,
  redirect deadline inclusion and suspension-versus-unload deadline behavior.

## Required notification trace oracles

Event ledger includes property notifications, content kind, page/local ID, outcome
and control state. Assert order/count, not merely final URI:

| Trigger | Required trace |
|---|---|
| A terminal IsLoading PropertyChanged requests B | A terminal recorded -> IsLoading=false notification -> Completed(A) -> Starting(B) |
| A terminal PropertyChanged calls Close | A state notification -> Completed(A) once -> detach/Closed; no B or extra A terminal |
| Starting(A) requests B or sets Source=null | Starting(A) returns -> Cancelled(A) -> process B/null; A never loads |
| Starting throws | Starting(A) -> Failed(A, Platform/HRESULT), unless cancellation/closing already requires Cancelled |
| TLS A then associated warning H then proceed R | Failed(A, token) -> Starting(H,HostHtml) -> Completed(H) -> consume token -> Starting(R,Remote) |
| Two initialization joiners; one Cancel | Only canceled caller ends Canceled; other ends success at readiness -> then latest queued Starting |

## Review finding resolution ledger

| Review finding | Frozen design / required task proof |
|---|---|
| P1-1 cross-control token collision | Opaque CX token and engine issuer/decision pair; 4.8, 5.1, 8.3 |
| P1-2 warning/HTML lifecycle | Explicit preserving overload, ContentKind and history/token transitions; 4.5, 5.6, 8.3 |
| P1-3 null Source | Per-state table and invalid DP rollback; 5.3 |
| P1-4 reentrancy | Transaction/deferred command trace oracles; 5.5, 5.7, 5.9 |
| P1-5 ABI precision | Exact pack4 declarations/offsets/table/ownership; 3.0 before 3.1-3.6 |
| P1-6 global configuration | CA-file-only session isolation/global-font conflict; 4.11 |
| P2-1 async joiners | Per-caller action ownership/readiness milestone; 5.4 |
| P2-2 final release/unload | Runtime tickets/quiescence/process pinning; 3.3, 7.5 |
| P2-3 G4 ordering | Combined gate after rendering AND section 7; 6.6, 7.5 |

These entries resolve design choices; none claim implementation/test completion.

## Fixture extraction protocol

Carry every row/phase in M9 fixture-extraction-plan.md into the new result ledger.
Host fixture reports use run ID + scenario/assertion IDs and Boolean/structural
results. Server and device runner independently verify expected requests/events;
timeouts, missing reports or malformed payloads fail aggregate results. Old and
new suites run against the same fixture revision before old engine probes are
removed. CSS text presence does not prove styling; server response alone does
not prove CORS response exposure; renderer-private checks are internal tests.

Run Wikipedia Main_Page and the owner's GitHub repository with exact URLs,
interaction/failed-resource records and owner visual observations after controlled
tests. Unsupported site features remain documented, not converted into a pass.
