# M10 Windows 10 Mobile C++/CX design

Author: Sol. Status: owner-selected direction; reviewed contract corrections,
implementation and runtime/device feasibility pending.

This is the developer-facing architecture for
[`wkview-m10-w10m-cppcx-restructure`](../../openspec/changes/wkview-m10-w10m-cppcx-restructure/README.md).
Its OpenSpec requirements and tasks define acceptance. M9 is superseded, not
completed; its device records describe historical packages only.

Supporting M10 contracts live in its `artifact/` directory; references below to
public-api.md, native-abi.md, migration-map.md, build-and-packaging.md,
acceptance-matrix.md and implementation-handoff.md mean those files there. See
[public API](../../openspec/changes/wkview-m10-w10m-cppcx-restructure/artifact/public-api.md),
[ABI](../../openspec/changes/wkview-m10-w10m-cppcx-restructure/artifact/native-abi.md)
and [handoff](../../openspec/changes/wkview-m10-w10m-cppcx-restructure/artifact/implementation-handoff.md).

## 1. Scope and authoring boundary

All authored Windows 10 Mobile application, public component, XAML, and WinRT OS-service
projections use C++/CX. The public runtime type remains `WebKit.WebKitWebView`.
Its authored files stay directly in `Source/WebKit/UIProcess/API/w10m/`.
Language choice does not require moving the control to Tools or a WinRT folder.

Native engine algorithms, transport, rendering math, DirectX COM interfaces,
and third-party code retain ordinary C/C++. WebCore, WTF, JavaScriptCore, and
PAL must not depend on C++/CX headers or compile wholesale with `/ZW`. Engine
W10M adapters forward OS requests through the neutral bridge to C++/CX services
when projection is needed. Ordinary native helpers inside the component do not
need `ref class` wrappers just to satisfy a naming policy.

### Current evidence versus target

- The installed MSVC ARM compiler accepts a C++/CX UserControl probe with `/ZW`.
- The installed SDK 10.0.26100.0 C++/WinRT base header rejects `_M_ARM`.
  This is a version-specific observation, not a claim about all historical
  C++/WinRT releases. C++/CX is the owner's selected architecture regardless.
- Compiling an object does not prove DLL linking, compatible exception/runtime
  support, WinMD correctness, XAML metadata generation, or phone activation.
- The installed SDK lacks ARM32 link libraries. Discover a complete compatible
  Windows 10 SDK, initially 10.0.19041.0, and prove its runtime/XAML tooling.
  Do not change toolsets speculatively or substitute ARM64 libraries.
- Existing native shims are not presumed compatible with C++/CX exceptions,
  reference counting, activation, or `vccorlib` initialization.

## 2. Layers and ownership

```text
MiniBrowser / device runner / diagnostic apps (C++/CX projections)
                          |
                WebKit.WebKitWebView (C++/CX)
                          |
        PageControllerW10M + presentation/input/platform adapters
                          |
                  versioned C ABI
                          |
        WebCore W10M page, loader, session and compositor adapters
                          |
             existing WebCore / WTF / JavaScriptCore
```

| Layer | Owns | Must not own |
|---|---|---|
| App | Chrome, bindings, consent UX, app paths/options | HTTP downloader, engine history, private DOM assertions |
| Control | XAML identity, properties/events, controller, visual child | Address bar, fixture URLs, warning copy |
| Native controller | Page handle, operation state, copied callback values | WebCore types, global raw app pointer |
| Engine | Page/session, navigation/history, resource/security semantics | XAML projections, fixture sequence, app UI |
| Presenter/input | Surface, DPI, subscriptions, gestures, frame metrics | Network completion oracle or shadow history |
| Tests | Fixtures, run IDs, actions, oracles, aggregate result | Production-only test hooks |
| Tools | Projects, toolchain selection, staging/audit/deployment | Authored framework runtime classes |

One UI/engine-owning thread serves the controls. Transport workers marshal
completion back to that thread. Process-global initialization and a service
pump may be shared, but page state and callback routing are never global.

## 3. Target source tree

```text
Source/WebKit/UIProcess/API/w10m/
  WebKitWebView.h / .cpp
  WebKitNavigationTypes.h
  WebKitNavigationEventArgs.h / .cpp
  WebKitWebViewOptions.h / .cpp
  WebKitCertificateFailureToken.h / .cpp
  README.md
  native/
    EngineModuleW10M.h / .cpp
    PageControllerW10M.h / .cpp
    NavigationStateW10M.h / .cpp
  rendering/
    SwapChainPresenterW10M.h / .cpp
    FrameSchedulerW10M.h / .cpp
    FrameMetricsW10M.h / .cpp
  input/
    InputAdapterW10M.h / .cpp
    ScrollControllerW10M.h / .cpp
  platform/
    DispatcherW10M.h / .cpp
    ClipboardServiceW10M.h / .cpp
    RuntimeOwnerW10M.h / .cpp

Source/WebKit/Shared/w10m/
  WKW10MEngine.h
  WKW10MPage.h
  WKW10MNavigation.h
  WKW10MInput.h
  WKW10MRendering.h

Source/WebCore/platform/w10m/
  EngineW10M.h / .cpp
  PageHostW10M.h / .cpp
  FrameLoaderClientW10M.h / .cpp
  ChromeClientW10M.h / .cpp
  BackForwardClientW10M.h / .cpp
  NavigationControllerW10M.h / .cpp
  StorageSessionProviderW10M.h / .cpp
  LoaderStrategyW10M.h / .cpp
  ResourceLoadW10M.h / .cpp
  CertificateExceptionStoreW10M.h / .cpp
  ClipboardW10M.h / .cpp
  existing Skia/compositor/drag-image adapters
  PageW10M.cpp (temporary legacy export forwarders)

Tools/MiniBrowser/w10m/csharp/
  App.xaml / .xaml.cs
  MainPage.xaml / .xaml.cs
  BrowserViewModel.cs
  BrowserPolicy.cs
  Assets/blocked.html and demo.html
  Package.appxmanifest
  README.md

Tools/w10m/
  build/             (native/component orchestration)
  packaging/         (staging, dependency audits, deployment)
  diagnostics/       (RuntimeSmoke/, FpuProbe/, TlsSmoke/)
  toolchain/         (native C/C++/assembly support)
  CXComponent/       (component project and build support only)
  NavigationTests/
    host/ device/ fixtures/ protocol/ evidence/
```

Place the component `.vcxproj` and shared property sheets in
`Tools/w10m/CXComponent/`; link authored source from the API directory. Place
app projects alongside their app source. Explicit intermediate/output paths
keep generated metadata, XAML glue and binaries in build directories.

Keep shared WebCore network/curl and upstream platform files in their upstream
homes. Inventory W10M-conditioned changes in WTF/PAL/WebCore outside the listed
directories; update relevant adapters without moving whole upstream modules.
Keep CA/font package assets with their documented producer/staging ownership.

`FirstPageW10M.cpp` is split by dependency: reusable startup belongs to
`EngineW10M`; smoke page content and assertions belong to RuntimeSmoke. A native
smoke payload remains native even when its application host uses C++/CX.

## 4. Naming and code conventions

| Symbol | Convention |
|---|---|
| Public namespace/control | `WebKit`, `WebKitWebView` |
| Public properties/methods/events/enums | PascalCase |
| Native host helpers | PascalCase with `W10M` suffix, e.g. `PageControllerW10M` |
| WebCore platform adapters | PascalCase with existing `W10M` suffix |
| Native methods/locals | lowerCamelCase |
| Native private fields | `m_` + lowerCamelCase |
| Constants | lowerCamelCase with units where relevant |
| C ABI | `WKW10M` prefix, explicit fixed-width types; retain `API` uppercase |
| Files | Primary type name; `.h` and `.cpp` pair |
| Scripts | lowercase kebab-case, language-specific documented entry points |
| Generated files | Generator-controlled names, no hand edits |

Examples: `frameProfileSampleCount`, `maximumResourceBytes`,
`initialViewportWidthCssPixels`, `navigationTimeoutMilliseconds`.
Document ABI values and non-obvious thresholds; do not introduce constants for
ordinary comparison zero, successful HRESULT, or string-prefix index zero.
Use SDK/WebKit/curl semantic constants instead of duplicate numeric literals.

Use `enum class` for internal states and C++/CX `public enum class` for public
metadata. ABI headers use explicitly sized integral fields and named value
constants, not compiler-dependent enum layout or `bool`. Types/members carry
why-focused documentation; public enums document each numeric value. Preserve
WebKit formatting. Update the source audit/format rules for CX syntax before
running them; never format generated output.

The initial rename map and full-scope accounting are in M10
`migration-map.md`. Its rows must cover every authored W10M class, enum,
constant family, export, script, build target, and source file before moving
that area. An upstream override retains its mandated method name.

## 5. Public contract

The authoritative field-level contract is M10 `public-api.md`. The core is:

```text
WebKitWebView : Windows.UI.Xaml.Controls.UserControl
  Source : Uri (dependency property)
  CanGoBack, CanGoForward, IsLoading, DocumentTitle (read-only observable)
  EnsureInitializedAsync() : IAsyncAction
  GoBack(), GoForward(), Reload(), Stop()
  NavigateToString(String^)
  NavigateToString(String^, WebKitCertificateFailureToken^ warningToken)
  ProceedWithCertificateFailure(WebKitCertificateFailureToken^ token)
  Close()
```

The default constructor supports XAML. A `WebKitWebViewOptions` constructor
supports pre-initialization host paths without teaching the framework package
layout. Default asset paths are resolved relative to the containing package;
an explicit options instance can supply CA/font directories. Options are copied
and immutable once initialization starts. Each session captures CA-file-only trust
(bundle replaces native store); requests/cache reuse cannot share another session's
trust or exceptions. Fonts are process-global: canonical path/content fingerprint
must match the first successful setup or initialization fails. No silently ignored
per-control fonts or mutable CurlContext SSL trust settings.

Exact resource ranges, queue/buffer/redirect caps and deadline clocks are in
`public-api.md`. Defaults include 8 transfers, 32 MiB decoded bytes per resource,
16 MiB transient backlog and 30 seconds including queue/redirect time; suspend
pauses the deadline, visual unload does not.

`Source` contains the requested target during pending navigation, and the
effective engine URI after commit. Policy refusal or pre-commit failure restores
the previous committed source (null on a fresh control). `SourceChanged` is an
engine change notification, not a promise that every requested value committed.
Guard internal property updates to avoid recursive navigation. Assigning an
unchanged DP value need not start another load; use Reload to reload it.

Null Source uses a state table: clears unstarted queued intent, cancels proposals/
provisional loads, stops committed remaining work and retains the committed URI/
document/history. Invalid assignment restores the DP before E_INVALIDARG. During
reentrant dispatch setters retain the transaction snapshot and defer their intent.
See public-api.md for every state and ordered trace.

Read-only properties notify bindings via `INotifyPropertyChanged`; do not expose
writable dependency properties for engine-only state. Construct and copy event
args per notification. Exceptions map to documented WinRT HRESULTs and never
cross the neutral bridge. The new metadata revision intentionally replaces the
prototype's string Source and separate NavigationFailed semantics.

## 6. Initialization, lifetime, and event delivery

```text
Created -> Initializing -> Ready -> Closing -> Closed
                  |
                  +-> Faulted -> Initializing (explicit retry)
```

Concurrent EnsureInitializedAsync calls return separate IAsyncAction objects backed
by one internal attempt. Caller-local Cancel/Close does not cancel another joiner
or Source-driven initialization. Success means page/controller readiness; resolve
joiners before scheduling queued navigation. Failure errors live joiners and permits
explicit retry using frozen options; control Close cancels attempt and pending actions.

XAML Unloaded removes visual frame/input subscriptions and pauses presentation;
Loaded may resume an unclosed instance. It does not destroy the page merely
because the control was temporarily reparented. Networking remains serviced.
Suspend/resume has the same separation and invalidates lost GPU resources.

Notifications are owner-thread transactions. Mark terminal obligation before
publishing state/PropertyChanged, raise its terminal event, then drain deferred
commands. Starting synchronously freezes Cancel and honors queued replacement/Stop/
null/Close before accepting the load. Commands are FIFO and revalidated at execution;
none start a replacement between old loading-state change and old terminal event.
Catch handler exceptions at ABI; late Cancel writes fail explicitly.

Close marks closing immediately and clears queued commands, but completes already-
owed terminal transactions and cancels active IDs once before detach/revocation.
Retain dispatch guards; destruction waits until unwind. Generation invalidation
drops stale engine work, never an already-owed public terminal event. Final-release/
shutdown cleanup suppresses public handlers because no live consumer is promised.

Use weak references for XAML/async callbacks. RuntimeOwnerW10M retains native cleanup
tickets independently of controls before dispatcher enqueue. Failed enqueue enters
owner-thread quiescence: reject work, detach pages, cancel/ack platform/worker work,
revoke process hooks, drain tickets, then release services. Never wait synchronously
for UI callbacks on that same thread. App hosts run shutdown before dispatcher
destruction. If OS thread loss makes owner cleanup impossible, quarantine native
tickets/module in the process registry until exit with diagnostics and no app calls;
ordinary Close/recreate must release all per-page resources without this fallback.

WebCore.dll remains pinned for process lifetime in M10. Hot-unload is deferred;
page and service quiescence is mandatory even though module code stays loaded.

Policy starts are synchronous on the owner thread; deferred notifications carry
page ID, navigation ID, document generation, and copied values. Revalidate after
reentrant handlers. Wrong-thread operations fail explicitly rather than quietly
accessing a foreign apartment. Two controls own separate pages and sessions.

## 7. Engine ABI and OS services

M10 `native-abi.md` defines exact V1 declarations, pack4 ARM32 offsets/sizes,
result/capability values, size negotiation, context retain/release acknowledgment,
clipboard completion and graphics interop. Task 3.0 freezes dual-compiler schema
conformance before producers/consumers; conflicting measured layouts require a
design revision, never implementation-selected ABI choices.
Native C++CX-facing code owns no WebCore types; exception-disabled engine code
owns no CX handles. The host supplies dispatcher and clipboard callbacks with
retained context, rather than passing arbitrary WinRT pointers into WebCore.

Navigation IDs are per-page monotonically increasing nonzero UInt64 values;
document generation and callback sequence are separate counters. The loader
creates/owns IDs; the control never attributes old events using mutable current
ID fields. Diagnostic logs are observational only. Preserve curl error codes,
adapter error domain, and platform HRESULT as distinct fields.

## 8. Rendering, input, transport, and policy

One control owns one rendering subtree. CPU mode presenter owns swap-chain buffers;
GPU mode engine ANGLE/Skia compositor owns them and host owns only the panel.
Attach/QI/AddRef/Release and mode-switch rules are explicit in native-abi.md;
never attach two swap chains to the panel. Native interface interop stays COM.
input adapter owns pointer/keyboard/focus tokens; scroll controller owns
gesture/inertia math; frame metrics owns samples. Avoid duplicate render
callbacks and multiple service-pump ticks merely because two controls exist.

Preserve Skia staging, DPI, pinned layers, cached scrolling, and compositor
readback diagnostics. CPU fallback reports its mode and does not substitute a
Cairo engine in a MiniBrowser package. DirectX COM uses native smart pointers.

Keep WebCore's actual URL/origin/response/subresource path. Loading is independent
of visual frame production. Each page has an isolated ephemeral cookie/session
context and bounded requests/buffering. CORS/CSP/mixed-content enforcement is
not recreated in the app. Host HTML uses opaque-origin substitute data and
about:blank, not remote curl output written into an active document loader.

Certificate consent uses WebKitCertificateFailureToken, a nonconstructible public
opaque object bound internally to unique page/session/failure revision. Numeric
page-local navigation IDs are diagnostics, not token identity. A validated retry
atomically consumes the token and creates a new Remote ID. The exception is session
and normalized HTTPS host/effective-port scoped, not global; cross-origin
redirects get no inherited exception. App-owned UI supplies consent. Ordinary
web content cannot invoke the native decision through a reserved URL scheme.

Host HTML gets its own cancellable Starting/terminal lifecycle with ContentKind=
HostHtml, absent HTTP status and opaque about:blank. It is transient history-neutral
content; existing engine history items/cursor remain unchanged. Back/Forward use
their existing neighbors; Reload reloads current HTML. Plain NavigateToString
invalidates pending consent. The explicit `(html, currentWarningToken)` overload
preserves only that decision through warning commits/replacements. BrowserPolicy
holds the remote token separately from HTML IDs. Other navigation/Stop/null/Close
invalidates it; complete transition table and trace tests are in public-api.md.

## 9. Build, metadata, and dependencies

Use CMake/Ninja for existing native engine targets and MSBuild W10M (AppContainer) projects for
CX components/apps and compiled XAML. A platform orchestration script builds
both without relying on one CMake compiler setting to handle incompatible
language modes. New code must record compiler, SDK, target machine, runtime,
WinMD and generated XAML provider paths in a build identity manifest.

Public C++/CX declarations produce component metadata. Legacy MIDL IDL is a
historical ABI reference; do not package an independently generated conflicting
WinMD beside compiler metadata. Inspect generated IIDs, class bases, threading
metadata, event delegates, and dependency properties. Incompatible signatures
receive new contract/interface identity; public runtime name may remain stable.

Prefer the standard generated activation mechanism and metadata provider. Keep
manual ABI factory code only if necessary and verified against the same generated
contract, with exception-to-HRESULT and AddRef ownership tests. A small WRL shell
is not permission to retain WRL-authored public controls/events/app composition.

All generated artifacts are build outputs. No developer profile paths in checked
in toolchains. Discover tools via VS/SDK installation metadata and accept explicit
overrides; fail on missing ARM32 libraries or runtime dependencies. Build only
validated configurations. Never hide missing CRT/EH symbols with fake runtime
implementations, blanket duplicate-symbol linking, or ARM64 substitution.

## 10. Migration and verification gates

1. Freeze a current baseline, including existing uncommitted probe work.
2. Prove the complete CX browser-free component and compiled-XAML host on phone.
3. Add shared ABI; isolate module/page/navigation ownership and typed callbacks.
4. Implement control, rendering/input/platform services, and independent instances.
5. Migrate MiniBrowser/diagnostics and implement the public device runner.
6. Extract old fixtures only after replacement coverage; apply remaining naming slices.
7. Audit package/import/metadata closure and run the acceptance matrix.
8. Obtain owner acceptance; only then retire compatibility files.

Build/package each slice; keep a known-good Skia package and legacy adapters until
replacement evidence exists. Roll back a failing slice to the recorded package
and compatible engine/component pair; do not delete earlier evidence or fabricate
completed tasks. Commit/push/tag require a separate explicit request.

M10 does not promise ExecuteScriptAsync, persistent/shared profiles, download,
permission/new-window deferrals, arbitrary schemes, JIT, or universal website
compatibility. Existing unsupported features remain visibly unsupported.

## 11. Implementation handoff

Read M10 `implementation-handoff.md`, `tasks.md`, and `acceptance-matrix.md`.
Complete gates in order. Record exact evidence for each checked task. No task is
complete solely because these documents or an object compile exist. Changes to
public names, Source semantics, metadata ownership, compiler boundary, security,
or acceptance need a design/spec revision before implementation.
