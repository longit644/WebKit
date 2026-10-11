# Embedding WebKitWebView in a W10M application

## Contract and current integration state

The public runtime class is `WebKit.WebKitWebView : UserControl`. Reference the
matching compiler-generated `WebKit.winmd` and package its `WebKit.dll`; the
default interface is revision-2 `IWebKitWebViewV2`. App projects compile C++/CX
with ARM32 MSVC `/ZW`; native WebCore keeps its separate compiler/ABI boundary.
Generate the app's XAML type provider/XBF from this metadata and register the
control/options classes to this DLL in the package manifest.

The public shape, Source/state/events, independent async observations, copied
options, frozen-table commands and explicit Close contracts are implemented and
device-tested. The normal production engine page forward remains Unsupported:
the current default initialization returns an action failure (E_NOTIMPL) rather
than claiming a ready browser page. The compiled contract host demonstrates
actual declarative identity, URI assignment, binding/layout and ordinary Close;
deterministic V1 tests demonstrate consumer lifecycle semantics. Complete
production page/renderer/platform integration has its own remaining gates.

## Declarative control and read-only bindings

```xml
<Page
    xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
    xmlns:web="using:WebKit">
    <StackPanel>
        <web:WebKitWebView x:Name="Browser" Source="https://example.org/" Height="400"/>
        <TextBlock Text="{Binding DocumentTitle, ElementName=Browser}"/>
        <Button Content="Back" IsEnabled="{Binding CanGoBack, ElementName=Browser}"/>
        <Button Content="Forward" IsEnabled="{Binding CanGoForward, ElementName=Browser}"/>
        <ProgressRing IsActive="{Binding IsLoading, ElementName=Browser}"/>
    </StackPanel>
</Page>
```

The outer object participates in layout; no manual returned-visual attachment is
needed. Source is a writable `Windows.Foundation.Uri` DP requiring absolute
HTTP(S). The other four properties are copied, read-only INPC state. IsLoading
means one outstanding main navigation, not initialization or arbitrary resource
activity. DocumentTitle is empty when absent.

## Initialization and copied options

Default construction supports XAML. Package `cacert.pem` and a `Fonts` directory
containing the required DejaVu regular/bold faces. Empty/null option paths choose
those defaults; relative paths resolve against the containing package, not CWD.
The options overload copies all values at construction; application bag mutation
does not alter the control. Validation/trust bytes are frozen before page use and
reused by explicit retry.

```cpp
// Programmatic construction copies configuration into a private immutable holder.
auto options = ref new WebKit::WebKitWebViewOptions();
options->MaximumConcurrentRequests = 8;
options->MaximumResourceBytes = 32ULL * 1024 * 1024;
options->MaximumBufferedBytes = 16ULL * 1024 * 1024;
options->RequestTimeoutMilliseconds = 30000;
auto browser = ref new WebKit::WebKitWebView(options);

// Two callers get distinct completion slots over one shared internal attempt.
auto first = browser->EnsureInitializedAsync();
auto second = browser->EnsureInitializedAsync();
first->Completed = ref new Windows::Foundation::AsyncActionCompletedHandler(
    [](Windows::Foundation::IAsyncAction^ action, Windows::Foundation::AsyncStatus status) {
        if (status == Windows::Foundation::AsyncStatus::Completed)
            action->GetResults();
        // Error: inspect IAsyncInfo.ErrorCode/GetResults and handle its original HRESULT.
    });
```

Success means controller/page readiness, **not first navigation completion**.
Live joiners settle before the latest queued Source is scheduled. Canceled,
closed or failed caller actions never become successful on a subsequent retry.
Each post-ready call still returns a fresh completed action.

One caller's `IAsyncInfo.Cancel` completes only that observation as Canceled /
E_ABORT. Its `IAsyncInfo.Close` releases that observation/handler; peers and the
shared attempt continue even if every caller ends its observation. Control Close
cancels all pending observers and shared work. Explicit EnsureInitializedAsync
after a failure starts a new generation with the same frozen configuration.

| Option | Default | Accepted range |
|---|---|---|
| MaximumConcurrentRequests | 8 | 1..16 |
| MaximumResourceBytes | 32 MiB | 64 KiB..64 MiB |
| MaximumBufferedBytes | 16 MiB | 1 MiB..32 MiB |
| RequestTimeoutMilliseconds | 30000 | 1000..120000 ms |

Queue=128 and redirects=10 are fixed. CA-file contents replace native OS trust;
they do not augment it. Font setup is process-global; conflicting canonical
path/content fails initialization before page creation.

## Source and navigation commands

Source shows requested URI while pending and effective URI after commit.
Precommit cancellation/failure restores the prior committed URI or null.
Unchanged nonnull assignment is not Reload. During initialization only the latest
Source remains; unused targets get no navigation ID or Starting event.

Source=null clears an unstarted queued target, or stops outstanding work while
preserving a committed document/history. It never creates a privileged blank
document. While initializing it keeps joiners/the shared attempt alive. After a
terminal document it stops subresources without another main terminal event.
Back/Forward use engine history; unavailable directions are no-ops with no new
ID. Reload with no document is also a no-op. Bound no-ops still invalidate pending
native consent as required. Stop never clears an already-committed document.

Navigation mutations inside notifications are validated, queued FIFO and
revalidated at execution. Source getters and its real DP remain the current
transaction's snapshot while a setter is deferred. Close marks closing
immediately, discards queued work, preserves already-owed terminal delivery and
cleans up/revokes after dispatch unwind.

Deferred execution errors are contained as internal diagnostics. A rejected
command does not erase an owed terminal or prevent the next valid FIFO command
from executing. A native admission rejection before Starting creates no main ID
or NavigationCompleted event. Direct synchronous rejection preserves the original
HRESULT and restores the prior requested Source; readiness-triggered rejection
restores committed Source while the successful initialization action stays complete.

Before readiness, newer Source/HTML requests replace older unstarted document
intent. Stop/null Source removes it without canceling shared initialization or its
joiners. Obsolete HTML continuations cannot run after a later request. Internal
readiness scheduling markers are not policy mutations and cannot veto Starting.

## Typed events and synchronous policy

Subscribe using SDK `TypedEventHandler<WebKitWebView, Args>` delegates and store
EventRegistrationTokens when ordinary unsubscription is needed. All public
notifications run on the owning UI dispatcher with copied payloads. SourceChanged
describes actual engine commit/same-document changes; requested/restored Source
changes use PropertyChanged only. HistoryChanged and DocumentTitleChanged follow
state installation. A started main ID gets one NavigationCompleted delivery.

```cpp
// Cancel is writable only within this synchronous proposal dispatch.
auto policyToken = browser->NavigationStarting +=
    ref new Windows::Foundation::TypedEventHandler<WebKit::WebKitWebView^, WebKit::WebKitNavigationStartingEventArgs^>(
        [](WebKit::WebKitWebView^, WebKit::WebKitNavigationStartingEventArgs^ args) {
            // App policy can inspect copied Uri/ContentKind and veto this proposal.
            if (args->ContentKind == WebKit::WebKitNavigationContentKind::Remote
                && args->Uri->Host == L"blocked.example")
                args->Cancel = true;
        });
```

Retaining Starting arguments does not extend the Cancel window; late writes
report RO_E_CLOSED. A Starting handler failure becomes Failed/Platform with its
originating HRESULT unless explicit Cancel, Close or replacement already requires
Cancelled. Other handler failures are diagnostic; the recorded terminal is not
changed, retried or erased. One throwing subscriber does not guarantee later
subscribers in that invocation ran.

Outcome, ErrorDomain and ErrorCode are separate. Transport codes are CURLcode;
Network codes are adapter-local; Platform preserves HRESULT bits; Engine codes
belong to the engine. Equal numbers in different domains have no shared meaning.
HasHttpStatus expresses presence; HttpStatus alone never determines transport
failure. ContentKind distinguishes Remote from HostHtml.

## Host markup and certificate warnings

Plain `NavigateToString(html)` proposes history-neutral opaque-origin about:blank
HostHtml, with one cancellable Starting/main ID/terminal lifecycle. It passes no
remote base URI. Null html is invalid; copied text uses explicit length, including
embedded NULs. A veto retains the current document. Transient commit leaves engine
history entries/cursor untouched; Back/Forward traverse existing neighbors.
Reload replays retained host text with a fresh ID and no history insertion.

Only the explicit `NavigateToString(html, warningToken)` overload associates a
warning with the exact current decision object. Repeated associated warning
commits preserve that eligibility. Generic HTML and navigation/history/reload/
stop/null-Source commands invalidate it on execution, even when no-op/veto.
Invalid arguments rejected before enqueue have no decision side effect.

On eligible HTTPS failure, NavigationCompleted may carry a nonconstructible
WebKitCertificateFailureToken. It exposes FailedNavigationId/FailedUri only.
CanProceedWithCertificateFailure is a **creation-time token-presence snapshot**;
it does not promise current validity. Keep the remote failure token separate from
later HostHtml IDs and ignore HostHtml completion as a new certificate failure.

```cpp
// Called by the app's explicit user-consent action using its retained remote token.
browser->NavigateToString(warningHtml, remoteFailureToken);
// After the user chooses proceed, consume the same still-current object once.
browser->ProceedWithCertificateFailure(remoteFailureToken);
```

Proceed validates hidden control/session/failure/object binding, consumes local
eligibility before enqueue and asks the provider to atomically consume/retry its
exact stored HTTPS target with a fresh Remote ID. Another control's object is
invalid even when both local navigation IDs are 1. Retry veto/failure never makes
the object reusable. Web content and reserved URL schemes cannot grant consent.

## Errors and lifetime

| Condition | Result |
|---|---|
| Invalid URI/scheme, null HTML/options, invalid/stale/cross-control token | E_INVALIDARG |
| Worker public mutation or event add/remove | RPC_E_WRONG_THREAD |
| Closing/closed control mutation or new subscription | RO_E_CLOSED |
| Late Starting.Cancel write | RO_E_CLOSED |
| Caller action cancellation | Canceled / E_ABORT |
| Closed caller observation read/GetResults | RO_E_CLOSED |
| Pending action GetResults | E_ILLEGAL_METHOD_CALL |
| Second action completion-handler assignment | E_ILLEGAL_DELEGATE_ASSIGNMENT |
| Missing CA/fonts | Originating Windows HRESULT, returned through failed initialization action |
| Malformed/empty CA contents | HRESULT_FROM_WIN32(ERROR_INVALID_DATA) |
| Process font configuration conflict | HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED) |
| Current unimplemented production page forward | Failed initialization action / E_NOTIMPL |

Call Close explicitly on the owning dispatcher. It is idempotent, cancels
pending operations, finishes owed events before revocation, and waits for native
Pending cleanup acknowledgement before releasing instance assets. Read-only final
snapshots remain owner-thread readable. Weak native/async subscriptions do not
root a released control; Close revokes public delegate roots and captured cycles.
Removing/reinserting an unclosed visual is not permanent Close. Presentation,
input, suspension and last-release/dispatcher-loss protocols retain their
remaining rendering/runtime-owner integration gates.

## Deferred capabilities and evidence

Host script execution, downloads/new windows/permissions deferrals, shared or
persistent profiles, independent per-control font registries, redirect-hop
notifications and engine DLL hot-unload are deferred. Capability reporting must
match delivered native table behavior; compile/metadata proof does not imply
arbitrary website compatibility or completed renderer/services integration.

Per-service availability (task 7.4 review; the engine mask stays zero until an
end-to-end production path works, so every item below that is not yet wired
fails fail-closed rather than claiming success):

- Navigation notifications/HTML/certificates: engine implements, control
  consumes only against controlled V1 tables; production bind needs the
  StructuredNotifications/HostHtml/CertificateDecisions bits and fails
  E_NOTIMPL without them.
- CPU presentation: device-proven on Lumia; GPU attach needs the
  GPUCompositor bit and fails E_NOTIMPL without it; resize correctness stays open.
- Input/focus forwarding: validated, needs the Input bit; real-engine
  keyboard/focus answers E_NOTIMPL explicitly.
- Clipboard copy/cut/paste: host route device-proven; engine self-serves
  internally; the ClipboardService bit needs an engine-forward decision plus
  runtime-owner wiring.
- Sessions/trust/fonts: engine isolation, immutable policy and canonical
  fingerprint implement; production consumption needs their bits.

`Tools/w10m/CXWebViewCompiledProbe` imports only generated public WinMD and stages
XBF/PRI. `Tools/w10m/CXStateTests` tests typed consumer semantics against controlled
V1 inputs; its exports are excluded from production. Exact package/build/report
evidence lives in the M10 `artifact/` directory and the task checklist.
