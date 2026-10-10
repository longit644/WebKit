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
| Nested EngineAPI / DLL HMODULE | native/EngineModuleUWP; negotiated API and process-lifetime pinning | `Source/WebKit/UIProcess/API/uwp/native/EngineModuleUWP.h/.cpp` binds the 3.2 table by loader plus `GetProcAddress`, pins it for process lifetime, tracks per-page ownership and latches shutdown; evidence in `engine-module-pinning.md`. Legacy GetProcAddress/header users migrate to it in task 3.6; version rejection, shutdown and per-page close/recreate counts are proven against a test-double provider, not the real engine |
| WebKitWebViewPresenterUWP / FrameTiming | rendering/SwapChainPresenterUWP; FrameTimingUWP native POD in FrameMetricsUWP.h | Extracted in 6.1; compatibility alias, native COM/mode tests and CPU/Skia phone baseline in swap-chain-presenter.md; landscape remains 6.3 |
| Wrapper recordFrame/recordFramePacing/sample fields | rendering/FrameMetricsUWP; frameProfileSampleCount constant | Extracted in 6.1; seconds input, millisecond values, microsecond/milli-Hertz report protocol and reset/window tests in swap-chain-presenter.md |
| Wrapper advanceFrame/render tokens | rendering/FrameSchedulerUWP and private platform/FrameSchedulerDriverUWP | Extracted in 6.2; native epoch/lease state, one CX owner timer/render hook, real paused CSS/script/timer/fetch DOM and two-view deduplication in shared-frame-scheduling.md |
| Wrapper pointer/XAML subscriptions | input/InputAdapterUWP | Extracted in 6.4; gesture classification, tap slop/offset, capture-cancel and per-control state in input-controllers.md |
| Wrapper applyScroll/resetMotion/inertia fields | input/ScrollControllerUWP | Extracted in 6.4; kinetic/edge physics, wheel rules and owner-thread guards in input-controllers.md |
| PageUWP.h / WK* exports/result/state | Shared/uwp/WKUWP*.h with `Source/WebCore/platform/uwp/LegacyExportMapUWP.h` as the explicit per-export map; callable forwards await tasks 4.x | WebCore exports, smoke probes, wrapper; dual-compiler layout/import checks |
| PageUWP.cpp/PageHostUWP | PageHostUWP.h/.cpp with no fixtures | Frame/loader/compositor owners; creation/teardown tests |
| PageFrameLoaderUWP / PageChromeUWP | FrameLoaderClientUWP / ChromeClientUWP, separate files | Callback and override consumers; state/terminal tests |
| BackForwardClientUWP / StorageSessionProviderUWP | Separate existing-named .h/.cpp | History/session behavior and per-control isolation |
| Page navigation/ID/document state | NavigationControllerUWP.h/.cpp | Typed ABI payloads; redirect/replacement/reload identity |
| LoaderStrategyUWP.cpp/ResourceLoadUWP | Keep strategy; extract ResourceLoadUWP.h/.cpp | CurlRequestClient path; subresource/security/budget tests |
| addProceedException/removeProceedExceptions/hasProceedException | CertificateExceptionStoreUWP issuer/decision tokens plus WebKitCertificateFailureToken.h/.cpp | Loader/controller/BrowserPolicy; equal-local-ID collision, warning lifecycle and cross-host tests |
| ClipboardUWP projection/dispatcher pointers | Engine ClipboardUWP forwards neutral service; CX platform/ClipboardServiceUWP and DispatcherUWP; platform/ClipboardRouteUWP retained-token router in 7.2 | Extracted in 7.1; neutral exactly-once service, internal dispatcher hop and WinRT text transport in clipboard-service.md; routed copy/cut/paste, idempotent Close and close-race exactness in clipboard-routing.md with Lumia v1.0.0.44 round trips; engine headers keep no CX/WinRT ownership, table wiring stays 7.5 |
| Process wake/clipboard hooks and deferred page destruction | platform/RuntimeOwnerUWP.h/.cpp cleanup registry and acknowledgment | Implemented in 7.5: ticketed clipboard routing, hook revocation, shutdown with acknowledged cancellation, dispatcher-loss drops, engine pin survival and generation counts in runtime-owner.md; production control adoption stays section 8 |
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
Their specific dependencies are listed under "M7/M8 regression dependencies".

## M7/M8 regression dependencies

| Origin | Scope M10 must not regress | Why M10 touches it |
|---|---|---|
| M7 | FreeType/fontconfig font stack, packaged `Fonts` directory, WinRT text clipboard, native drag-image previews | Canonical fingerprint replaces first-init font config (4.11); host clipboard service plus retained-token router are device-proven (7.1-7.2, v44 round trip) while the engine self-serves clipboard internally (7.3); drag images are no-op stubs; runtime-owner wiring stays 7.5; no M7 behavior is regressed, only re-homed with fail-closed gaps explicit |
| M8 | Skia Ganesh GPU tile painting, `USE_SKIA=ON` staging guard, compositing/readback diagnostics, resize/DPI behavior | Tasks 6.1-6.3 split presenter/scheduler/metrics and change CPU-vs-GPU surface ownership |
| M7/M8 device records | `Tools/uwp/RUNTIME-AUDIT.md`, `SKIA-PORT.md`, `ARM-WALLS.md` remain the accepted-baseline references | Task 11.3 compares new measurements against them |

## Exact symbol inventory: framework API and engine

Read-only inventory measured 2026-10-05 (task 1.2). Line numbers refer to the
working tree; the five locally modified/untracked paths are inventoried in their
modified state, not at HEAD.

### Source/WebKit/UIProcess/API/uwp

`WebKitWebViewUWP.h` (251 lines): `constexpr size_t kFrameProfileSampleCountUWP = 64` at 14; class `WebKit::WebKitWebViewUWP` at 19 with destructor 22, `initialize` 25, `xamlElement` 28, `loadHTML` 31, `navigateRemote` 34, `goBack` 37, `goForward` 40, `reload` 43, `stop` 46, `setNavigationPolicyHandler` 49, `scrollBy` 52, `pageState` 55, `repaint` 58, `savePNG` 61; private `resize` 64, `useFallbackPresenter` 67, `pointer` 70, `report` 73, `advanceFrame` 76, `applyScroll` 79, `resetMotion` 82, `now` 85, `recordFrame` 88, `recordFramePacing` 91; nested `struct EngineAPI` at 95 whose 21 `decltype(&WK...UWP)` members occupy 98-158; fields `m_engine` 162, `m_page` 165, `m_owner` 168, `m_log` 171, `m_rootElement` 174, `m_panelElement` 177, `m_inputElement` 180, `m_composition` 183, `m_clip` 186, `m_sizeToken` 189, `m_presenter` 192, `m_pixels` 195, `m_width`/`m_height` 198, `m_pixelWidth`/`m_pixelHeight` 199, `m_scaleX`/`m_scaleY` 202, `m_presenterReady`/`m_inPaint`/`m_pointerDown`/`m_dragging` 205, `m_compositorActive`/`m_compositorAttempted`/`m_surfaceReady` 208, `m_startX`/`m_startY`/`m_lastY` 211, `m_pendingScrollX`/`m_pendingScrollY` 214, `m_redrawPending` 217, `m_counterFrequency`/`m_lastPointerTime`/`m_lastMotionTime`/`m_lastFrameTime` 220, `m_velocityY`/`m_overscrollY`/`m_springVelocity` 223, `m_inertia`/`m_pendingElastic` 226, `m_profileFrames` 229, `m_profileStart` 234, `m_frameWorkSamples`/`m_frameIntervalSamples` 237, `m_frameWorkCount`/`m_frameIntervalCount`/`m_frameWarmupCount` 240, `m_frameOverBudget`/`m_frameLongIntervals` 243, `m_previousFrameSubmitted` 246, `m_previousFrameStart`/`m_scrollWorkTotal`/`m_engineWorkTotal`/`m_renderWorkTotal` 249. `.cpp` constants include `kMillisecondsPerSecond = 1000.0` at 44. Port-authored and renameable.

`WebKitWebViewPresenterUWP.h` (86 lines): class `WebKit::WebKitWebViewPresenterUWP` at 10; nested POD `struct FrameTiming { double uploadMS; copyMS; presentMS; }` at 13 with members 15/18/21; `initializeForCoreWindow` 25, `initializeForSwapChainPanel` 28, `present` 31, `resize` 34, `setCompositionScale` 37, `setCompositionOffset` 40, `presentCachedFrame` 43, `frameTiming` 46; private `now` 50, `initializeDevice` 53, `description` 56; fields `m_device` 59, `m_context` 62, `m_swapChain` 65, `m_upload` 68, `m_width` 71, `m_height` 74, `m_compositionScaleX`/`m_compositionScaleY` 77, `m_frequency` 80, `m_timing` 83. `.cpp` constants: `kSwapChainBufferCount = 2` 11, `kPresentSyncInterval = 1` 13, `kNoDXGIFlags = 0` 14, `kFirstSwapChainBuffer = 0` 15, `kFirstSubresource = 0` 16, `kNoSourceDepthPitch = 0` 17, `kSingleMipLevel = 1` 18, `kSingleTextureArraySlice = 1` 19, `kSingleSampleCount = 1` 20, `kMillisecondsPerSecond = 1000.0` 21, `kZeroCompositionOffset = 0.0f` 22. `DXGI_SWAP_CHAIN_DESC1` and the COM device members are upstream-style native and keep their types.

### Source/WebCore/platform/uwp

`PageUWP.h` (250 lines), ABI-mandated numerics that must not drift:

- `enum UWPValidationLimit` 16: `UWPMaxPageDimension = 2048` 18, `UWPMaxPaintDimension = 4096` 20, `UWPMaxPaintStride = 16384` 22, `UWPMaxDeviceScale = 8` 24, `UWPBytesPerBGRA = 4` 26.
- `UWPDefaultFontSize = 18` 34, `UWPDefaultFontFamily[] = "DejaVu Sans"` 35.
- `enum WKPageResultUWP` 39: `WKPageResultOkUWP = 0` 41, `WKPageResultInvalidArgumentUWP = 1` 43, `WKPageResultNoMainFrameUWP = 4` 46, `WKPageResultNoActiveDocumentLoaderUWP = 5` 48, `WKPageResultNoPaintSurfaceUWP = 6` 50, `WKPageResultDocumentWriterBeginFailedUWP = 7` 52, `WKPageResultWrongThreadUWP = 8` 54, `WKPageResultCompositorConfigurationUnsupportedUWP = 9` 56, `WKPageResultCompositorUnavailableUWP = 10` 58. Values 2 and 3 are unused gaps.
- `enum WKPageLogBooleanUWP` 69: `WKPageLogFalseUWP = 0` 71, `WKPageLogTrueUWP = 1` 73.
- `enum WKNavigationFailureUWP` 78: `WKNavigationFailureCancelledUWP = 1` 80.
- `enum WKPointerEventTypeUWP` 84: `WKPointerEventPressUWP = 0` 86, `WKPointerEventMoveUWP = 1` 88, `WKPointerEventReleaseUWP = 2` 90.
- `enum WKPointerModifierUWP` 96: `WKPointerModifierNoneUWP = 0` 98, `WKPointerModifierShiftUWP = 1` 100, `WKPointerModifierControlUWP = 2` 102, `WKPointerModifierAltUWP = 4` 104.
- `struct WKPageStateUWP` 111: `width`, `height` 113, `scrollX`, `scrollY` 115, `contentWidth`, `contentHeight` 117, `canGoBack` 120, `canGoForward` 122.
- `enum WKDocumentChangeKindUWP` 131: `WKDocumentChangeKindNoneUWP = 0` 133, `WKDocumentChangeKindFragmentUWP = 1` 135, `WKDocumentChangeKindPushStateUWP = 2` 137, `WKDocumentChangeKindReplaceStateUWP = 3` 139, `WKDocumentChangeKindPopStateUWP = 4` 141.
- `struct WKPageDocumentStateUWP` 150: `title` 154, `httpStatus` 160, `hasHttpStatus` 162, `documentChange` 164, `redirectHops` 166.
- Callback typedefs: `WKNavigationPolicyCallback` 5, `WKPageLogUWP` 13. Exports declared 173-249.

`LoaderStrategyUWP.h` (180 lines): `enum UWPResourceErrorCode` 17 with `UWPResourceErrorCancelled = 1` 19, `UWPResourceErrorBlocked = 2` 21, `UWPResourceErrorBlockedByContentBlocker = 3` 23, `UWPResourceErrorUnsupportedURL = 4` 25, `UWPResourceErrorInterruptedForPolicyChange = 5` 27, `UWPResourceErrorUnsupportedMIMEType = 6` 29, `UWPResourceErrorFileDoesNotExist = 7` 31, `UWPResourceErrorRedirectFailed = 8` 33, `UWPResourceErrorHTTPSOnly = 9` 35, `UWPResourceErrorPluginWillHandleLoad = 10` 37, `UWPResourceErrorByteBudgetExceeded = 11` 39, `UWPResourceErrorSynchronousUnsupported = 12` 41. Free functions `storageSessionUWP` 56 (defined in `PageUWP.cpp`), `addProceedException` 63, `removeProceedExceptions` 66, `hasProceedException` 69. Typedef `LoaderStrategyUWPLog` 71 (same type as `WKPageLogUWP`). `setLoaderStrategyUWPLog` 74. Class `LoaderStrategyUWP` 78 with `m_loads` 175, `m_suspended` 178.

Remaining engine files: `ClipboardUWP.{h,cpp}` (namespace `WebCore::ClipboardUWP`; `readTextAsync`, `writeTextAsync`, `clearAsync`, `setUIDispatcher` exported, `clear`/`writeText`/`readText` not), `DragImageUWP.{h,cpp}`, `FirstPageUWP.cpp` (also hosts `WKInitializeEngineUWP`, `WKRenderFirstPageUWP`, `FirstPageResultUWP`, `FirstPageLoaderStrategy`, `FirstPageMediaStrategy`, `FirstPagePlatformStrategies`), `TextureMapperCompositorUWP.{h,cpp}` (includes `TextureMapperFrameStatsUWP.h` and `SkiaGPUContextUWP.h`), `TextureMapperFrameStatsUWP.h`, `SkiaGPUContextUWP.h`. Internal-linkage types inside `PageUWP.cpp`: `StorageSessionProviderUWP` 81, anonymous-namespace `BackForwardClientUWP` 110, `struct PageHostUWP` 213 (including the fixture-suite fields), `PageChromeUWP` 827, `PageFrameLoaderUWP` 887, `enum FixtureSuitePhase` 388, `fixtureBodyText`/`probeFixtureMarkers`/`scheduleSettleProbe`/`scheduleFixtureSuite` and the `uwp-suite=1` trigger at 1008.

### Tools/uwp/WinRTComponent

`WebKitWebView.idl` (206 lines) prototype identities, all superseded by the M10 metadata revision: `WebKitNavigationOutcome` 13 (`Completed = 0`, `Failed = 1`, `Cancelled = 2`); delegates `WebKitNavigationEventHandler` 28 (`331252db-3b4c-4011-b830-b7a34cc5b68c`), `WebKitNavStartingHandler` 34 (`e2aaa8e0-f399-4a52-bc39-3f55669bec24`), `WebKitSourceChangedHandler` 40 (`7813f950-7875-4d1f-841d-b71419cc3865`); interfaces `IWebKitWebView` 48 (`da4dd758-dbf5-4568-8074-d92300fdb7b8`), `IWebKitNavigationStartingEventArgs` 126 (`df4d11eb-659d-478d-a21e-f3a1b1ea0903`), `IWebKitSourceChangedEventArgs` 144 (`1e12f19a-bd8e-4bf9-be0c-2f4de1534766`), `IWebKitNavigationEventArgs` 156 (`0d6aeba7-2b48-4508-9751-6b56cf1d64dc`); runtime classes `WebKitNavigationStartingEventArgs` 173, `WebKitSourceChangedEventArgs` 181, `WebKitNavigationEventArgs` 189, `WebKitWebView : UserControl` 200. Documented divergences from the M10 contract: `Source` is `HSTRING` not `Uri^`; one `NavigateToString` overload only; no `ProceedWithCertificateFailure`, no `Close`, no `IsLoading`/`DocumentTitle`; no error-domain, HTTP-status, content-kind or token members; `HistoryChanged` and completion share one args interface.

`WebKitWebViewComponent.cpp` (766 lines, WRL): namespace `WebKitComponent` 33 with alias `UIXaml` 31; preprocessor shims `typedef Windows::Foundation::IAsyncAction __x_Windows_CFoundation_CIAsyncAction;` 20 and `RO_NO_TEMPLATE_NAME` 23/25; constants `kWebKitWebViewClassName` 36, `kHttpSchemePrefix` 37, `kHttpsSchemePrefix` 38, `kUriPrefixSearchStart` 39, `kUtf8ConversionFlags` 40, `kConversionSizeQueryOnly` 41, `kNullTerminatedInputLength` 42, `kNullTerminatorCharacterCount` 43, `kEmptyStringLength` 44, `kZeroInitializedByte` 45, `kWebKitWebViewClassNameLength` 46, `kInitialComponentNavigationId` 47, `kCompletedAsyncActionId` 48, `kWebKitWebViewInterfaceCount` 49, `kFactoryInterfaceCount` 50, `kNoNavigationErrorCode` 51; classes `WebKitNavigationEventArgs` 54, `WebKitNavigationStartingEventArgs` 107, `WebKitSourceChangedEventArgs` 158, `CompletedAsyncAction` 189, `WebKitWebView` 244, `WebKitWebViewFactory` 707; exports `DllGetActivationFactory` 743, `DllCanUnloadNow` 754, `DllMain` 759. Behavior to remove rather than port: log-string parsing in `OnEngineLog` 547 with prefix literals 555-557, manual fragment stripping in `sameDocument` 646, process-global routing through `s_sinkInstance` 699/703, single-instance refusal in `ensureInitialized` 504, and the `WebKit.CompletedAsyncAction` runtime-class name 190 that has no IDL entity.

`WebKitWinRTBase.h` (77 lines): `enum class AsyncStatusValue : INT32` 14 (`Started = 0`, `Completed = 1`, `Canceled = 2`, `Error = 3`); hand-declared OS interfaces `IAsyncInfo` 29 (`00000036-0000-0000-C000-000000000046`), `IAsyncActionCompletedHandler` 53 (`a4ed5c81-76c9-40bd-8be6-b1d90fb0ae9d`), `IAsyncAction` 62 (`5a648006-843a-4da9-865b-9d26e5dfad7b`). These IIDs and names are OS-mandated and must be retained verbatim; the file is retired once SDK async projection is used (migration-map row 18).

`XamlProbe.idl` (42 lines) and `XamlProbeComponent.cpp` (128 lines, the only authored `/ZW` source): `IXamlProbe` uuid `6d1c8f44-2b90-4a37-8c65-1f9d3b7a5e20` at IDL 22 and again hand-written in the .cpp at 32; `public ref class XamlProbe sealed : UserControl, IXamlProbe` 41; constants `kProbeId` 21, `kProbeClassName` 22, `kProbeFillAlpha` 23, `kProbeFillRed` 24, `kProbeFillGreen` 25, `kProbeFillBlue` 26, `kProbeRequestedWidth` 27, `kProbeRequestedHeight` 28; exports 106/120/125. Not referenced by any build file.

### Generated MIDLRT artifacts on disk

`Tools/uwp/WinRTComponent/WebKitWebView.h` (1534 lines), `WebKitWebView_i.c` (97), `WebKitWebView_p.h` (1153), `WebKitWebView.winmd` (9216 bytes) are untracked generated products of the prototype IDL, ignored by `.gitignore` lines 126-129. `WebKitWebView_i.c` records `target_arch=AMD64` and a 2038 timestamp, so it was generated for the wrong architecture and must be regenerated before any ABI comparison (task 2.3). Stray `WebKitWebView_p.c`, `XamlProbe_p.c` and `dlldata.c` sit at the repository root (ignored by `.gitignore` 119/121/139) because the documented recipe does not set a working directory or output path. No source includes the IDL, `_i.c` or the `.winmd`.

## Export and ABI surface

`WKUWPGetEngineAPI` now has a producer: `Source/WebCore/platform/uwp/WKUWPEngineAPI.cpp` exports it from the `WebCore` target under `WK_UWP`, with all 24 V1 slots filled and every not-yet-implemented slot reporting `WKUWPResultUnsupported` (task 3.2, evidence in `engine-api-discovery.md`). Its consumer, `EngineModuleUWP`, still does not exist; that is task 3.3. Alongside the new table the legacy surface is unchanged: today `WebKitWebViewUWP.cpp:88-108` binds exactly 21 exports, each matched 1:1 by a `decltype` in `WebKitWebViewUWP.h:98-158`: `WKCreatePageUWP`, `WKDestroyPageUWP`, `WKLoadHTMLUWP`, `WKNavigatePageUWP`, `WKGoBackPageUWP`, `WKGoForwardPageUWP`, `WKReloadPageUWP`, `WKStopPageUWP`, `WKSetNavigationPolicyHandlerPageUWP`, `WKResizePageUWP`, `WKScrollPageUWP`, `WKPointerPageUWP`, `WKTickPageUWP`, `WKPaintPageUWP`, `WKPaintPageScaledUWP`, `WKPageNeedsPaintUWP`, `WKGetPageStateUWP`, `WKEnableCompositorUWP`, `WKDisableCompositorUWP`, `WKRenderCompositorUWP`, `WKInstallClipboardDispatcherUWP`. Those legacy forwarders are task 3.6 and are retained unchanged until then.

Exported but never bound or consumed outside the engine: `WKInitializeEngineUWP` (declared `PageUWP.h:180`, defined `FirstPageUWP.cpp:191`, called in-engine at `PageUWP.cpp:1167`) and `WKGetPageDocumentStateUWP` (declared `PageUWP.h:236`, defined `PageUWP.cpp:1679`). `WKRenderFirstPageUWP` (defined `FirstPageUWP.cpp:261`) has no header declaration at all; `Tools/uwp/RuntimeSmoke/SmokeHost.cpp:198` resolves it with `GetProcAddress` and re-declares the signature inline via `reinterpret_cast`, so signature drift cannot be caught by a header.

## Consumers and build wiring

Include edges: `PageUWP.h` from `PageUWP.cpp:2`, `TextureMapperCompositorUWP.h:3`, `WebKitWebViewUWP.h:2`, `WebKitWebViewPresenterUWP.cpp:2`, `FirstPageUWP.cpp:24`, `RuntimeSmoke/SmokeHost.cpp:2`. `LoaderStrategyUWP.h` from `PageUWP.cpp:25`, `LoaderStrategyUWP.cpp:2`, `FirstPageUWP.cpp:17`. `ClipboardUWP.h` from `ClipboardUWP.cpp:3`, `PageUWP.cpp:5`, and `Source/WebCore/platform/win/PasteboardWin.cpp:31` by the relative path `"../uwp/ClipboardUWP.h"`. `DragImageUWP.h` from `DragImageUWP.cpp:6` and `Source/WebCore/platform/DragImage.h:38`. `TextureMapperFrameStatsUWP.h` and `SkiaGPUContextUWP.h` from `TextureMapperCompositorUWP.cpp:13` and from `Source/WebCore/platform/graphics/texmap/BitmapTexture.cpp:40,44`. `WebKitWebViewUWP.h` from its own `.cpp:1`, `WebKitWebViewComponent.cpp:26`, `MiniBrowserUWP.cpp:4`. `WebKitWebViewPresenterUWP.h` from `WebKitWebViewUWP.h:3`, its own `.cpp:1`, `SmokeHost.cpp:3`. `WebKitWinRTBase.h` from `WebKitWebViewComponent.cpp:3` and `MiniBrowserUWP.cpp:5`. Generated `WebKitWebView.h` from `WebKitWebViewComponent.cpp:24`.

CMake edges: `Source/WebCore/PlatformWin.cmake:22-30` appends `platform/uwp/{ClipboardUWP,DragImageUWP,FirstPageUWP,PageUWP,LoaderStrategyUWP,TextureMapperCompositorUWP}.cpp`, registers `ClipboardUWP.h` and `DragImageUWP.h` as private framework headers, and adds `platform/uwp` to `WebCore_PRIVATE_INCLUDE_DIRECTORIES`; `Source/WebCore/platform/Curl.cmake:65-67` appends `platform/network/curl/PublicSuffixStoreUWP.cpp`. `Tools/uwp/WinRTComponent/CMakeLists.txt` builds target `WebKitWebView` from `WebKitWebViewComponent.cpp` plus the two wrapper sources, exports `DllGetActivationFactory`/`DllCanUnloadNow`, and hard-codes `C:/vcpkg/installed/arm-uwp-webkit/include`. `Tools/MiniBrowser/uwp/CMakeLists.txt` and the three diagnostic `CMakeLists.txt` files compile the wrapper sources directly into each executable. `PageUWP.h`, `LoaderStrategyUWP.h`, `TextureMapperCompositorUWP.h`, `TextureMapperFrameStatsUWP.h`, `SkiaGPUContextUWP.h`, `WebKitWebViewUWP.h` and `WebKitWebViewPresenterUWP.h` are registered in no CMake target.

`WK_UWP` is defined in exactly one place, `WebKitLibraries/toolchains/Toolchain-ARM32-UWP-clang.cmake:23`, with `WK_UWP_TRIPLE` 16, `WK_UWP_CLANG_CL` 29, `WK_UWP_RC` 41, `WK_UWP_C_FLAGS` 61, `WK_UWP_LINK_FLAGS` 89, `WK_UWP_BUILTINS_ARM` 93; every other `WK_UWP` consumer depends on that file. `WTF_PLATFORM_UWP=1` is added at line 24 and is what `PLATFORM(UWP)` expands from, so no separate `PLATFORM_UWP` definition is required.

## Collisions and stale references

Each item is a required action for the task named, not a finding to re-discover later.

1. `WKGetPageDocumentStateUWP` and `WKPageDocumentStateUWP` are dead across the component boundary: declared/defined but absent from `EngineAPI` and from the bind list, with no consumer in `Source/` or `Tools/`. Task 4.3 wires the structured equivalent; the legacy forwarder must not be presented as a working feature.
2. `WKInitializeEngineUWP` is defined in `FirstPageUWP.cpp`, a file this map treats as smoke-only content. Task 4.1 moves reusable startup to `EngineUWP`.
3. `WKRenderFirstPageUWP` has no header declaration; task 8.5 must give the smoke payload a declared signature instead of an inline `reinterpret_cast`.
4. `WKNavigatePageUWP` is declared twice in `PageUWP.h` (line 8 with a comment describing a different symbol's purpose, and line 192). Task 3.6 collapses legacy declarations.
5. `FirstPageResultUWP` values 2 (`NoFontConfigUWP`) and 3 (`FontsFailedUWP`) are returned by `WKInitializeEngineUWP` and pass through `WKCreatePageUWP` into a `WKPageResultUWP*` with no counterpart value, where `WebKitWebViewUWP.cpp` maps them to `E_FAIL`. Task 3.6 must map explicitly rather than reinterpret domain-less integers, and task 4.11 covers the font-configuration cause.
6. `kFrameProfileSampleCountUWP` appears at both `WebKitWebViewUWP.h:14` and `:237`; both sites change together in task 6.1.
7. `kMillisecondsPerSecond` is defined in both `WebKitWebViewUWP.cpp:44` and `WebKitWebViewPresenterUWP.cpp:21` inside `namespace WebKit`. Internal linkage avoids an ODR break, but the split into `FrameMetricsUWP` and `SwapChainPresenterUWP` must keep one documented owner.
8. `WebKit.CompletedAsyncAction` (component `.cpp:190`) is a runtime-class name absent from the IDL and the winmd. Task 10.2 retires it with the prototype.
9. `C++ class WebKitWebView` (component `.cpp:244`) collides with runtime class `WebKit.WebKitWebView` and with the planned authored path `Source/WebKit/UIProcess/API/uwp/WebKitWebView.h/.cpp`. Record an explicit rule: the CX ref class keeps the public name; the native controller is `PageControllerUWP`.
10. The untracked generated `Tools/uwp/WinRTComponent/WebKitWebView.h` occupies exactly the path M10 assigns to the new authored header. Task 10.1 must delete or relocate generated output before authoring there; the IDL recipe must emit into the build directory.
11. `Tools/uwp/WinRTComponent/CMakeLists.txt` declares `project(WebKitWebViewComponent)` but target `WebKitWebView`; this map requires `WebKitUWPComponent`. Task 10.1/10.3 aligns the name and emits `WebKit.dll`/`WebKit.winmd` under build output.
12. Hard-coded developer and SDK paths in `WinRTComponent/CMakeLists.txt:12`, `WinRTComponent/README.md:10-12,44-49`, the ARM32 toolchain file (lines 29, 41, 61, 89, 93), the `arm-uwp-webkit` triplet (31, 38, 44, 50-51, 57, 61, 64), `Tools/uwp/arm32-uwp-env.ps1`, `build-runtime.ps1`, `toolchain/clang-cl-arm.sh`, `toolchain/link.sh`, `toolchain/link-shim.c`, `TlsSmoke/CMakeLists.txt:8-9`, `MiniBrowser/uwp/CMakeLists.txt:12-13`, `Tools/uwp/ports/pthreads/portfile.cmake` and several READMEs. Task 2.7/10.3 replaces them with discovery plus recorded overrides.
13. `WebKitWebView.idl:1`, `XamlProbe.idl:1` and `XamlProbeComponent.cpp:1` reference `design-winrt-component.md` / `control-contract.md` as if they were repository design documents; neither exists there any more. Task 10.6 repoints them at the M10 change.
14. `.gitignore:116` names `Source/WebKit/UIProcess/API/uwp/WinRT/`, which does not exist. Task 10.5/10.6 corrects it to the M10 layout.
15. The documented MIDLRT recipe does not set a working directory or `/O`, so running it produces stray proxy sources at the repository root. Task 2.7 documents the build-directory form.
16. `Tools/uwp/RuntimeSmoke/AppxManifest.xml:15` declares EntryPoint `WebKitRuntimeSmoke.App` while the implemented factory class is `WebKitRuntimeSmoke.ViewSource` (`SmokeHost.cpp:330`); `Tools/uwp/TlsSmoke/AppxManifest.xml:15` declares `WebKitWebView.TlsSmoke.App` against `WebKitTlsSmoke.ViewSource` (`TlsSmoke.cpp:338`). Only `FpuProbe/AppxManifest.xml:19` matches, and its comment records that a mismatch kills the process. Task 8.5/8.7 must reconcile the manifests with the CX application classes.
17. `FirstPageLoaderStrategy` (`FirstPageUWP.cpp:74-167`) is never instantiated: the only installed strategies object is `FirstPagePlatformStrategies` 173, whose loader factory returns `LoaderStrategyUWP`. Task 4.1 deletes it with the smoke split.
18. Fixture-suite state lives in production engine source: `FixtureSuitePhase` (29 values), `fixtureBodyText`, `probeFixtureMarkers`, `scheduleSettleProbe`, `scheduleFixtureSuite`, the `PageHostUWP` fixture fields and the `uwp-suite=1` trigger at `PageUWP.cpp:1008`. Task 9.7 removes them after replacement coverage.
19. `storageSessionUWP` is declared in `LoaderStrategyUWP.h:56` but defined in `PageUWP.cpp:75`; splitting either file without the other silently severs it.
20. `StorageSessionProviderUWP`, `PageChromeUWP`, `PageFrameLoaderUWP` and `BackForwardClientUWP` have no headers (the latter two are inside an anonymous namespace). Task 4.2 promotes them to the planned named headers.
21. `ClipboardUWP.h` exports the async entry points but not `clear`/`writeText`/`readText`; the export surface inside one namespace is inconsistent and must be decided in task 7.1.
22. Three different mechanisms reach `platform/uwp` headers: the CMake private include dir, an absolute path in the component CMake file, and the relative include in `PasteboardWin.cpp:31`. Task 10.1 normalizes them.
23. `Source/WebCore/platform/network/curl/PublicSuffixStoreUWP.cpp` overrides upstream `PublicSuffixStore` virtuals (`platformIsPublicSuffix` 9, `platformTopPrivatelyControlledDomain` 14). Those names are upstream API and must be retained; the file itself is the port-authored unit. `CurlContext.cpp` and `CurlRequest.cpp` contain no UWP conditional and are only participants: `PageUWP.cpp:1273-1275` mutates the process-global `CurlContext::singleton().sslHandle()`, which task 4.11 replaces with per-session policy.
24. `Source/WebCore/platform/graphics/texmap/BitmapTexture.cpp` is the only file outside `platform/uwp` that includes UWP headers; any move of `TextureMapperFrameStatsUWP.h` or `SkiaGPUContextUWP.h` must preserve that include path.

## Apps, diagnostics, tests and tooling inventory

Read-only inventory measured 2026-10-05 (task 1.3).

### Tools/MiniBrowser/uwp

`MiniBrowserUWP.cpp` (1340 lines) is one WRL application class plus curl worker and temporary diagnostics. Entry point `extern "C" void __stdcall WKMiniBrowserStartup()` 1302 (linked via `/ENTRY:WKMiniBrowserStartup`, `CMakeLists.txt:14`), which initializes COM, opens `LocalState\minibrowser.log`, installs `logNativeFault` through `AddVectoredExceptionHandler` from `kernelbase.dll` and starts `MiniBrowserApp`. `class MiniBrowserApp` 331 with `InspectableClass(L"WebKitWebView.MiniBrowser.App", BaseTrust)` 332 and the full activation handler set 355-376. Temporary enums `BlockedTemplateTestStage` 36 (`NotStarted = 0`, `PackagedPagePending = 1`, `OverridePagePending = 2`, `Finished = 3`) and `ComponentDiagnosticStage` 51 (`NotStarted = 0`, `IndexPending = 1`, `SecondPending = 2`, `BackPending = 3`, `VetoPending = 4`, `Finished = 5`); `BlockedTemplateSource` 72 is a real diagnostic result code (`EmbeddedFallback = 0`, `PackagedFile = 1`, `LocalStateFile = 2`, `ExplicitPath = 3`). Curl worker types `FetchRequest` 398 and `FetchResult` 419 with `fetchThreadProc` 472, `fetchRemote` 793, `consumeFetchResult` 719, `s_curlInitOnce` 459/1299. Blocked-page path `setBlockedPageTemplate` 524, `readUtf8File` 531, `fillToken` 546, `loadBlockedTemplate` 555, `showBlockedPage` 640. Temporary component diagnostic `runComponentDiagnostic` 1073 and `tickComponentDiagnostic` 1185, with `RoActivateInstance(L"WebKit.WebKitWebView")` 1076 and subscriptions 1147-1167. UI is an inline XAML string at 948-953 resolved through `FindName` 383; no `MainPage.xaml` exists yet. Tick-driven constants 87-96 (`kBlockedTemplateTestMaxTicks = 1800`, `kComponentSecondNavigationTick = 600`, `kComponentBackNavigationTick = 1200`, `kComponentVetoNavigationTick = 1800`, `kComponentDiagnosticFinishTick = 2400`, `kNativeNavigationSampleCount = 8`, `kNativeNavigationSampleInterval = 30`, `kNativeNavigationMaxTicks = 600`, `kExpectedBlockedTemplateTestCount = 2`, `kFirstBlockedTemplateTestCount = 1`) assume 60 Hz. Curl options 483-485 (`CURLOPT_MAXREDIRS 5L`, `CURLOPT_CONNECTTIMEOUT 10L`, `CURLOPT_TIMEOUT 25L`) and `kFetchProbeCapBytes` 451.

`AppxManifest.xml` pins identity `WebKitWebView.MiniBrowser`, publisher `CN=WebKitWebView`, version `1.0.0.77`, `ProcessorArchitecture="arm"`, `Application Id="App"`, EntryPoint `WebKitWebView.MiniBrowser.App`, an `ActivatableClass` for `WebKit.WebKitWebView` 15 and `internetClient`. `blocked.html` carries the `{{TITLE}} {{INTRO}} {{URL}} {{REASON}} {{TECHNICAL}} {{PROCEED}}` tokens; `cacert.pem` is the packaged Mozilla bundle (188900 bytes). `README.md` 70 lines documents build and device commands.

### Diagnostics

`Tools/uwp/RuntimeSmoke/`: `RuntimeSmoke.cpp` exports `WKRunRuntimeSmoke()` 163 with `enum RuntimeSmokeFailure` bits 1/2/4/8/16/32/64/128 (documented in `README.md` 24-33) and CRT canary constants 15-24 that are the assertion, not tuning; `SmokeHost.cpp` entry `WKSmokeStartup()` 338, `SmokeView` 254 / `SmokeViewSource` 329, JSC probes resolved at 168-173 and `WKRenderFirstPageUWP` at 198; `CMakeLists.txt` builds library `WebKitRuntimeSmoke` plus host `WebKitRuntimeSmokeHost` and compiles the presenter into the host; `AppxManifest.xml` identity `WebKitWebView.RuntimeSmoke` `1.0.0.10`; `stage-app.py` is the shared staging CLI (see command surfaces).

`Tools/uwp/FpuProbe/`: `FpuProbe.cpp` entry `WKFpuProbeStartup()` 262, `FpuView` 203 / `FpuViewSource` 253, inline `fmrx %0, fpscr` 67, expected values 32-42 and 94-95 (`kLongjmpReturnValue = 7`, `kCrtProbeReturn = 11`, `kIntProbeReturn = 9`, `kFpscrTrapEnableMask = 0x1f00`), `installFaultHandler` 161 defined but skipped at 233; `CrtFrameProbe.S` exports `wkProbeSetjmpSp` 17 and `wkProbeSetjmpR11` 18; `AppxManifest.xml` identity `WebKitWebView.FpuProbe` `1.0.0.11` with the correct EntryPoint.

`Tools/uwp/TlsSmoke/`: `TlsSmoke.cpp` entry `WKTlsStartup()` 346, `TlsView` 243 / `TlsViewSource` 337, `enum TlsProbeStatus` 39, three ordered phases (plain HTTP 223, insecure HTTPS 229, verified HTTPS 235) with enter/exit markers 172-220, probe URLs 70-71; `CMakeLists.txt` hard-codes vcpkg and SDK `bcrypt.lib` paths; `AppxManifest.xml` identity `WebKitWebView.TlsSmoke` `1.0.0.8`.

### Tests

`Tools/uwp/NavigationTests/server.py` (230 lines) serves `/`, `/index.html`, `/assets/{style.css,first.js,second.js,image.png,font.ttf}`, `/second.html`, `/pushstate.html`, `/replacestate.html`, `/redirect`, `/redirect-loop`, `/missing.html`, `/latin1.html`, `/gzip.html`, `/download`, `/api/data`, `/cors/{allow,deny}`, `/cookies/{set,echo}`, `/policy.html` (CSP `default-src 'self'; script-src 'none'; img-src 'none'`), `/cross-origin.html`, `/slow.html`, `/delay`, `/large`, `/requests`, and answers CORS preflight for `/cors/allow` only. Defaults `DEFAULT_SERVER_HOST = '127.0.0.1'` 22, `DEFAULT_SERVER_PORT = 8000` 23, `DEFAULT_DELAY_MS = 200`, `MAX_DELAY_MS = 5000`, `MAX_LARGE_RESPONSE_BYTES = 4*1024*1024`, `DEFAULT_LARGE_RESPONSE_BYTES = 1024*1024`, `DELAYED_IMAGE_MS = 3000`. There is no result/reporting protocol: `log_message` is suppressed so cookies never leak and requested paths are exposed as JSON at `/requests`. `test_server.py` (136 lines) has eight host tests and imports `from server import ...` 9, so it depends on both files sharing a directory. `__pycache__/` artifacts are present and not ignored by `Tools/uwp/.gitignore`.

### Tooling

`Tools/uwp/toolchain/`: `arm-crtstart.c` (exports `mainCRTStartup` 27, `wmainCRTStartup` 30, `WinMainCRTStartup` 33, `wWinMainCRTStartup` 39), `crmain.c`, `crwmain.c`, `crwinmain.c`, `crwwinmain.c`, `crdllmain.c`, `crrt.cpp` (553 lines; ARM32 Store CRT reimplementation with `__security_cookie` 50, `atexit` 102, POSIX aliases from 204, `_Cnd_timedwait` 390, `.CRT$XCA`/`$XCZ` sentinels 446-447), `crrtti.c` (`__RTDynamicCast` 17), `crtvft.S` (`??_7type_info@@6B@`), `crdiv.S` (`__rt_sdiv64`/`__rt_udiv64`/`__rt_sdiv`/`__rt_udiv`), `crtjmp.S` (`__intrinsic_setjmp`, `_setjmp`), `crtls.cpp`, `wkstl.cpp` (892 lines of STL shims), `link-shim.c` (host x64 shim with hard-coded `lld-link.exe` path 20), `clang-cl-arm.sh` and `link.sh` (wrappers; live copies under a developer directory), `uwp-desktop-apis.h` (forced include), `arm-virtual-call-probe.cpp`, `runtime-abi-probe.cpp`. `Tools/uwp/lib/`: `arm-crtstart.lib`, `clang_rt.builtins-arm.lib`, `m.lib`. `Tools/uwp/ports/`: cairo, fontconfig, harfbuzz, libxml2, openssl, pthreads overlay ports, each with its own `portfile.cmake` UWP branching. Top level: `arm32-uwp-env.ps1`, `build-runtime.ps1`, `audit-dlls.py`, `audit-source-docs.py`, `device-portal.py`, `.gitignore` (`*.obj` only), plus `crrt.obj` and three `__pycache__` files as stray intermediates.

### Command surfaces

| Surface | Command | Notes |
|---|---|---|
| Engine configure/build | `cmake -S . -B build-arm-uwp-skia -G Ninja -DPORT=Win -DWK_UWP=ON ...` then `ninja -C build-arm-uwp-skia -j 8 WebCore` | `SKIA-PORT.md` 12/15 |
| App configure/build | `cmake -S Tools/MiniBrowser/uwp -B build-arm-minibrowser ...` then `cmake --build build-arm-minibrowser --parallel 8` | `MiniBrowser/uwp/README.md` 20-21 |
| Diagnostic builds | `cmake -S Tools/uwp/{RuntimeSmoke,FpuProbe,TlsSmoke} -B build-arm-... ` | per-app README |
| Component build | `Tools/uwp/WinRTComponent/CMakeLists.txt` target `WebKitWebView` | no documented command line |
| Native env | dot-source `Tools/uwp/arm32-uwp-env.ps1` | sets `VCPKG_ROOT`, `INCLUDE`, `LIB` |
| Runtime library build | `pwsh -File Tools/uwp/build-runtime.ps1` | rebuilds `lib/arm-crtstart.lib` |
| Toolchain discovery | `powershell -File Tools/uwp/build/discover-arm32-toolchain.ps1 [-VsPath] [-SdkVersion] [-Json] [-Quiet]` | task 2.1 |
| Staging | `python Tools/uwp/RuntimeSmoke/stage-app.py --build --host [--no-probe] [--manifest] --engine --prefix --stage [--override]* --reader` | imports `audit-dlls` as a module |
| Dependency audit | `python Tools/uwp/audit-dlls.py <roots> [--search] [--reader] [--output]` | fails on non-ARMNT binaries |
| Device portal | `python Tools/uwp/device-portal.py --url https://<host> [--insecure] [--pin] get|post|install|launch ...` | 30 s handshake, 180 s deploy timeouts |
| Source audit | `python Tools/uwp/audit-source-docs.py` | needs C++/CX syntax rules |
| Fixture server | `python Tools/uwp/NavigationTests/server.py --host 0.0.0.0 --port 8000` | second instance on 8001 for CORS |
| Fixture host tests | `python -m unittest discover -s Tools/uwp/NavigationTests -p test_server.py -v` | |
| Packaging | `makeappx.exe pack /d <stage> /p <out.appx> /o` | literal text in READMEs only, not scripted |
| Signing / install / launch | manual `signtool`, device portal `install`/`launch` | no script; `CN=WebKitWebView` dev certificate |
| MIDLRT and `/ZW` | documented manual `midlrt.exe` then `cl.exe /ZW` recipes | `WinRTComponent/README.md` |
| vcpkg | `vcpkg install <port> --overlay-triplets=WebKitLibraries/triplets --triplet arm-uwp-webkit` | |

Produced entry/activation symbols: `/ENTRY:WKMiniBrowserStartup`, `/ENTRY:WKSmokeStartup`, `/ENTRY:WKFpuProbeStartup`, `/ENTRY:WKTlsStartup`, `/EXPORT:DllGetActivationFactory`, `/EXPORT:DllCanUnloadNow`, native payload `WKRunRuntimeSmoke`, assembly exports `wkProbeSetjmpSp`, `wkProbeSetjmpR11`, `__rt_sdiv64`, `__rt_udiv64`, `__rt_sdiv`, `__rt_udiv`, `__intrinsic_setjmp`, `_setjmp`, `??_7type_info@@6B@`, CRT startup exports listed above.

### Values that must not survive

Developer/SDK paths are enumerated in collision 12. LAN addresses and ports: `MiniBrowserUWP.cpp:108-110` fixture URLs `http://192.168.1.69:8002/{index.html,index.html?uwp-suite=1,second.html}`, the launch-time default suite navigation 1067 (commented as temporary), closed-port fixtures `http://192.168.1.69:9/{first,second}` 603/621, the `"uwp-suite=1"` substring gate 733 with `kComponentFixtureSuiteURL` 109 left unused, and `RuntimeSmoke/README.md` 44-46 portal URL. Package identity and version are hard-coded in all five manifests (MiniBrowser `1.0.0.77`, RuntimeSmoke `1.0.0.10`, FpuProbe `1.0.0.11`, TlsSmoke `1.0.0.8`, all `CN=WebKitWebView`, `MinVersion 10.0.15063.0`, `MaxVersionTested 10.0.15254.0`, `Microsoft.VCLibs.140.00` dependency). Tick budgets assume 60 Hz and must become time-based or move into the runner.

Values that are native payload semantics and must be preserved: the `WKRunRuntimeSmoke` failure bitmask and every RuntimeSmoke CRT canary constant; all FpuProbe constants plus `CrtFrameProbe.S` frame-argument variants and `wkProbe*` names; TlsSmoke enter/exit marker naming and phase ordering; `Tools/uwp/lib/*.lib` and `toolchain/*` CRT/ABI symbol names; fixture scenario bodies, `PASS` markers, the `m9=fixture` cookie value and the CSP header, which move to `fixtures/` rather than being rewritten.

`stage-app.py` resolves `cacert.pem` and `blocked.html` relative to the manifest directory, so moving the manifest while relocating the HTML into `assets/` breaks staging unless both the lookup and the staging list change together (tasks 8.3, 10.4).

### UWP-conditioned sources outside the three main directories

Two spellings exist: `PLATFORM(UWP)`, which expands from the toolchain's `WTF_PLATFORM_UWP=1`, and the CMake variable `WK_UWP`. Retain upstream file and symbol names in all of these.

WTF: `wtf/PlatformUse.h` 237 (generic event loop), `wtf/WindowsExtras.h` 42 (registry/window stubs), `wtf/RandomDevice.cpp` 40/113 (BCryptGenRandom), `wtf/CurrentTime.cpp` 171/175, `wtf/StackTrace.h` 130/150, `wtf/win/{DbgHelperWin.h:31,57, DbgHelperWin.cpp:34,75, FileSystemWin.cpp:106, GDIObject.h:118,130, MemoryPressureHandlerWin.cpp:38,58,73,90,137, SignalsWin.cpp:155, ThreadingWin.cpp:128}`, `wtf/simde/arm/neon.h` 105340 (vendored third-party CPU-intrinsic selection, retained per 7.3 review; no OS interaction). PAL: `WebCore/PAL/pal/system/win/SoundWin.cpp` 35. JSC: `jit/ExecutableAllocator.cpp` 412. bmalloc: `bmalloc/VMAllocate.h` 420, `mimalloc/src/prim/windows/prim.c` 95.

WebCore graphics: `platform/graphics/{Font.h:39,253,392, FontCache.h:294, FontCustomPlatformData.h:38,77,107, FontPlatformData.h (11 sites), opentype/FontMemoryResource.h:46, opentype/OpenTypeUtilities.cpp:272, skia/FontCacheSkia.cpp:47,66,182, texmap/BitmapTexture.cpp (13 sites), texmap/BitmapTexture.h:145,165, cairo/CairoUtilities.cpp:46,338, cairo/GraphicsContextCairo.cpp:49, cairo/GraphicsContextCairo.h:45, win/cairo/GraphicsContextWinCairo.cpp:36,42,82, win/cairo/ImageAdapterWinCairo.cpp:32,40,65, win/DIBPixelData.cpp:43,106, win/GraphicsContextWin.cpp:39,51, win/IconWin.cpp:42,53,92, win/SystemFontDatabaseWin.cpp:40}`.

WebCore platform/win: `ClipboardUtilitiesWin.cpp`, `CursorWin.cpp`, `DragDataWin.cpp`, `GDIUtilities.{cpp,h}`, `HWndDC.h`, `KeyEventWin.cpp`, `PasteboardWin.cpp` (24 sites, includes `../uwp/ClipboardUWP.h`), `PlatformMouseEventWin.cpp`, `PlatformScreenWin.cpp`, `SharedMemoryWin.cpp`, `SystemInfo.cpp` (wide loader spelling), `WCDataObject.{cpp,h}`, `WheelEventWin.cpp`, `WindowMessageBroadcaster.{cpp,h}`, `WindowsKeyNames.cpp`. All reviewed in 7.3 (`artifact/os-interaction-review.md`): engine-internal WRL use retained in `platform/uwp/` only, everything else Win32-substitution or fail-closed stub with no projection.

WebCore other: `platform/DragImage.h`, `platform/Pasteboard.h`, `platform/MainThreadSharedTimer.{h,cpp}`, `page/EventHandler.cpp` 3931, `page/win/DragControllerWin.cpp` 58, `accessibility/AXObjectCache.h` 123, `accessibility/win/AXObjectCacheWin.cpp` 72, `rendering/RenderLayerBacking.cpp` 1838/4018, `rendering/RenderLayerCompositor.cpp` 4140, `platform/network/win/NetworkStateNotifierWin.cpp` 72.

Third-party conditionals to keep patched and not rename: `ThirdParty/ANGLE/src/libANGLE/ProgramExecutable.cpp` 2657/2773 (`WTF_PLATFORM_UWP && _M_ARM && __clang__`), `ThirdParty/skia/CMakeLists.txt`, `ThirdParty/ANGLE/CMakeLists.txt` and `PlatformWin.cmake`. CMake consumers of `WK_UWP`: `Source/cmake/OptionsWin.cmake`, `Source/cmake/WebKitMacros.cmake` 656, `Source/WTF/wtf/PlatformWin.cmake` 45, `Source/WebCore/PlatformWin.cmake` 15/261, `Source/WebCore/platform/Curl.cmake` 62, `Source/JavaScriptCore/PlatformWin.cmake` 1.

## Per-slice record format

For each touched symbol/file record old path/name, new path/name, language/owner,
all include/export/project/script/doc consumers, compatibility rule, task ID,
verification command/result, and device evidence when applicable. Mark retained
and generated families explicitly. No area is complete until its manifest has
zero unaccounted authored UWP symbols; remaining legacy compatibility is listed.
