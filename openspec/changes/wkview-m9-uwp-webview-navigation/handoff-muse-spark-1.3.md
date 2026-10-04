# Handoff to Muse Spark 1.3

The owner explicitly stopped the current agent and requested this handoff.
Continue from here; do not assume historical hypotheses are confirmed.

## Goal and current state

Finish M9 UWP WebKitWebView. The immediate next task is proper engine history:
retain WebCore HistoryItems and connect MiniBrowser Back/Forward/Reload to that
history instead of its shell URL vector. **History implementation has not begun.**
The last agent inspected the relevant interfaces and found additional empty-client
hooks that must be replaced. No history client or history bridge edits were made.

Owner requirements: a durable root-cause fix, not feature disabling or fixture
workarounds. Keep MiniBrowser Skia Ganesh/ANGLE-D3D11. Commit/push only on request.

## Verified work

### PNG crash resolved and visually confirmed

- Exception `0xC0000028` is **STATUS_BAD_STACK**, not a floating-point exception.
  SDK STATUS_FLOAT_INVALID_OPERATION is `0xC0000090`. Much of the earlier
  `handoff-2026-10-03.md` historical narrative incorrectly interpreted this code.
- Original clang 19 code passes r11 as __intrinsic_setjmp's hidden frame argument.
- FpuProbe 1.0.0.10 explicitly calls the same original VCRUNTIME140 exports:
  caller SP as frame => longjmp succeeds; r11 as frame => crash.
- `Tools/uwp/toolchain/crtjmp.S` now contains a **leaf SP argument adapter** that
  tail-calls `__imp___intrinsic_setjmp`. It does not implement longjmp. The original
  CRT performs buffer saving and Windows unwind/restore.
- FpuProbe 1.0.0.11 completes integer and actual noinline FP calculations through
  this adapter. Modified locals are volatile so their post-jump values are defined.
- Browser libraries were relinked: WebCore, JavaScriptCore, libpng and FreeType.
- MiniBrowser 1.0.0.62 stopped crashing; owner confirmed the green PNG is visible.
- Preserve the toolchain's existing `/FORCE:MULTIPLE` first-definition-wins policy
  with arm-crtstart.lib before msvcurt.lib: the CRT IAT import member also defines
  a public setjmp thunk. Presence of CRT jmp imports is now expected, not a failure.

### MiniBrowser 1.0.0.63 device matrix completed

Authoritative evidence: `device-v63-acceptance.md` in this directory.

Passed:
- Downloaded font actually selected: customPlatformData present, interstitial=0,
  glyph lookup succeeds. M9 task **4.2 is checked complete**.
- Redirect final document; HTML 404 document; ISO-8859-1 decoding.
- CSP inline-script block and image block.
- Two-origin CORS allowed request and blocked response.
- Pending three-second image cancellation on replacement and Stop. Document stays
  stable 3500 ms later, and no delayed-image data/completion callbacks are logged.

Failed / untested:
- Engine history back-count=0, goBack=false. Forward was not reached.
- HTTPS/mixed-content/invalid-cert acceptance is outstanding; separate OpenSSL
  ARM32 TlsSmoke crash remains open.
- Cancellation teardown, main-document cancellation, navigation IDs/terminal
  ordering, budgets and lifecycle are not proven by this limited matrix.

`fixture-suite-completed=1` means the diagnostic runner finished, not all passed.
The log is append-only: an earlier curl error 28 occurred when the owner stopped
the servers. The owner subsequently restarted both and the matrix completed.

## Exact history findings to use next

1. `Source/WebCore/platform/uwp/PageUWP.cpp`, `WKCreatePageUWP`, constructs
   `pageConfigurationWithEmptyClients`; replace `configuration.backForwardClient`.
2. `Source/WebCore/loader/EmptyClients.cpp:122`, EmptyBackForwardClient discards
   addItem/setChildItem/goToItem, returns null for lookup and zero for counts.
3. `Source/WebCore/history/BackForwardClient.h` requires:
   - addItem(Ref<HistoryItem>&&)
   - setChildItem(BackForwardFrameItemIdentifier, Ref<HistoryItem>&&)
   - goToItem(HistoryItem&)
   - allItems(FrameIdentifier), itemAtIndex(int, FrameIdentifier)
   - backListCount(), forwardListCount(), containsItem(), close().
   Retain HistoryItems, handle forward truncation on new navigation, and keep
   frame-aware lookup/child-item updates correct. Do not store just URL strings.
4. `HistoryItem.h` exposes itemID/frameItemID/frameID, children(), childItemWithFrameID,
   setChildItem, URL/title/state/scroll metadata. HistoryController.cpp:637-638
   calls setChildItem with the parent frameItemID.
5. `BackForwardController.cpp` goBack/goForward obtains items and calls Page::goToItem;
   setCurrentItem calls client.goToItem. Its itemAtIndex/allItems default to the
   main frame ID. BackForwardController owns the client by Ref.
6. **Additional navigation blocker:** EmptyFrameLoaderClient::shouldGoToHistoryItem
   returns ShouldGoToHistoryItem::No (`EmptyClients.cpp:1060`). Its declaration is
   final (`EmptyFrameLoaderClient.h:164`), so make the necessary empty-client hook
   overridable and implement browser policy in PageFrameLoaderUWP.
7. Other empty history hooks to investigate before claiming JS history works:
   - dispatchGoToBackForwardItemAtIndex / dispatchEnqueueHistoryTraversalDelta are
     no-ops (EmptyClients.cpp:1075/1079), declarations final.
   - createHistoryItemTree returns null (EmptyClients.cpp:1149), declaration final.
   - supportsAsyncShouldGoToHistoryItem returns false; async hook asserts.
   Determine which hooks apply to this single-process embedding.
8. `Source/WebKit/UIProcess/API/uwp/WebKitWebViewUWP.{h,cpp}` dynamically binds native
   bridge exports from PageUWP.h using BIND/GetProcAddress. It currently exposes
   navigateRemote/pageState, not Back/Forward/Reload/Stop navigation APIs.
9. `Tools/MiniBrowser/uwp/MiniBrowserUWP.cpp` toolbar Back pops m_history and navigates
   that URL; Reload navigates m_history.back(). Add Forward and wire actual engine
   navigation actions/state. Preserve local demo behavior deliberately; do not
   claim shell-vector navigation validates engine history.

## Device and fixture setup

- Repo: `C:/Users/Longi/Workspace/WebKit`.
- Device Portal: `https://192.168.1.52`, insecure/self-signed flag required.
- PC fixture address: `192.168.1.69`. Do not use disconnected `192.168.1.45`.
- Installed MiniBrowser: `WebKitWebView.MiniBrowser_1.0.0.63_arm__2932e6rj4m41p`.
- App ID: `WebKitWebView.MiniBrowser_2932e6rj4m41p!App`.
- Installed FpuProbe: 1.0.0.11.
- Signing certificate thumbprint: `18FD65CF5E103CF96F04C2A451DC49C94788A40D`.
- Owner currently has two fixture terminals running:

```powershell
python Tools/uwp/NavigationTests/server.py --host 0.0.0.0 --port 8002
python Tools/uwp/NavigationTests/server.py --host 0.0.0.0 --port 8003
```

Temporary shell startup is `http://192.168.1.69:8002/index.html?uwp-suite=1`.
This opts into the PageUWP asynchronous diagnostic suite, which returns to
index.html afterward. Restore about:demo and remove temporary diagnostic code
before final acceptance. The suite's Back/Forward checks will run once history
is available; add Reload, same-document/pushState, branch truncation and scroll
restoration coverage rather than assuming existing checks cover them.

## Build state — important

- Engine build dir: `build-arm-uwp-skia`; shell: `build-arm-minibrowser`.
- Source `. ./Tools/uwp/arm32-uwp-env.ps1` before builds; use eight parallel jobs.
- Default all-target engine build has unrelated ARM gtest SEH errors. Target WebCore
  / JavaScriptCore explicitly.
- FreeType's resource hang was caused by CMake's LLVM preprocessing path. Pin
  CMAKE_RC_COMPILER_INIT plus SDK 22621 x64 rc.exe to compile original RC input.
  Normal FreeType Ninja resource compile/link was verified after this change.
- **Do not FORCE the existing RC compiler cache entry.** That initially reset the
  engine CMake cache, losing cross-toolchain settings. FORCE has been removed.
- Engine configuration was restored from `Tools/uwp/SKIA-PORT.md`, with explicit
  llvm-lib (CMAKE_AR) / lld-link and restored initial compiler/linker flags.
  Normal Ninja still planned **8316 steps** after this recovery. A full consistent
  incremental build configuration must be reconciled before final acceptance.
- For 1.0.0.63 only PageUWP.cpp was compiled using the generated Ninja compdb command
  and WebCore relinked against existing objects. The generated rule name is
  CXX_COMPILER__WebCore_unscanned_Release. Select the exact basename /PageUWP.cpp,
  not endswith(PageUWP.cpp), which also matches FirstPageUWP.cpp.
- Relink helper: `C:/Users/Longi/AppData/Local/Temp/opencode/relink-webcore.py`.
  It expands the generated link command and uses a temporary response file to
  avoid Windows command-length limits. This is a diagnostic incremental build,
  not evidence of a full clean acceptance build.
- WEBKIT_FRAMEWORK now adds a runtime archive LINK_DEPENDS under WK_UWP so JSC and
  WebCore relink when arm-crtstart.lib changes.
- Existing old libpng/FreeType buildtree linker flags needed `/FORCE:MULTIPLE`
  refreshed. FreeType's old flags were reset with `-U CMAKE_SHARED_LINKER_FLAGS`.
- Stage with all existing overrides **including SQLite and libpng**. The complete
  stage recipe in the original handoff lacks the newly added libpng override;
  additionally pass `--override C:/vcpkg/buildtrees/libpng/arm-uwp-webkit-rel`.
- Keep the Skia staging guard. Verify actual installed package version before
  launch: HTTP 202 only means installation accepted, not finished.

## Diagnostics and safety fixes already in source

- MiniBrowser VEH ignores debugger notifications 0x40010006/0x4001000a.
- PageUWP settle probes and suite callbacks check an expiring weak lifetime token
  before dereferencing PageHostUWP; loadGeneration rejects replaced-document probes.
- Font probe uses renderer Style::ComputedStyle + primaryFont custom platform data.
- CSP fixture has a visible PASS marker changed to FAIL only by the blocked script.
- Six host fixture tests passed via:
  `python -m unittest discover -s Tools/uwp/NavigationTests -p test_server.py -v`.

## Todo list for Muse Spark 1.3

- [x] Implement retained, frame-aware BackForwardClient and install per page.
- [x] Replace the empty frame-loader history policy/traversal hooks required by
      this embedding; handle history tree creation as needed.
- [x] Add native bridge actions/state for Back/Forward/Reload (and Stop), preserving
      thread ownership and existing ABI compatibility.
- [x] Bind wrapper actions and wire toolbar to engine history; remove shell URL
      history from remote navigation and update address/history availability.
- [x] Extend device suite for Reload, same-document/pushState, forward truncation,
      and scroll restoration; distinguish blocked navigation from accepted traversal.
      (Anchor same-document + Back, Reload, truncation, scroll restoration done;
      `pushState`/`replaceState` JS-API coverage still open.)
- [x] Build matching binaries, bump/sign/install package, verify installed identity,
      rerun the matrix with both servers available, and preserve exact results.
      (MiniBrowser 1.0.0.64; see `device-v64-history.md`.)
- [ ] Reconcile engine build cache / full clean build before final acceptance.
- [ ] Update tasks.md and evidence; do not check entire tasks from partial tests.
      (3.3 checked with stated limits; 4.4 note appended.)
- [ ] Continue remaining M9 security/cancellation/budgets/session/lifecycle checks.
- [ ] Implement native WinRT/WinMD/XAML activation and C# MVVM shell (M9 sections
      5–6); C# UWP/.NET Native workloads remain a known prerequisite.
- [ ] Restore about:demo and strip temporary diagnostics before acceptance.
- [ ] Resume separate OpenSSL/TlsSmoke issue and RTTI-stub audit as tracked debt.
- [ ] Attribute the three `navigation-error: 1` teardown reports (log failing URL
      in `dispatchDidFailLoad`) before treating cancellation as fully closed.

## Change ownership

There are extensive uncommitted modifications and untracked files from earlier
sessions. Inspect git status/diff before changing them; do not discard them.
Nothing was committed or pushed. Relevant files include PageUWP.cpp/h,
EmptyFrameLoaderClient.h, LoaderStrategyUWP.*, WebKitWebViewUWP.*, MiniBrowserUWP.cpp,
AppxManifest.xml, WebKitMacros.cmake, ARM toolchain, build-runtime.ps1,
arm-crtstart.lib, FpuProbe/, NavigationTests/, and this openspec change directory.
