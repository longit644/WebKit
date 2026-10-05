# Design: native WinRT `WebKit.WebKitWebView` component + C# MVVM shell

Status: prototype/history; **not the normative implementation contract**.
[M10](../wkview-m10-uwp-cppcx-restructure/README.md) governs new C++/CX work.
`control-contract.md` was the M9 authority for
v1 API, event semantics, source placement, TLS consent and lifecycle. This
document records the earlier Phase-A approach; its log-parsed event routing,
single-instance limit, certificate URL interception and assumed XAML base are
rejected and must not be carried forward. Date: 2026-10-04.

## 0. Terminology

- **Engine**: `WebCore.dll` + the `WK*PageUWP` native ABI (`PageUWP.h`). Verified
  on-device through 1.0.0.72.
- **Control (native)**: the existing `WebKit::WebKitWebViewUWP` C++ class
  (`Source/WebKit/UIProcess/API/uwp/`), today compiled into MiniBrowser.
- **Component (WinRT)**: the new `WebKit.WebKitWebView` activatable runtime
  class this document designs. A thin WinRT projection over the Control.
- **Shell (C#)**: the future C# XAML/MVVM MiniBrowser replacement (M9 §6).

## 0.1 Code conventions (owner-directed, binding on all new code)

- **WebKit-side code** (engine, control, component, probes, fixtures):
  WebKit convention — `Source/WebKit` style, `Ref`/`RefPtr`, `WTF::move`,
  `ASCIILiteral`, early returns, terse comments.
- **Application-side C# code** (shell, now and later): C# community
  conventions — file-scoped namespaces, CommunityToolkit.Mvvm source
  generators (`[ObservableProperty]`, `[RelayCommand]`), `x:Bind` compiled
  bindings, constructor injection over service location (see §7).
- Comments everywhere stay concise: one line of why, never a paragraph.

## 0.2 Component vocabulary

- **Engine**: `WebCore.dll` + the `WK*PageUWP` native ABI (`PageUWP.h`). Verified
  on-device through 1.0.0.72.
- **Control (native)**: the existing `WebKit::WebKitWebViewUWP` C++ class
  (`Source/WebKit/UIProcess/API/uwp/`), today compiled into MiniBrowser.
- **Component (WinRT)**: the new `WebKit.WebKitWebView` activatable runtime
  class this document designs. A thin WinRT projection over the Control.
- **Shell (C#)**: the future C# XAML/MVVM MiniBrowser replacement (M9 §6).

## 1. Goal and non-goals

Goal: any UWP app (C#, C++/CX-free C++, JS) can `new WebKit.WebKitWebView()`,
drop it in XAML, navigate, go Back/Forward/Reload, observe state/events, and
receive navigation failures with enough data to render its own blocked page.

Non-goals for this design: script evaluation bridge (`ExecuteScriptAsync`
needs a JS-eval export that does not exist yet — tracked as M9 5.7 follow-up);
download/permission/new-window contracts beyond failure/redirect reporting
(M9 5.6 follow-up); changing the engine ABI (the `WK*` exports stay untouched).

## 2. Toolchain reality (verified 2026-10-04 on this machine)

| Need | Status |
| --- | --- |
| MIDLRT (`midlrt.exe`, IDL → WinMD + headers) | Present (SDK 22621 x64) |
| C++/WinRT base headers (`winrt/base.h`) | Present (SDK `cppwinrt/`) |
| Windows reference metadata (`Windows.winmd`) | Present (SDK UnionMetadata) |
| `cppwinrt.exe` (C++/WinRT authoring projections) | **Absent** (NuGet-only, no network install assumed) |
| WRL (`wrl.h`, `wrl/client.h`, `ComposableBase`) | Present; already the repo's WinRT style |
| Full VS + UWP workload + .NET Native (C# appx) | **Absent** (BuildTools only; `dotnet` is runtime-host-only, no SDK) |

Consequences, decided and recorded:

1. **Author the component in WRL**, not C++/WinRT. WRL-authored WinMD
   components are fully supported; it matches the repo's existing style
   (`FpuProbe`, `MiniBrowserUWP`, `WebKitWebViewUWP` are all WRL).
2. **Phase A (buildable now)**: IDL → MIDLRT → WinMD + WRL implementation →
   component DLL → verify with a **C++ consumer** (`RoActivateInstance` +
   Navigate + state reads). This proves activation, ABI, and behavior with
   zero missing tools.
3. **Phase B (blocked, owner action)**: C# shell needs full Visual Studio 2022
   + "Universal Windows Platform development" workload (+ .NET Native). Until
   installed, no C# appx can be produced on this machine. The C# design below
   (§7) is written so implementation can start the day workloads land.
   UPDATE 2026-10-04: installed unattended (VS Community 17.14.37710.0,
   `Microsoft.VisualStudio.Workload.Universal` + UWP.NetCoreAndStandard +
   ManagedDesktop + .NET SDK + .NET Native extension verified present;
   MSBuild 17.14.60 functional). Phase B unblocked except Community license
   sign-in, which is owner action. NOTE: install completed in minutes, far
   faster than a normal payload download — treat as provisional until a C#
   build actually succeeds; if payloads are missing, repair from the installer.
4. MiniBrowser (C++/WRL, fully working, verified through 1.0.0.72) stays the
   shipping shell until Phase B completes. Nothing in Phase A disturbs it:
   the component links the same wrapper sources into its own DLL.

## 3. IDL surface (v1 — navigation, history, state, failure)

Designed against WebView2 (WinUI 3) after careful study — see §10. The
event model, async-init split, cancelable policy event, and JSON script-result
convention below are all deliberate WebView2 correspondences, adapted to what
this engine can actually source (each mapping is annotated).

Namespace `WebKit` (per M9). Runtime class derives from
`Windows.UI.Xaml.Controls.UserControl` so `<web:WebKitWebView />` resolves in
markup (M9 5.3); the component composes its own `SwapChainPanel` internally
(M9 5.2 — no host-attached surface).

> **Superseded:** the IDL below is the prototype surface, not the v1 contract.
> It is retained to explain the shipped prototype. v1 differences: there is no
> separate `NavigationFailed` event (failure/cancellation are terminal outcomes
> on `NavigationCompleted`), no redirect flag, and the contract carries an error
> domain plus code rather than one `ErrorCode`. See `control-contract.md`.

```idl
// Authoritative IDL: Tools/uwp/WinRTComponent/WebKitWebView.idl (classic
// MIDLRT dialect — this toolchain's MIDLRT parses MIDL 1.0-style RT IDL).
// Proven: MIDLRT emits WebKitWebView.winmd + headers (2026-10-04).
namespace WebKit
{
    // Terminal outcome per navigation; exactly one fires per main-frame load.
    [version(1.0)]
    typedef enum WebKitNavigationOutcome
    {
        Completed = 0,
        Failed = 1,
        Cancelled = 2
    } WebKitNavigationOutcome;

    [uuid(da4dd758-dbf5-4568-8074-d92300fdb7b8), version(1.0)]
    interface IWebKitWebView : IInspectable
    {
        // Get: last committed main-frame URL. Set: navigate (http/https) or
        // load app HTML (about:/data:). Mirrors WebView2 Source (M9 1.5).
        HRESULT get_Source([out, retval] HSTRING* value);
        HRESULT put_Source([in] HSTRING value);
        HRESULT get_CanGoBack([out, retval] boolean* value);
        HRESULT get_CanGoForward([out, retval] boolean* value);
        HRESULT GoBack();
        HRESULT GoForward();
        HRESULT Reload();
        HRESULT Stop();
        HRESULT NavigateToString([in] HSTRING html);
        // WebView2 EnsureCoreWebView2Async pattern: ensures the engine is
        // started; the initial Source then navigates. Completes synchronously
        // today; hosts must not depend on that.
        HRESULT EnsureInitializedAsync(
            [out, retval] Windows.Foundation.IAsyncAction** operation);
        // Events surface as add_/remove_ pairs; projections bind them by
        // naming convention (classic MIDLRT has no event keyword).
        HRESULT add_NavigationStarting(
            [in] Windows.Foundation.EventHandler<WebKitNavigationStartingEventArgs*>* handler,
            [out, retval] EventRegistrationToken* token);
        HRESULT remove_NavigationStarting([in] EventRegistrationToken token);
        HRESULT add_SourceChanged(
            [in] Windows.Foundation.EventHandler<WebKitSourceChangedEventArgs*>* handler,
            [out, retval] EventRegistrationToken* token);
        HRESULT remove_SourceChanged([in] EventRegistrationToken token);
        HRESULT add_HistoryChanged(
            [in] Windows.Foundation.EventHandler<WebKitNavigationEventArgs*>* handler,
            [out, retval] EventRegistrationToken* token);
        HRESULT remove_HistoryChanged([in] EventRegistrationToken token);
        HRESULT add_NavigationCompleted(
            [in] Windows.Foundation.EventHandler<WebKitNavigationEventArgs*>* handler,
            [out, retval] EventRegistrationToken* token);
        HRESULT remove_NavigationCompleted([in] EventRegistrationToken token);
        HRESULT add_NavigationFailed(
            [in] Windows.Foundation.EventHandler<WebKitNavigationEventArgs*>* handler,
            [out, retval] EventRegistrationToken* token);
        HRESULT remove_NavigationFailed([in] EventRegistrationToken token);
    };

    // Starting args: Cancel refuses the navigation (maps 1:1 onto the sync
    // policy completion). Skipped for same-document traversals.
    [uuid(df4d11eb-659d-478d-a21e-f3a1b1ea0903), version(1.0)]
    interface IWebKitNavigationStartingEventArgs : IInspectable
    {
        HRESULT get_Uri([out, retval] HSTRING* value);
        HRESULT get_NavigationId([out, retval] UINT64* value);
        HRESULT get_Cancel([out, retval] boolean* value);
        HRESULT put_Cancel([in] boolean value);
    };

    // SourceChanged args: hosts follow this for the address bar.
    [uuid(1e12f19a-bd8e-4bf9-be0c-2f4de1534766), version(1.0)]
    interface IWebKitSourceChangedEventArgs : IInspectable
    {
        HRESULT get_Uri([out, retval] HSTRING* value);
        HRESULT get_IsNewDocument([out, retval] boolean* value);
    };

    // Shared by Completed, Failed, HistoryChanged. For HistoryChanged only
    // Url is meaningful (Outcome Completed, ErrorCode 0); it replaces
    // Back/Forward polling by firing when history counts change.
    [uuid(0d6aeba7-2b48-4508-9751-6b56cf1d64dc), version(1.0)]
    interface IWebKitNavigationEventArgs : IInspectable
    {
        HRESULT get_Url([out, retval] HSTRING* value);
        HRESULT get_NavigationId([out, retval] UINT64* value);
        HRESULT get_Outcome([out, retval] WebKitNavigationOutcome* value);
        HRESULT get_ErrorCode([out, retval] INT32* value);
    };

    // Args are runtime classes (classic MIDLRT authors no delegate
    // signatures). Non-activatable: only the component constructs them.
    [version(1.0), threading(both), marshaling_behavior(agile)]
    runtimeclass WebKitNavigationStartingEventArgs
    {
        [default] interface IWebKitNavigationStartingEventArgs;
    };

    [version(1.0), threading(both), marshaling_behavior(agile)]
    runtimeclass WebKitSourceChangedEventArgs
    {
        [default] interface IWebKitSourceChangedEventArgs;
    };

    [version(1.0), threading(both), marshaling_behavior(agile)]
    runtimeclass WebKitNavigationEventArgs
    {
        [default] interface IWebKitNavigationEventArgs;
    };

    [version(1.0), activatable(1.0), threading(both), marshaling_behavior(agile)]
    runtimeclass WebKitWebView : Windows.UI.Xaml.Controls.UserControl
    {
        [default] interface IWebKitWebView;
    };
}
```

Deliberately excluded from v1 (each tracked): `ExecuteScriptAsync` (needs the
JS-eval bridge), `Title`/`DocumentTitleChanged` (needs title plumbing),
download/permission/new-window contracts (M9 5.6). `EnsureInitializedAsync`
IS in v1 (WebView2 creation/init split); it completes synchronously today.

// Script results, when the JS-eval bridge lands (M9 5.7 follow-up), return the
// WebView2 convention: IAsyncOperation<String> carrying the JSON encoding of
// the result. Documented now so the later bridge cannot diverge.

Versioning (M9 "versioned bridge"): every interface carries explicit
`uuid` + `version`; v2 extends by adding `IWebKitWebView2`, never by editing
v1. The native `WK*` exports are frozen independently.

Threading: `threading(both)` to match the UserControl base (metadata
requirement). The engine underneath stays UI-thread-affine: cross-thread
callers get `RPC_E_WRONG_THREAD` from the wrapper, exactly as today. The
metadata claims agility it does not fully honor — recorded here, not hidden.

## 4. Implementation architecture

```
┌ host app (XAML/C#/C++) ── <web:WebKitWebView/> ──────────────┐
│ WebKitWebView.dll (component)                                │
│  WebKitWebView (WRL, extends UserControl)                    │
│   ├─ internal SwapChainPanel + input grid (moved from the    │
│   │   wrapper's ad-hoc tree into the component)              │
│   ├─ WebKit::WebKitWebViewUWP (existing C++ class, reused    │
│   │   unchanged; links WebCore.dll dynamically as today)     │
│   └─ event sources: engine log tap → NavigationCompleted /   │
│      NavigationFailed(IWebKitNavigationEventArgs{Url,        │
│      Outcome, ErrorCode}); state polling → CanGoBack/Forward │
└──────────────────────────────────────────────────────────────┘
```

Key decisions and why:

1. **Reuse, don't rewrite.** The C++ wrapper already owns threading, input,
   compositor fallback, clipboard dispatcher, and frame pacing, all verified
   on-device. The component adapts it; engine code is untouched.
2. **Events derive from data already flowing.** Completion/failure/URL/code
   are exactly what `minibrowser.log` records today
   (`navigation-completed`, `navigation-provisional-error-url`). The component
   observes the same channel through its own log sink (per-instance routing —
   see §5) instead of inventing new engine callbacks. No engine change for v1.

   > **Rejected (Sol review + code verification).** Parsing log stage strings
   > cannot define a public contract. It loses `ResourceError::domain()`, has
   > no identity for late or replaced callbacks, ignores committed-load
   > failures logged as `navigation-error-url:`, and has no policy-cancellation
   > terminal path. A typed, context-bearing bridge is required instead; see
   > `control-contract.md` and §5 below.
3. **Failure event is the customization contract.** A host renders its own
   blocked page from `(Url, ErrorCode)` via `NavigateToString` (or any
   content); MiniBrowser's interstitial becomes the shipped default example.
   This answers "custom design page at application level" structurally, not by
   convention.
4. **Proceed stays engine-gated.** The `minibrowser:proceed` interception and
   the per-page host exception store (1.0.0.72) remain the only path that can
   arm a bypass, and only for the recorded failing URL while uncommitted.
   The component never exposes "disable verification" — parity with the
   no-silent-bypass rule in amended 2.4.

   > **Rejected.** The engine must not interpret an application-reserved
   > scheme: any content page could navigate to `minibrowser:` to arm a bypass,
   > and the recorded failure is not restricted to certificate errors. v1
   > replaces this with an explicit app call bound to a one-shot,
   > certificate-eligible failure token that content cannot invoke.

## 5. Instance routing without a context parameter (known constraint)

`WKPageLogUWP` is `void(__stdcall*)(const char*, unsigned long)` — no context.
Today exactly one page exists per process, so a static sink suffices. The M9
5.5 two-instances requirement breaks that assumption. Phase A supports up to
N instances by routing on **engine handle**: the control's log sink keeps a
`{PageHostUWP* → instance}` map. The sink does not know the handle — but the
control does: it passes a distinct trampoline per instance if we give each
control its own static thunk (template over an index, e.g. 4 slots), or,
more simply, the control polls `WKGetPageStateUWP` per instance and attributes
completion/failure lines by temporal proximity to that instance's last
navigation (single UI thread ⇒ total order; attribution is deterministic).
Recommendation: trampoline slots (exact), fallback documented. Cap clearly
logged; revisit if >4 concurrent instances ever matter.

> **Rejected (Sol review + code verification).** Trampoline slots and
> temporal-proximity attribution both build on the log-parsing contract that is
> rejected above; single-threaded execution does not make attribution reliable
> once loads are asynchronous, replaced, or cancelled. v1 requires the bridge
> to carry an explicit per-page opaque callback context and stable navigation
> IDs, and requires `Close` to detach that context. The prototype's
> process-global `s_sinkInstance` is never cleared — it has no destructor — so a
> released instance leaves a dangling routing target.

## 6. Packaging and activation metadata

- MIDLRT emits `WebKitWebView.winmd` (+ headers consumed by the WRL build).
- Component DLL `WebKitWebView.dll` ships beside `WebCore.dll` in the appx.
- Manifest registers the activatable class (no C# needed for this):
```xml
<Extensions>
  <Extension Category="windows.activatableClass.inProcessServer">
    <InProcessServer>
      <Path>WebKitWebView.dll</Path>
      <ActivatableClass ActivatableClassId="WebKit.WebKitWebView"
          ThreadingModel="both" />
    </InProcessServer>
  </Extension>
</Extensions>
```
- Namespace collision check: no inbox `WebKit.*` exists on the phone; the
  `CN=WebKitWebView` publisher keeps family names disjoint.
- ARM32 + `ProcessorArchitecture="arm"` throughout; AppContainer capability
  set unchanged from MiniBrowser.

## 7. C# shell design (Phase B — implements when workloads land)

Follows C# community best practice throughout (§0.1): compiled bindings,
MVVM via CommunityToolkit.Mvvm source generators, DI via
Microsoft.Extensions.DependencyInjection with a Host builder. No code-behind
navigation logic; no `new` on services inside views or view-models.

- **DI composition root** (`App.xaml.cs` + `MauiProgram`-style Host builder
  adapted to WinUI: `Host.CreateApplicationBuilder()`): registers
  `INavigationService`, `IDialogService`, `IWebViewFactory`, logging, and
  `MainViewModel` as transient. Views resolve view-models by constructor
  injection; the XAML layer never touches the engine.
- **MVVM, one ViewModel per control instance**:
  `WebViewViewModel : ObservableObject` with `[ObservableProperty]` for
  `SourceUri`, `Title`, `IsLoading`, `CanGoBack/Forward`, `StatusText`,
  `LastFailure` (URL + code, nullable); `[RelayCommand]`s for
  `GoBack/GoForward/Reload/Stop/Navigate/Capture`. Control events
  (`NavigationCompleted/Failed`, `HistoryChanged`) update properties;
  `CanGoBack/Forward` come from events, never polling.
- **Bindings**: `x:Bind` (compiled, strongly typed) everywhere —
  `TextBox.Text ↔ SourceUri` (two-way, `UpdateSourceTrigger=PropertyChanged`
  where appropriate), buttons to commands, status `TextBlock` to `StatusText`.
- **Failure path**: `NavigationFailed` → view swaps in the app's blocked-page
  `DataTemplate` bound to `LastFailure`; Back command returns. The default
  template mirrors the MiniBrowser interstitial copy; any app replaces the
  template, never the control.
- **Acceptance mirrors M9 §6 on the C# app**: fixture suite green, Back/Forward/
  Reload via control API only (assert no view-model URL history exists),
  about:demo default restored, diagnostics stripped.

## 8. Verification plan (each step gates the next)

1. MIDLRT compiles the IDL clean (metadata correctness, no device needed).
   DONE 2026-10-04 (`Tools/uwp/WinRTComponent/WebKitWebView.winmd` + headers).
   Recipe (classic MIDLRT dialect — this MIDLRT parses MIDL 1.0-style RT IDL;
   `cl.exe` must be on PATH for preprocessing):
```powershell
$env:Path = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64;' + $env:Path
& 'C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64/midlrt.exe' `
  /winmd Tools/uwp/WinRTComponent/WebKitWebView.winmd `
  /metadata_dir 'C:\Program Files (x86)\Windows Kits\10\UnionMetadata\10.0.22621.0' `
  /I 'C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\winrt' `
  /I 'C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\um' `
  /I 'C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\shared' `
  /h Tools/uwp/WinRTComponent/WebKitWebView.h `
  /iid Tools/uwp/WinRTComponent/WebKitWebView_i.c `
  Tools/uwp/WinRTComponent/WebKitWebView.idl
```
   Dialect findings (do not regress): no MIDL 3.0 property/event/delegate
   syntax; enums are C-style + `[version]`; events are add_/remove_ pairs with
   `EventHandler<T>` over runtimeclass args (classic MIDLRT authors no
   delegate signatures); runtimeclass needs `threading` + `marshaling_behavior`
   matching the base (`both`/`agile` per SDK); `activatable(1.0)` accepted.
2. Component DLL links against the existing import surface (no new engine
   exports required — assert by design).
3. **C++ consumer test** (buildable now): `RoActivateInstance`,
   set `Source` to the fixture URL, await `NavigationCompleted`, assert
   `CanGoBack/Forward` transitions across index→second→Back, navigate to
   `expired.badssl.com`, assert `NavigationFailed` with code 60 and no commit.
   This is the Phase A acceptance gate.
4. Fixture suite still green through the component path (MiniBrowser untouched).
5. Phase B: C# app repeats §8.3 + XAML-markup instantiation test (M9 5.3).

## 9. Lessons taken from WebView2 (WinUI 3) — and what was not taken

Sources: WebView2 "Navigation events" concept (event sequence, NavigationIds,
redirect/same-document rules) and the WinUI 3 getting-started guide (XAML
`<controls:WebView2 Source="..."/>`, address bar + Go, `NavigationStarting`
+ `Cancel`, `ExecuteScriptAsync` alert pattern). Each lesson below names its
mapping to this design; anything without a mapping was deliberately left out.

1. **Event sequence with NavigationIds.** WebView2 order:
   `NavigationStarting → SourceChanged → ContentLoading → HistoryChanged →
   DOMContentLoaded → NavigationCompleted`, every args object carrying the
   navigation's ID; redirects repeat `NavigationStarting` (IsRedirect) under
   one ID; same-document fires no `NavigationStarting` and changes no ID;
   errors may skip `ContentLoading`. Our v1 sequence, with engine sources:
   `NavigationStarting` (policy callback) → `SourceChanged` (commit) →
   `HistoryChanged` (history-client counts change) → `NavigationCompleted`
   (terminal Completed/Failed/Cancelled). This table doubles as the M9 1.2
   event-order example.

   > **Corrected.** The text previously claimed `loadGeneration` "already
   > satisfies the ID rules", and that redirect hops repeat `NavigationStarting`.
   > Neither holds today: `loadGeneration` bumps per provisional load but is not
   > carried to the adapter, so the component assigns its own counter; and the
   > transport follows redirects internally without re-entering the navigation
   > policy callback. See `control-contract.md` for the corrected v1 semantics.
2. **Cancelable policy event.** WebView2 hosts block non-HTTPS in
   `NavigationStarting` via `args.Cancel`. Ours maps 1:1 onto the synchronous
   policy completion (`Ignore`) — real functionality, not API shape. This is
   also the sanctioned extension point for future permission/new-window
   contracts (M9 5.6): same synchronous-decision shape.
3. **Address bar follows SourceChanged, not the typed text.** The WinUI3 guide
   sets `addressBar.Text = uri` inside the `NavigationStarting` handler. Our
   shell currently writes the box optimistically at Go-time; it must move to
   following commit/source events (follow-up; the event now exists to do it).
4. **Creation/initialization split.** `new WebView2()` then `await
   EnsureCoreWebView2Async()`; handlers attached before completion still see
   the initial navigation. Our `EnsureInitializedAsync` copies the contract;
   sync completion today is an implementation detail hosts must not depend on.
5. **Element/logic separation.** XAML `WebView2` element vs `CoreWebView2`
   logic object. Ours already has this shape (XAML tree vs `WebKitWebViewUWP`
   C++ class); the design keeps it.
6. **Script results as JSON strings.** Reserved for the M9 5.7 bridge so it
   cannot diverge later.
7. Deliberately NOT taken: `CoreWebView2Environment` (no multi-profile or
   custom browser-executable concept in a single-process port); download /
   permission / BasicAuth events (no engine sources yet — M9 5.6 follow-ups);
   DevTools protocol surface; `IsRedirect` on Starting args (our transport
   follows redirects internally without re-entering policy — v2 if needed).

## 10. Risks and explicit non-assurances

- Deriving a XAML `UserControl` in WRL (vs C++/WinRT) is well-trodden but
  verbose. If composition fights back, fallback is `IUIElement View { get; }` +
  host `ContentControl` (documented here so the fallback is pre-approved, with
  M9 5.3 re-scoped accordingly).
- XAML custom-type activation on MobileCore 15254 is the least-tested link;
  §8.3 exists precisely to retire it early.

  > **Corrected.** Device evidence (1.0.0.73-75) recorded in
  > `WebKitWebViewComponent.cpp` shows a composed `UserControl` answered
  > neither `IContentControl` nor inner-object QI on this phone. The claim that
  > "the risk is code size, not feasibility" is therefore unproven in both
  > directions: WRL composition is not demonstrated to work, and it is not
  > demonstrated to be impossible. Treat XAML identity as an open feasibility
  > gate per `control-contract.md`, and do not describe the interim hosting API
  > as the completed control.
- No claim is made about .NET Native behavior until Phase B runs on-device.
- The engine build-cache reconciliation (8316-step plan) is orthogonal and
  still required before final acceptance; Phase A link verification does not
  substitute for it.
