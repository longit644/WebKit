# W10M public component

The M10 public namespace is `WebKit`, with `WebKitWebView : UserControl` authored
in this directory. Public filenames follow the runtime-class names without a
`W10M` suffix; native implementation helpers in `native/` retain that suffix.

## Metadata revision 2

The control's default interface is `IWebKitWebViewV2`, IID
`B0CACD92-A344-4B7B-9E94-99C3C3BFB35C`. This is an incompatible revision of the
historical WRL prototype: `Source` is a `Windows.Foundation.Uri`, events use SDK
`TypedEventHandler<WebKitWebView, Args>`, and certificate consent uses an opaque
object. The full contract is
`openspec/changes/wkview-m10-w10m-cppcx-restructure/artifact/public-api.md`.

`WebKit.dll` and `WebKit.winmd` are built from the same MSVC ARM32 `/ZW` objects.
The build's `ContractRevision=2` identifies the M10 revision. The compiler's
ordinary WinRT type-version attributes remain version 1; contract revision is
not an OS-contract version. No historical IDL or generated prototype header is
an input to this build.

Only `WebKitWebView` and `WebKitWebViewOptions` have public activation. Event
argument constructors and `WebKitCertificateFailureToken` construction are
internal. The token exposes only read-only `FailedNavigationId` and `FailedUri`;
its control identity and native issuer/decision pair stay out of WinMD.

See [USAGE.md](USAGE.md) for compiled-XAML embedding, options/async examples,
Source/null rules, typed events, warning/retry flow, exact errors and deferred APIs.

## Build and verification

These drivers require PowerShell 7 and discover the installed ARM32 MSVC/SDK
inputs. Use a separate fresh output directory for each command.

```powershell
.\Tools\w10m\build\build-arm32-cx-component.ps1 -OutDir <fresh-output-directory>
.\Tools\w10m\build\test-cx-component-contract.ps1 -OutDir <fresh-test-directory>
.\Tools\w10m\build\test-webview-configuration.ps1 -OutDir <fresh-configuration-test-directory> -FontDirectory <bundled-font-directory>
```

The first command verifies the ARMNT/AppContainer image, activation exports and
compiler-derived signatures. The second builds an x64 test copy, compiles an
external consumer using only the generated WinMD, checks that forbidden
construction/setters fail compilation, and tests factories and projected
defaults. This is host contract evidence; compiled-XAML phone identity/binding
evidence is provided separately by `Tools/w10m/CXWebViewCompiledProbe`.

## Configuration snapshots

The options constructor copies all six values into a private immutable native
`WebViewConfigurationW10M`. Later mutations of the application's options object
cannot alter it. Empty/null paths select `cacert.pem` and `Fonts` in the
containing package; relative custom paths also resolve against that package.
Paths are lexically canonicalized with Windows case normalization, rather than
resolved against the process working directory.

The initialization entry validates ranges before filesystem/page use, reads and
parses the PEM bundle, validates the readable required DejaVu regular/bold faces,
and calculates SHA256 over CA bytes and sorted font names/contents. The validated
holder retains the actual CA bytes for subsequent initialization/page creation;
retry reuses that holder. Missing files preserve their Windows HRESULT, malformed
CA data returns `HRESULT_FROM_WIN32(ERROR_INVALID_DATA)`, and invalid ranges/paths
return `E_INVALIDARG`. Native ABI views borrow these owned buffers. Certificate
parsing does not consult a Windows trust store. Engine font parsing/setup remains
the initialization authority.

The configuration test uses isolated copied fixtures, including a deliberate CA
file mutation. Its native validator neither creates a page nor initializes the
engine. Actual page/session consumption is connected by later controller tasks.

## Implementation progress

Tasks 5.1-5.9 deliver the public shape, immutable copied event payloads, synchronous
Starting.Cancel dispatch guard, options validation, activation boundaries and
observable Source/history/loading/title snapshots. Source follows its requested,
committed/restored and null-transition contract. Reentrant setters preserve the
current getters/DP and execute FIFO after property/typed-event dispatch.
`EnsureInitializedAsync` now returns a distinct SDK action per call over one
generation-bound shared attempt. Independent Cancel and IAsyncInfo.Close affect
only that observation; control Close cancels all pending actions. Validated options
and retained CA bytes survive retry. Every live joiner resolves before latest-Source
navigation is queued; success is page/controller readiness rather than first-load
completion. `platform/InitializationActionW10M` owns the private CX action state.

Production page forwarding remains the next integration step; the current default
reports E_NOTIMPL through a failed action instead of successful placeholder readiness.
Public navigation/HTML/retry commands now dispatch through a bound frozen page table.
The real-control diagnostic in `Tools/w10m/CXStateTests/` passes 33 async and 42
property/transaction assertions with controlled readiness/V1 signals and actual
package-default validation. See the task checklist for behavioral gates.

`platform/PageCallbacksW10M` now supplies the actual retained frozen V1 attachment
with a weak per-control target and active-dispatch lifetime protection. Copied
typed arguments preserve error domains/codes, content policy and explicit HTTP
presence; all public state is installed before property/typed-event delivery.
Eligible structured HTTPS failures create hidden control/issuer/revision-bound
token objects. Old event token presence remains a snapshot after current consent
is invalidated. The version-1.0.0.5 diagnostic passes 37 retained-table callback/
token assertions in addition to both existing device suites.

`native/PageControllerW10M` consumes a negotiated/runtime-pinned V1 page table.
Back/Forward/Reload/Stop and both host-HTML overloads use copied, revalidated
deferred commands. Host HTML has no remote base URI; retained text is adopted
only on commit and reloaded history-neutrally. Warning association requires the
exact current object; generic HTML/reload/no-op history still invalidate pending
consent. Retry consumes local eligibility before enqueue and calls provider atomic
decision consumption with a new main ID. The version-1.0.0.7 frozen-table provider
diagnostic passes 27 command assertions alongside all three existing suites.
The framework owns no history vector; real engine DOM/TLS enforcement and slot
forwarding retain their integration gates.

Ordinary Close now finishes owed property/policy/terminal transactions before
revoking subscriber roots and releasing instance-owned contexts/configuration.
`platform/EventHandlersW10M` snapshots invocation and supports explicit final
revocation, including captured-object/control cycles. Native Pending close keeps
ownership until the engine releases its retained callback attachment; repeat Close
does not reissue cleanup. All mutation/event-add paths enforce owner thread and
closing/closed HRESULTs, while final read-only values remain available on the owner.
The version-1.0.0.8 diagnostic passes 24 lifecycle/weak/thread assertions plus
all four regression suites. Last-release/dispatcher-loss/global cleanup tickets
remain task 7.5; temporary visual unload/presentation remains section 6.

The compiled-XAML contract host imports only public generated WebKit metadata,
declares real WebKitWebView/URI Source/bindings and passes 13 phone assertions
with generated type-provider/XBF/PRI checks. The build also records actual authored
compiler inputs and runs `check-public-control-contract.py`, rejecting prototype,
legacy-log and framework-fixture event sources. Renderer/services production
integration remains distinct from declarative public-contract acceptance.

Notification transactions now distinguish immediate rejection from failed deferred
execution. Rejected queued work records internal diagnostics and does not strand
later FIFO commands or lose an owed terminal. Readiness and document-intent epochs
prevent obsolete initial Source/HTML requests, and bookkeeping markers cannot veto
real proposals. Version 1.0.0.10 passes 35 transaction assertions, including 13 exact
reference traces, plus all 163 existing device assertions. See
`artifact/notification-transactions.md` in the M10 change for the full scope,
reference ordering and payload/report identities.

All authored functions/methods, constants, enums and types require documentation
comments. `python Tools/w10m/audit-source-docs.py --check <authored-paths>` (or the
`Tools/w10m/audit-source-docs.ps1`/`.sh` forwarding wrappers) checks CX/native
declarations and spacing across the implemented M10 placement
(`Source/WebKit/Shared/w10m/`, `Source/WebCore/platform/w10m/`, this directory
and `Tools/w10m/`), including this directory's authored `WebKitWebView.h`.
MIDLRT outputs, `legacy-generated/` directories and `*.g.*` XAML/MIDL sources
are excluded as generator-owned build outputs; never hand-edit them.

## Native presentation extraction

Task 6.1 adds `rendering/SwapChainPresenterW10M` and `FrameMetricsW10M`, compiled
without `/ZW`. CPU owns DirectX upload/swap-chain resources; GPU uses the frozen
table with engine-owned compositor resources. Active mode replacement requires
detach, and Pending release acknowledgement is bound to an attachment cookie.
The native ownership/metrics gate passes 49 checks and both ARM compilers;
Lumia CPU presentation and retained Skia demo/counter/scrolling pass. See
`rendering/README.md` and M10 `artifact/swap-chain-presenter.md` for exact units,
ownership, builds and device identities. Horizontal rendering remains incorrect
and task 6.3 stays open. New-control visual/service/runtime integration retains
the later rendering and cleanup gates.

Task 6.2 adds `rendering/FrameSchedulerW10M` and the private CX
`platform/FrameSchedulerDriverW10M`: shared service epochs, independent view
visibility/leases, one owner service timer and weak rendering/wake hooks.
Service-capable bound pages join automatically and retain service through Pending
Close. The real engine's negotiated service/suspension slots now forward native
owner work, while page capability mask zero/default Unsupported initialization
still prevents false readiness. Native checks pass 34/34; Lumia v1.0.0.14 passes
22 actual paused-resource/timer/peer assertions and all 198 existing assertions.
See M10 `artifact/shared-frame-scheduling.md`; horizontal rendering remains 6.3.

Task 6.5 adds `rendering/RenderSubscriptionW10M`: exactly-once Rendering
subscription across unload/load, suspension, frame failure and Close. The native
wrapper pauses/resumes callbacks on root Loaded/Unloaded and OS suspend without
tearing down page state; the MiniBrowser shell forwards Suspending/Resuming.
Native checks pass 35/35; Lumia v1.0.0.43 passes power-button suspend/resume
cycles with continued rendering. See M10 `artifact/lifecycle-subscriptions.md`.

Task 6.4 adds `input/InputAdapterW10M` and `input/ScrollControllerW10M`: gesture
classification with tap slop/offset/cancel semantics plus kinetic/edge physics
behind narrow engine ports. The native wrapper delegates with identical log
stages; V1 keyboard/focus forward with validation and explicit `E_NOTIMPL`
against the real engine. Native checks pass 54/54; Lumia v1.0.0.42 passes
tap/scroll/fling interaction. The v42 shell also serializes first-time window
creation after portal launch exposed an OnLaunched/OnActivated race tripping the
process owner gate. See M10 `artifact/input-controllers.md`.

Task 6.3 adds presenter lost-device recovery with one rendering-child owner:
`deviceRemovedReason()` plus `recoverDevice()`, which rebuilds device,
context, chain and upload on the retained panel, geometry and scale after
releasing the dead chain first; any rebuild failure lands Detached. The
wrapper maps removed/reset failures from present, cached-present and resize
into one rebuild plus a single retry, with a diagnostic drill and shell
Recover button proving a real-driver rebuild on device. Host checks pass
67/67; Lumia v1.0.0.49 passes rotation/scale/pinned-layer behavior with a
single attachment and exactly one counted recovery. The landscape system-bar
overlay stays shell chrome for task 8.1. See M10
`artifact/resize-recovery.md`; G4 is complete.

Task 7.1 adds `platform/DispatcherW10M`, `platform/ClipboardServiceW10M` and
`platform/ClipboardPlatformW10M`: an internal owner-thread dispatcher hop, a
neutral host-side V1 clipboard service with exactly-once completion/cancel
semantics, and the CX WinRT text-transport boundary. The service compiles
without `/ZW` on both ARM compilers and without C++ unwinding under the native
toolchain; an empty clipboard completes OK with an empty span. The borrowed
table leaves `postWake` null until the 7.5 runtime owner wires it. Host checks
pass 83/83 with the engine purity gate; WinRT copy/paste behavior stays 7.2.
See M10 `artifact/clipboard-service.md`.

Task 7.2 adds `platform/ClipboardRouteW10M`: owner-side copy/cut/paste routing
over the service with retained per-operation tokens, monotonic IDs and
erase-before-delivery. Close cancels every pending route operation exactly
once and latches idempotently; later calls fail Closed. Host checks pass
89/89; Lumia v1.0.0.44 passes 27 real round-trip/worker/close-race assertions
plus all 220 existing assertions. See M10 `artifact/clipboard-routing.md`.

Task 7.5 adds `platform/RuntimeOwnerW10M`: ticketed clipboard routing with
acknowledged cancellation, hook revocation, shutdown with quiescence,
dispatcher-loss drops with synchronous close release, engine pin survival and
close/recreate generations. It builds the engine-facing table with owner
context and a working `postWake`. Host checks pass 137/137; Lumia v1.0.0.45
passes 37 owner assertions plus all 247 existing assertions. Production control
adoption belongs to section 8; G4 waits solely on task 6.3. See M10
`artifact/runtime-owner.md`.

Native acronym spelling uses CPU/GPU/OK (`PresentationModeW10M::CPU/GPU`,
`WKW10MResultOK`, `WKW10MCapabilityCPUPaint/GPUCompositor`). Mixed-case spellings are
fully replaced without aliases, per owner instruction. See M10
`artifact/acronym-conventions.md` for the convention and unchanged binary layout proof.
