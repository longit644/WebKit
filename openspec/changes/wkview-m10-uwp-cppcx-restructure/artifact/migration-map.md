# M10 migration map and M9 transfer ledger

Companion to [../design.md](../design.md); supporting contract files are siblings
in this `artifact/` directory. Proposal, specs and tasks remain at the change root.

This manifest is the move/rename authority. Existing implementation is retained
until its replacement gate passes. Destination trees/naming are fixed in
`Tools/uwp/DESIGN.md`; record every additional discovered symbol under these
rules rather than making opportunistic renames. Implementation must expand
family rows into exact symbols/files and record consumers before changing them.

## Source/symbol map

| Existing owner / family | Destination and decision | Consumers / verification |
|---|---|---|
| Tools/uwp/WinRTComponent/WebKitWebViewComponent.cpp | Split public CX control into API/uwp/WebKitWebView.h/.cpp; typed event classes into WebKitNavigationEventArgs.h/.cpp | New WinMD, apps, public runner; metadata/ABI tests |
| WebKitWebView.idl and generated .h/_i.c/_p.* | Historical prototype reference; new CX declarations are metadata authority; generated replacement outputs under build | Package/IDL references, MiniBrowser; remove only after consumer migration |
| WebKitWinRTBase.h / CompletedAsyncAction | SDK CX async projection and shared initialization attempt | EnsureInitializedAsync concurrency/failure tests |
| WebKitWebViewUWP.h/.cpp / class | native/PageControllerUWP.h/.cpp, stripping XAML, gestures, presenter and module ownership | Component and app source lists; one reusable component, two-page tests |
| Nested EngineAPI / DLL HMODULE | native/EngineModuleUWP; negotiated API and process-lifetime pinning | GetProcAddress/header users; version rejection, shutdown and per-page close/recreate counts |
| WebKitWebViewPresenterUWP / FrameTiming | rendering/SwapChainPresenterUWP; FrameTimingUWP native POD in FrameMetricsUWP.h | DirectX use sites, compositor/readback; DPI/present/cache tests |
| Wrapper recordFrame/recordFramePacing/sample fields | rendering/FrameMetricsUWP; frameProfileSampleCount constant | Logs/capture consumer; profile units/protocol documented |
| Wrapper advanceFrame/render tokens | rendering/FrameSchedulerUWP | Owner thread/service pump; idle network and two-view frame tests |
| Wrapper pointer/XAML subscriptions | input/InputAdapterUWP | Pointer capture, tap/drag/wheel, keyboard/focus; device interaction |
| Wrapper applyScroll/resetMotion/inertia fields | input/ScrollControllerUWP | Native gesture algorithm; edge/scroll regression |
| PageUWP.h / WK* exports/result/state | Shared/uwp/WKUWP*.h; legacy export forwards temporarily | WebCore exports, smoke probes, wrapper; dual-compiler layout/import checks |
| PageUWP.cpp/PageHostUWP | PageHostUWP.h/.cpp with no fixtures | Frame/loader/compositor owners; creation/teardown tests |
| PageFrameLoaderUWP / PageChromeUWP | FrameLoaderClientUWP / ChromeClientUWP, separate files | Callback and override consumers; state/terminal tests |
| BackForwardClientUWP / StorageSessionProviderUWP | Separate existing-named .h/.cpp | History/session behavior and per-control isolation |
| Page navigation/ID/document state | NavigationControllerUWP.h/.cpp | Typed ABI payloads; redirect/replacement/reload identity |
| LoaderStrategyUWP.cpp/ResourceLoadUWP | Keep strategy; extract ResourceLoadUWP.h/.cpp | CurlRequestClient path; subresource/security/budget tests |
| addProceedException/removeProceedExceptions/hasProceedException | CertificateExceptionStoreUWP issuer/decision tokens plus WebKitCertificateFailureToken.h/.cpp | Loader/controller/BrowserPolicy; equal-local-ID collision, warning lifecycle and cross-host tests |
| ClipboardUWP projection/dispatcher pointers | Engine ClipboardUWP forwards neutral service; CX platform/ClipboardServiceUWP and DispatcherUWP | Pasteboard/frame callbacks; pending completion and Close |
| Process wake/clipboard hooks and deferred page destruction | platform/RuntimeOwnerUWP.h/.cpp cleanup registry and acknowledgment | App shutdown/services; failed enqueue, orphan fallback and normal close/recreate counts |
| CurlContext mutable SSL configuration and first-init font state | Immutable per-session/request CA policy; EngineUWP canonical font fingerprint | Windows curl backend, TLS/connection cache, first-page font setup; different-bundle/conflicting-font tests |
| FirstPageUWP.cpp startup/strategy/content | EngineUWP for generic startup; smoke payload under diagnostics/RuntimeSmoke | Search all exports/build references before split; retained smoke results |
| TextureMapperCompositorUWP/SkiaGPUContextUWP/TextureMapperFrameStatsUWP/DragImageUWP | Keep upstream-style platform names; remove only app/fixture leakage | Rendering, WebCore platform CMake; accepted Skia regression |
| MiniBrowserUWP.cpp/MiniBrowserApp | MiniBrowser::App, MainPage, BrowserViewModel, BrowserPolicy and assets | App project/manifest; declarative component and UX tests |
| MiniBrowser curl worker/shadow state/poll completion | Remove after component-backed equivalents pass | No app curl link/HTML shortcut or shadow history |
| MiniBrowser diagnostic/test-stage enums/fixture constants | NavigationTests device scenario state and fixture config | Old-to-new assertion comparison; no hard-coded LAN IP in production |
| blocked.html/demos | MiniBrowser assets/blocked.html and assets/demo | Packaging, LocalState override and HTML escaping; TLS UX tests |
| RuntimeSmoke/FpuProbe/TlsSmoke hosts | Tools/uwp/diagnostics/<name>/, CX Application/CoreApplication projections | Projects, scripts, native payload entrypoints; retained results |
| NavigationTests/server.py/test_server.py | host/fixture-server.py and host/test-fixture-server.py; fixtures/ separate content | README/imports/launchers; transitional forwarding wrappers |
| Tools/uwp/arm32-uwp-env.ps1/build-runtime.ps1 | build/arm32-native-env.ps1 and build/build-native-runtime.ps1 | Native projects/vcpkg invocation; discovery, wrapper compatibility |
| audit-dlls.py/device-portal.py | packaging/audit-dependencies.py and packaging/device-portal.py | Staging/deployment callers; validated CLI compatibility |
| audit-source-docs.py | Keep entrypoint or documented forwarding wrapper; CX-aware audit rules | Style verification and generated-output exclusions |
| toolchain native C/C++/assembly/ports/lib | Keep native language and role; explicit library producer/output ownership | Runtime ABI probes, vcpkg ports, linker configuration |
| WebKitLibraries toolchains/triplets, UWP-conditioned WTF/PAL/network files | Keep upstream directory; update references/discovery/native ABI as required | Full scoped search plus affected named-target builds |

## Symbol-family rules

- Public type/member names follow `public-api.md`; never reuse changed prototype
  interface IIDs. Keep outcome numbers 0/1/2 and separately version native ABI.
- Native private fields use m_lowerCamelCase; functions use lowerCamelCase;
  upstream override methods keep mandated names. One declared field per documented
  state responsibility, not compressed groups of unrelated Boolean flags.
- Internal enum values are scoped PascalCase. Existing UWPResourceErrorCode values
  1..12 and legacy result codes keep numerics until compatibility is retired;
  rename via explicit mappings, never reinterpret domain-less integers.
- Public enum values and non-obvious constants require individual documentation.
  Rename kFrameProfileSampleCountUWP to frameProfileSampleCount, profile time/capacity
  families to unit-bearing names, and remove constants for ordinary syntax zeros.
- Do not rename SDK, curl, upstream WebKit enums, methods, types, third-party files
  or assembly-required exports. Document each retained native/ABI name.
- Acronym spelling is `UWP`, not `Uwp`: native classes/files use the `UWP` suffix;
  shared C ABI uses `WKUWP`; lowercase directory `uwp/` stays unchanged.
- Build target names: WebKitUWPComponent (emits WebKit.dll and WebKit.winmd to
  match the public WebKit namespace), MiniBrowserUWP,
  NavigationTestsUWP, XamlIdentityProbeUWP, RuntimeSmoke, FpuProbe, TlsSmoke; engine
  WebCore/JavaScriptCore targets remain unchanged. Package runtime class stays WebKit.

## M9 requirement disposition

| M9 task group | M10 destination | Evidence policy |
|---|---|---|
| 1.1-1.5 contracts/fixtures | Architecture/API/ABI specs; Phase 0 and fixture replacement | Reuse loader/fixture maps; new contracts are not marked implemented |
| 2.1/2.2 initialization/session | Native bridge/resource specs; Phase 2/3 | New two-instance and startup evidence required |
| 2.3 persistence | Deferred | No persistent/shared profile claim |
| 2.4 TLS/consent | Resource spec and typed decision API | Retain device-tls-2.4/device-proceed-72; eliminate URL-based proceed |
| 3.1-3.5 navigation/history/state/HTML | Navigation spec and Phase 2/3 | Retain v64/v66/v68 history records; repeat against new package |
| 4.1-4.6 resources/cancellation/budgets | Resource/bridge/device specs | Retain v62/v63 and teardown findings; new close/budget evidence |
| 5.1-5.5/5.7 component/metadata/lifetime | CX/control/build specs; feasibility before integration | Earlier WRL/XAML findings are failure history, not acceptance |
| 5.6 new-window/download/permission | Deferred | Unhandled remains unsupported/fail-closed |
| 6.1-6.3 shell migration | Application spec; Phase 5 | Preserve Skia staging/demo/blocked UX, repeat app checks |
| 7.1-7.6 packaging/device/sites/acceptance | Build/device specs; Phase 7 | Exact build/package identity and owner observations required |

M7 fonts/clipboard and M8 rendering retain their own history and requirements;
M10 migrates integration and reruns affected regressions, not silently cancels them.

## Per-slice record format

For each touched symbol/file record old path/name, new path/name, language/owner,
all include/export/project/script/doc consumers, compatibility rule, task ID,
verification command/result, and device evidence when applicable. Mark retained
and generated families explicitly. No area is complete until its manifest has
zero unaccounted authored UWP symbols; remaining legacy compatibility is listed.
