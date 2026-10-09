# UWP public component

The M10 public namespace is `WebKit`, with `WebKitWebView : UserControl` authored
in this directory. Public filenames follow the runtime-class names without a
`UWP` suffix; native implementation helpers in `native/` retain that suffix.

## Metadata revision 2

The control's default interface is `IWebKitWebViewV2`, IID
`B0CACD92-A344-4B7B-9E94-99C3C3BFB35C`. This is an incompatible revision of the
historical WRL prototype: `Source` is a `Windows.Foundation.Uri`, events use SDK
`TypedEventHandler<WebKitWebView, Args>`, and certificate consent uses an opaque
object. The full contract is
`openspec/changes/wkview-m10-uwp-cppcx-restructure/artifact/public-api.md`.

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
.\Tools\uwp\build\build-arm32-cx-component.ps1 -OutDir <fresh-output-directory>
.\Tools\uwp\build\test-cx-component-contract.ps1 -OutDir <fresh-test-directory>
.\Tools\uwp\build\test-webview-configuration.ps1 -OutDir <fresh-configuration-test-directory> -FontDirectory <bundled-font-directory>
```

The first command verifies the ARMNT/AppContainer image, activation exports and
compiler-derived signatures. The second builds an x64 test copy, compiles an
external consumer using only the generated WinMD, checks that forbidden
construction/setters fail compilation, and tests factories and projected
defaults. This is host contract evidence; compiled-XAML phone identity/binding
evidence is provided separately by `Tools/uwp/CXWebViewCompiledProbe`.

## Configuration snapshots

The options constructor copies all six values into a private immutable native
`WebViewConfigurationUWP`. Later mutations of the application's options object
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
completion. `platform/InitializationActionUWP` owns the private CX action state.

Production page forwarding remains the next integration step; the current default
reports E_NOTIMPL through a failed action instead of successful placeholder readiness.
Public navigation/HTML/retry commands now dispatch through a bound frozen page table.
The real-control diagnostic in `Tools/uwp/CXStateTests/` passes 33 async and 42
property/transaction assertions with controlled readiness/V1 signals and actual
package-default validation. See the task checklist for behavioral gates.

`platform/PageCallbacksUWP` now supplies the actual retained frozen V1 attachment
with a weak per-control target and active-dispatch lifetime protection. Copied
typed arguments preserve error domains/codes, content policy and explicit HTTP
presence; all public state is installed before property/typed-event delivery.
Eligible structured HTTPS failures create hidden control/issuer/revision-bound
token objects. Old event token presence remains a snapshot after current consent
is invalidated. The version-1.0.0.5 diagnostic passes 37 retained-table callback/
token assertions in addition to both existing device suites.

`native/PageControllerUWP` consumes a negotiated/runtime-pinned V1 page table.
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
`platform/EventHandlersUWP` snapshots invocation and supports explicit final
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
comments. `Tools/uwp/audit-source-docs.py --check <authored-paths>` checks CX/native
declarations and spacing, including this directory's authored `WebKitWebView.h`.
