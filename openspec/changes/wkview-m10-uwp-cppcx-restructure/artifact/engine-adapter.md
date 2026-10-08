# M10 engine adapter: ownership, lifecycle and security contracts

As-built record for tasks 4.1-4.9: which controller owns what, which thread
runs it, how each lifecycle ends exactly once, and where security decisions
are enforced. Companion to the per-task artifacts; it states the combined
contract those runs proved, not new behavior.

## Controllers and ownership

All state below lives in `Source/WebCore/platform/uwp`, runs on the owner
thread, and is held by `PageHostUWP` (one host per control) except where
noted. No engine header depends on C++/CX or WinRT projections
(`check-uwp-engine-purity.py`); WRL/WinRT use inside `ClipboardUWP.cpp`,
`PageUWP.cpp` and `TextureMapperCompositorUWP.cpp` is implementation detail
slated for neutral service callbacks in task 7.1.

| Owner | Lifetime | Thread |
|---|---|---|
| `PageHostUWP.page` | `WKCreatePageUWP` to `WKDestroyPageUWP` | Owner |
| `NavigationControllerUWP` | Host member; IDs never reused, terminals exactly once | Owner |
| `TransientPageUWP` | Host member; Idle/Starting/Shown, dismissed by ordinary commits | Owner |
| `CertificateExceptionStoreUWP` | Process map keyed by session; erased at teardown | Owner |
| `ResourcePolicyUWP` | Process map keyed by session; reset restores defaults | Owner |
| `StorageSessionProviderUWP` | Host-retained; `close()` destroys storage despite cached jars | Owner |
| `ResourceLoadUWP` | Per loader in the strategy map; cancel is idempotent | Owner |
| `LoaderStrategyUWP` | Installed platform strategy; suspend nesting counted | Owner |
| `EngineUWP` | Process singleton behind the init guard; one service pump | Owner |

Sessions are ephemeral per page (`generateEphemeralSessionID`): cookie jar,
certificate consent, resource policy, transient accounting and cache
partition all key on it, and teardown drops every entry. Controls never
share sessions; the loader strategy and MemoryCache are process-wide but
session-keyed.

## Lifecycle rules

- Every accepted command (navigate, loadHtml, reload, transient show)
  allocates a nonzero navigation ID that survives redirects and is never
  reused; traversals, fragment/push/replace mutations and same-document
  updates allocate nothing.
- Every ID reaches exactly one terminal (commit, veto, stop, replacement,
  provisional or committed failure). Late or repeated signals for settled
  IDs are dropped and counted, never re-recorded. Commit and failure signals
  attribute to the provisional owner, never blindly to the active ID.
- Transient content has its own ID with kind (generic/warning) and state
  (Starting/Shown); ordinary commits dismiss it. Reloading it reloads
  blankness; history gains no entry and the cursor is untouched.
- Certificate tokens bind session, failure revision, host and effective
  port; supersede, remote/history/reload/Stop and generic-content commands
  invalidate pending tokens while armed exceptions survive; teardown erases
  the session.
- Resource deadlines start at enqueue and include queue/redirect time;
  explicit suspension pauses them via recorded spans while visual unload
  never does. Budgets fail fast with distinguishable codes before copying.
- Host teardown stops loaders, releases the page, evicts the session's
  cache partition, closes cookie storage despite retained jars, and erases
  consent, policy and certificate state.

## Security enforcement

Deny by default at every layer: unsupported schemes and MIME fail closed,
policy vetoes record terminals synchronously, reserved document URLs grant
no consent, TLS verifies normally with per-site consented exceptions scoped
to session/host/port, and stale/used/cross-control tokens are rejected
without state change. No global flag or document URL bypasses verification.

## Test corpus: fixture phases and what they prove

The device suite in `FrameLoaderClientUWP.cpp` (78 phases, numbers stable)
is the lifecycle/security test corpus; each row below names the phases and
the marker families that must read true. Markers report values, never cookie
or token contents.

| Phases | Proves (markers) |
|---|---|
| 0-1 IndexProbes, Redirect | Resource delivery baseline; redirect commits under the command ID (`allocated`, `redirect-stable`, `http`, `url`) |
| 2-4 HistoryBack/Forward/Reload | Traversal allocates nothing; reload mints fresh and recommits (`traversal-stable`, `reload-allocated/new`) |
| 5-8 Fallback, Latin1, CSP, CORS | Error documents, encodings, policy enforcement, origin isolation |
| 9-13 Replacement, Stop | Replacement winner, stop-after-commit keeps commit (`replacement-committed`, `stop-committed`) |
| 14-17 ReloadDocument, Fragment, Back, HttpError | Same-document stability, branch truncation |
| 18-27 Scroll, PushState, ReplaceState | Restoration, push/replace entry semantics and ID stability |
| 28 Done | Suite completion gate |
| 29-30 Race | Same-turn replacement: loser Replaced, winner Committed |
| 31-33 Veto | Synchronous host veto, reentrant overlap resurrects nothing |
| 34-37 Loop, Download, RaceDelayed | Provisional failure and response veto with no commit; delayed loser stays Replaced; duplicates absorbed |
| 39-47 Transient | Loop-armed failure; warning origin/kind/commit/preservation/history/cursor; vetoed show changes nothing; generic invalidates; reload keeps history and terminal |
| 48-52 Isolation | Distinct sessions; own/isolated/cross cookies; teardown unregisters with survivor intact |
| 53-66 Certificates | Owner/peer arming at ID 1; cross/stale rejection without mutation; warning preservation; generic invalidation; single consumption with port/session isolation; reuse rejection; refailure; remote invalidation; peer teardown |
| 67-77 Budgets | Contract defaults and range validation; redirect/resource/transient/timeout codes; suspend survival with restore health |

Deliberately not device-proven (implemented, documented in task artifacts):
queue-overflow code 14 (unreachable: serialization precedes 129 concurrent
transfers), retry-execution after token consume (needs a TLS fixture),
CommittedFailure classification (no deterministic trigger), other web
storage APIs and session TLS configuration (tasks 4.11, 5.x).

## Verification for this task

- `cmake --build build-arm-uwp-fresh --target WebCore --parallel 8`
  links the actual ARMNT/AppContainer DLL with all 24 legacy exports plus
  discovery; no engine source changed, so no device run is claimed.
- `check-uwp-engine-purity.py` passes over every engine header.
- `check-wkuwp-legacy-surface.ps1` passes with both ARM32 compilers.
- `audit-source-docs.py` clean for touched files; `git diff --check` clean.
