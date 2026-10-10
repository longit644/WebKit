# M10 public API and naming contract

Companion to [../design.md](../design.md) and [native-abi.md](native-abi.md).

Revision: independent-review corrections. Target contract, not implementation.
Public namespace `WebKit`; `WebKitWebView : UserControl` stays in API/uwp root.
C++/CX declarations and compiler-generated metadata are authoritative.

## Properties and methods

| Member | Type / semantics |
|---|---|
| Source | Writable Uri^ dependency property; absolute HTTP(S), null transition table below |
| CanGoBack / CanGoForward | Read-only Boolean engine history availability |
| IsLoading | Read-only Boolean active main-navigation state |
| DocumentTitle | Read-only String^, empty if absent |
| EnsureInitializedAsync | New IAsyncAction^ per call, one shared internal initialization attempt |
| GoBack / GoForward | Void; unavailable direction no-op; actual engine traversal |
| Reload | Void; new navigation ID, no extra history entry; no document is a no-op |
| Stop | Void; cancel outstanding loads; no duplicate terminal event for an already-terminal document |
| NavigateToString | Void overloads described below; opaque-origin about:blank substitute document |
| ProceedWithCertificateFailure | Void, WebKitCertificateFailureToken^; consume the issuing control's current token and retry with a new navigation ID |
| Close | Void; immediate closing marker, cleanup/owed notifications at transaction boundary; idempotent |

Invalid arguments use E_INVALIDARG; wrong thread RPC_E_WRONG_THREAD; closed
mutations RO_E_CLOSED. Invalid/noncurrent certificate tokens use E_INVALIDARG.
Font configuration conflict uses HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED),
reported by initialization rather than silently changing another page's fonts.
Read-only final snapshots remain readable on the owner thread after Close.

## Source transitions

Source reflects the requested target while pending and the effective URI after
commit. Pre-commit failure/cancellation restores the prior committed URI or null.
SourceChanged reports only actual engine commits/same-document changes; property
change notifications also cover requested-value changes/restoration.

| State when Source=null is processed | Required transition |
|---|---|
| Created / Faulted, no document | Clear queued request; Source=null; no initialization or navigation start |
| Initializing | Clear queued request; initialization/joiners continue; no navigation ID invented |
| NavigationStarting dispatch | Mark that proposal canceled; after dispatch restore committed Source or null; emit one Cancelled terminal event |
| Provisional load before commit | Stop outstanding work; one Cancelled terminal outcome; restore previous committed Source or null |
| Committed but still loading | Stop remaining work; one Cancelled terminal outcome; keep committed URI/document |
| Ready / terminal document | Source restores/remains committed URI; do not clear document or history; Stop any outstanding subresources without a new main terminal event |
| Closing / Closed | Fail RO_E_CLOSED, except repeated Close itself |

Invalid URI/scheme assignment is rejected before enqueue/native side effects;
restore the previous DP value under the internal-update guard before raising
E_INVALIDARG. Unchanged non-null assignment is not Reload. During initialization
only the last queued Source remains, and unused values receive no Starting event.
Source=null never grants a blank privileged document; use NavigateToString.

During notification dispatch, navigation mutations are deferred commands. For a
reentrant Source setter, save the requested value and restore the currently
published DP under the guard; install the requested value when its command runs.
Getters inside a transaction therefore keep that transaction's snapshot.

## Configuration, trust and resource limits

Default constructor supports XAML and containing-package CA/font defaults. An
overload copies WebKitWebViewOptions^; it freezes on first initialization attempt.

| Option | Default / accepted range |
|---|---|
| CertificateAuthorityPath String^ | Packaged cacert.pem; empty means package default, not native-store fallback |
| FontDirectoryPath String^ | Packaged Fonts directory; empty means package default |
| MaximumConcurrentRequests UInt32 | 8; 1..16 active transfers per page session |
| MaximumResourceBytes UInt64 | 32 MiB; 64 KiB..64 MiB decoded response-body bytes per resource |
| MaximumBufferedBytes UInt64 | 16 MiB; 1 MiB..32 MiB transient undelivered network bytes per session |
| RequestTimeoutMilliseconds UInt32 | 30000; 1000..120000 |

Fixed limits: 128 queued requests per session and 10 redirects per request.
Queue overflow uses Network code 14; transient-buffer overflow code 13; resource
byte exhaustion retains adapter code 11; redirect failure retains code 8. Transport
timeout remains CURLcode timeout. Resource limits count delivered decoded bytes,
not Content-Length or compressed size alone. Buffering excludes parsed DOM/image/
font caches; those are not claimed as a total page-memory bound. Check caps before
copy/allocation; cancel overflowing requests rather than accumulating unbounded
worker-to-owner backlog. Timeout starts on enqueue, includes queue/redirect time,
and excludes explicit app suspension time; unload does not pause its clock.

CA bundle contents are snapshotted/validated at session creation. Each request
captures that immutable policy; custom bundle trust REPLACES native OS trust for
this session, never silently augments it. No mutable global SSL configuration.
Connection/TLS-session caches cannot cross page sessions/trust policies; disable
shared TLS/connection reuse in M10. Missing/invalid bundle fails initialization.
Certificate exceptions are scoped additionally to normalized HTTPS host/port.

Fonts are explicitly process-global: the first successful engine font setup
freezes a canonical directory plus font-content fingerprint until process exit.
Later identical options join; conflicting path/content fails before page creation
without changing existing pages. Initial setup failure does not freeze a usable
configuration. Per-control independent font registries are not promised.

## Public enums and arguments

| Enum | Explicit members |
|---|---|
| WebKitNavigationOutcome | Completed=0, Failed=1, Cancelled=2 |
| WebKitNavigationErrorDomain | None=0, Transport=1, Network=2, Engine=3, Platform=4 |
| WebKitSourceChangeKind | NewDocument=0, Fragment=1, PushState=2, ReplaceState=3, HistoryTraversal=4 |
| WebKitNavigationContentKind | Remote=0, HostHtml=1 |

Transport codes are CURLcode; Network codes are adapter values; Platform contains
HRESULT bits; Engine identifies engine errors. No conversion by numeric equality.

SDK TypedEventHandler<WebKitWebView^, Args^> delegates expose these copied fields:

| Event / argument type | Fields |
|---|---|
| NavigationStarting / WebKitNavigationStartingEventArgs | NavigationId UInt64; Uri Uri^; ContentKind; Cancel Boolean writable only during that synchronous dispatch |
| SourceChanged / WebKitSourceChangedEventArgs | Uri Uri^; IsNewDocument Boolean; ChangeKind |
| HistoryChanged / WebKitHistoryChangedEventArgs | CanGoBack Boolean; CanGoForward Boolean |
| DocumentTitleChanged / WebKitDocumentTitleChangedEventArgs | DocumentTitle String^ |
| NavigationCompleted / WebKitNavigationCompletedEventArgs | NavigationId UInt64; Uri Uri^; ContentKind; Outcome; ErrorDomain; ErrorCode Int32; HasHttpStatus Boolean; HttpStatus UInt32; CertificateFailureToken WebKitCertificateFailureToken^ (nullable); CanProceedWithCertificateFailure Boolean derived from token presence at creation |

Token presence is a snapshot, not a guarantee of later validity. Tokens expose
read-only FailedNavigationId and FailedUri for diagnostics, no public constructor,
numeric decision value, serialization or mutation. Object identity/hidden issuer
binding is checked along with the engine's opaque token; navigation IDs remain
page-scoped and may coincide in different controls. Equal IDs do not imply consent.

## Notification transactions and reentrant commands

An owner-thread transaction fixes an immutable payload/state snapshot and marks
all terminal obligations BEFORE property notifications. For a terminal A:
`mark A terminal -> publish IsLoading=false/PropertyChanged -> Completed(A)
-> finish transaction -> process queued commands`. Generation checks drop stale
engine work, not an already-created terminal delivery obligation.

Source/history/title transaction: update snapshot, notify changed properties,
raise corresponding event, finish, then execute commands. Starting transaction:
raise Starting, freeze Cancel, evaluate handler errors/closing/queued mutations,
then accept or generate the proposal's terminal outcome before processing commands.

Reentrant navigation/Stop/HTML/retry commands are validated on call and queued FIFO,
then revalidated on execution. A replacement/Stop/null request during Starting
invalidates that proposal before loading. A new remote load starts only after the
old load's Cancelled terminal transaction finishes. No command starts B between
A's IsLoading=false and Completed(A). Getter state remains A until transaction end.

Deferred execution rejection is diagnostic and does not strand following FIFO
work. Native admission rejection before Starting has no main ID/terminal obligation;
direct rejection restores prior requested Source and preserves the original HRESULT.
An unstarted readiness command is deferred: its failure restores committed Source
without changing successful initialization observations. Latest document intent
revalidates pending Source/HTML continuations; Stop/null Source clears it while
shared initialization continues. Readiness bookkeeping cannot veto a proposal.

Close immediately marks closing, rejects later commands, clears queued commands,
and requests engine cancellation. Actual destruction/revocation waits for active
dispatch unwind. Finish already-owed terminal transactions, create exactly one
Cancelled outcome for a nonterminal started proposal/load, then detach/revoke.
Close in Completed(A) never creates another terminal A. Final-release/app-shutdown
cleanup suppresses app calls; exactly-once public delivery applies while a live
consumer exists, not an already-destroyed object.

The adapter catches handler exceptions at the ABI boundary. A Starting-handler
failure aborts as Failed/Platform with its HRESULT unless Close/replacement/Cancel
already requires Cancelled. Other notification-handler failures are diagnostics;
do not change the recorded outcome, retry the same event, or escape into WebCore.
Completed is raised once to the event invocation machinery; a throwing subscriber
does not guarantee subsequent subscribers ran. Late Cancel writes fail
RO_E_CLOSED. Test PropertyChanged, Starting and Completed reentrancy explicitly.

## Initialization actions

Each EnsureInitializedAsync returns a DISTINCT action with its own completion
handler/cancel/close state, backed by one internal attempt. Action success means
page/session/controller readiness, NOT first navigation completion. After readiness,
resolve joiners before scheduling the latest queued Source as the next command.

One action's Cancel completes that caller Canceled/E_ABORT; its IAsyncInfo.Close
releases that caller's observation without canceling the shared attempt or other
joiners. Initialization continues even if all callers cancel: Source-driven use
and future callers may depend on it. Control Close cancels all pending joiners and
the shared attempt. Failure errors all live joiners with the same original HRESULT;
explicit retry uses frozen options. A post-ready call returns a fresh completed
action. Failed/closed caller actions never turn successful on a later retry.

## Host HTML and certificate-warning transitions

Signatures:

```text
NavigateToString(String^ html)
NavigateToString(String^ html, WebKitCertificateFailureToken^ warningToken)
ProceedWithCertificateFailure(WebKitCertificateFailureToken^ token)
```

Both HTML overloads use a new page-local navigation ID, ContentKind=HostHtml,
URI about:blank, one cancellable Starting, IsLoading and one terminal event.
Policy can veto; accepted HTML commits opaque-origin substitute data. No implicit
remote base/privileges, no network-document downloader shortcut. HTTP status is
absent. A veto/failure before commit retains the current document.

HTML is history-neutral transient content: do not insert/replace/remove engine
history items or change its cursor. Back/Forward traverse the existing engine
neighbors from that cursor and discard transient HTML when they commit. If no
neighbor exists the direction is unavailable; the prior remote document is NOT
invented as an extra Back entry. Reload reloads current HTML using retained host
text with a new ID, still without history insertion. A later remote navigation
uses the normal cursor and forward-branch truncation. Host text dies on Close or
replacement commit; it is never a shell history vector.

| Operation | Eligible decision token behavior |
|---|---|
| Eligible remote TLS terminal failure | Invalidate older token; issue one opaque token bound to control/page-session/failure revision/failed HTTPS URI |
| NavigateToString(html, exact current warningToken) | Validate before side effects; preserve this token through veto/failure/commit and repeated explicitly associated warning HTML |
| Plain NavigateToString(html) | Invalidate decision on execution, even if later vetoed; no accidental preservation |
| New remote proposal, Back/Forward/Reload/Stop, Source=null | Invalidate token on command execution, even when no-op/veto; warning Reload therefore ends eligibility |
| Invalid argument rejected before enqueue | No state/token side effect |
| ProceedWithCertificateFailure(current token) | Atomically consume before queuing retry; new Remote ID for exact stored target; no second use, even if retry is vetoed/fails |
| Close / final release / shutdown | Invalidate token and clear session exceptions |

BrowserPolicy stores the remote failed token separately from HTML navigation IDs.
It uses the preserving overload for warning content and ignores HostHtml terminal
events as certificate-failure replacements. Ordinary web content has no access
to token or methods; reserved URL schemes never grant consent. No wall-clock
expiry is promised: validity is determined by the transition table. Cross-control
submission fails even if both FailedNavigationId values equal 1.

Validated retry grants that session a normalized HTTPS host/effective-port
exception until Close; only the token's failed target may arm it. Cross-origin
redirects and other sessions use normal verification. The immutable trust policy
and exception partition must also govern TLS/connection-cache reuse.

## Metadata and deferred capabilities

Implemented declaration identity (task 5.1): M10 metadata revision 2 uses
`WebKit.IWebKitWebViewV2`, IID `B0CACD92-A344-4B7B-9E94-99C3C3BFB35C`, as the
control's default interface. This revision is distinct from compiler-emitted
WinRT type-version attributes. See `cx-component-contract.md` and
`webview-options.md` for declaration/configuration evidence and behavioral scope.

New class/argument/enum/token signatures get the M10 metadata revision; do not
reuse incompatible prototype interface identities. Generated WebKit.dll/WinMD
are the one packaged authority. Host script execution, shared/persistent profiles,
independent font registries, redirect-hop events and permission/download/new-window
deferrals remain deferred. Process-global engine DLL hot-unload is also deferred.

Capability reporting rule (task 7.4): the engine mask advertises a bit only
when its end-to-end production path works, so the mask is zero and every
unwired consumer path fails fail-closed (E_NOTIMPL/MissingCapability) instead
of claiming success. The per-bit engine state and unblocking conditions live in
the provider ledger (`Source/WebCore/platform/uwp/WKUWPEngineAPI.cpp`); the
per-service availability list lives in `API/uwp/USAGE.md`.
