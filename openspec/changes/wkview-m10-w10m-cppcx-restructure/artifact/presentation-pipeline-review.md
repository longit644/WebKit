# Systematic presentation review — 2026-10-12

Scope: v97 still flashes when focusing the address field, exposing the app after
Action Center, or hiding/showing the navigation bar. The three-dot menu does not
flash. The owner requested a pipeline review rather than further symptom patches.

## Evidence and corrections to earlier claims

- v97's saved engine trace (`%TEMP%/opencode/m11-v97-keyboard-before.txt`) shows
  viewport height cycling `611 -> 615 -> 322 -> 318 -> 611`, with surface dimensions
  changing accordingly. Resize synchronization reaches `ready=1`; the earlier
  rounding stall is no longer sufficient to explain the remaining flash.
- Menu display changes an XAML overlay; it does not take the same keyboard/focus
  or native exposure path. Darkening while editing is intentional owner UX.
- `EnsuredFocusedElementInView=true` says the application has already handled
  focus visibility. Microsoft explicitly states that false asks Windows to do
  the work too. The v93 change to false was incorrect and is restored.
- `presentation-first-frame` previously followed a successful operation result,
  which included deferred surface synchronization. It was not reliable proof of
  a painted frame. The owner-confirmed red v96 framebuffer is stronger evidence.

## Confirmed source/provenance defects

### 1. Present precedes paint in the resize path

`TextureMapperCompositorW10M::synchronizeSurface()` called `eglSwapBuffers` before
`render()` cleared and painted its target. After a flip or native resize, that
buffer is not guaranteed to contain this frame's complete image. Changing the
rounding tolerance or debouncing layout does not establish framebuffer validity.

ANGLE's `DisplayD3D::waitNative()` explicitly calls
`SurfaceD3D::checkForOutOfDateSwapChain()`. `eglWaitNative(EGL_CORE_NATIVE_ENGINE)`
is the native synchronization operation; a client dummy swap is not necessary.
The corrected client frame must have one painted swap, after synchronization and
successful paint. ANGLE's internal content-preserving resize behavior remains
separate and must be checked by actual device transitions.

### 2. The negotiated route omits the page rendering update

The legacy `WebKitWebViewW10M::advanceFrame()` calls `tick`, then `needsPaint`, then
`repaint`. `WKTickPageW10M` makes the GPU context current and calls
`Page::updateRendering()` plus `finalizeRenderingUpdate({})`.

The new negotiated route queried dirty state and rendered directly without that
page tick. Process `RunLoop::cycle()` is not a substitute for page rendering
update/finalization. The adapter must restore the proven ordering.

### 3. The fresh ANGLE build disabled the ARM workaround

`ProgramExecutable.cpp` guarded both scalar/vector and matrix direct-virtual-call
workarounds with `WTF_PLATFORM_UWP`. Its real compile command, obtained from the
native Ninja compilation database, defines `WTF_PLATFORM_W10M=1`, not UWP. The
workaround is therefore absent from the fresh DLL.

v95's dump has `libGLESv2+0x17718B`, with `r1` holding a code address used as a
uniform location. The correctly relocated disassembly is at `0x1017718A`
(preferred image base `0x10000000` plus RVA `0x17718A`), not the earlier incorrectly
read `0x100177xx` region. Its indexed load uses the corrupted location. Source
repair is the renamed guard; pinning v33 only isolated the build regression.

### 4. Baseline precedence silently mixes engine generations

v97's identity records baseline JavaScriptCore hash `73DD39610215B283...` beside
fresh WebCore, although the configured JavaScriptCore hash is
`1619EC7D67542CDE...`. The architecture/import audit did not reject this mismatch.

The configured build must own WebCore, JavaScriptCore, libEGL and libGLESv2.
Dependency overrides may still supply repaired FreeType/Fontconfig/ICU DLLs.
Staging must assert hash equality for all four engine outputs.

### 5. Exposure and reentrant teardown lack explicit frame ownership

The control handled SizeChanged synchronously while ANGLE also listened to the
same event, but had no visibility/activation revalidation path. Native geometry
work should execute at the next SDK frame, after panel listeners complete.

`Close()` also immediately detached GPU resources even when requested within a
native frame callback. A native-call depth guard cannot protect the frame if
teardown ignores it. Close must latch rejection and defer native detach until
the frame/callback stack unwinds. Errors must remain observable.

## Frame invariants used for the repair

1. One coherent configured engine output set is packaged.
2. Geometry is installed at an SDK frame boundary; a count of sightings is not
   a frame-stability guarantee.
3. Native surface synchronization never issues a client unpainted swap.
4. Page update/finalization precedes dirty inspection and tile paint/composition.
5. GL paint failure does not swap away the prior front buffer.
6. Deferred/idle work does not clear dirtiness or count as presentation success.
7. Exposure requests revalidation of the existing attachment; only a scale
   change or explicit detach changes attachment ownership.
8. Close cannot destroy resources used by a current native frame stack.

## v98 build verification

The corrective candidate is v98. The core/ANGLE build and matching component/
compiled-XAML shell build pass. New dependency-selection and deferred-frame checks
are required alongside the existing host tests. Device acceptance remains open:
startup pixels, address keyboard up/down, Action Center return, navigation bar
hide/show, and menu must be checked independently. A successful swap/readback
does not by itself prove absence of visible flashing in Windows composition.

The undeployed shell-splitting/frozen-viewport proposal was removed, and the v97
resize-sighting debounce was removed. The owner's dark overlay is retained.

## v98 interaction result: UX still fails

v98 was installed and initialized successfully with the freshly rebuilt ANGLE
and coherent engine output hashes. Startup painted samples were `ff0000ff`.
The owner then explicitly reported continued flashing. Raw interaction reports
are `%TEMP%/opencode/m11-v98-interaction.txt` and
`%TEMP%/opencode/m11-v98-interaction-engine.txt`.

- Editing/focus follows each actual touch and dismissal; no rapid editing-state
  oscillation was observed inside a single keyboard-show transition.
- Keyboard transitions synchronize `ready=1` and render the resulting extents.
- Action Center deactivation also changes the visible viewport from height 611
  to 659, and reactivation restores it. Therefore the common resize path is now
  observed for both keyboard and Action Center, rather than merely inferred.
- A single center pixel is not a whole-image black-frame detector: it can sample
  the diagnostic page's black text. No assertion of whole-buffer blackness is
  made from that one pixel.

### Additional confirmed native publication path

`SwapChain11::resetOffscreenColorBuffer` copies the overlapping portion of the
old offscreen texture into the resized one, then immediately calls `swapRect`.
At that point `mWidth`/`mHeight` still refer to the old dimensions; they are only
updated when `resetOffscreenBuffers` returns. `copyOffscreenToBackbuffer` uses
those old dimensions for quad normalization and viewport setup. On growth,
newly exposed rows have not been painted by the application. That implicit
Present is outside the WebCore painted-swap log and violates the same complete-
frame publication invariant even after the client dummy swap was removed.

v99 scopes the repair to W10M: retain the pixel-preservation copy in GPU memory,
but publish only the subsequent complete application-painted EGL swap. This
is a native buffer-lifecycle change, not an overlay/fade or debounce change.

## v99 owner acceptance: flashing resolved, transition lag remains

Package `WebKitProbe.MiniBrowser_1.0.0.99_arm__2932e6rj4m41p` installed with
`Code=0`, launched on the Lumia 950 XL / Mobile
`15254.603.armfre.feature2_rs3svc.200106-1633`, and remained running. v99 reused
the v98 shell, retaining the dark editing overlay and diagnostic red HostHtml;
its additional native change is the internal ANGLE resize-publication repair.
The owner confirmed that flashing was resolved, then separately reported lag
when the address bar changes state or moves with the navigation bar.

Saved reports are `%TEMP%/opencode/m11-v99-accepted-ux.txt` and
`%TEMP%/opencode/m11-v99-accepted-engine.txt`. They contain 139
`gpu-painted-present` records and 138 `gpu-surface-sync ... ready=1` records,
without paint/swap/GL errors or an unresolved surface resize. Crash capture was
enabled; no v99 crash dump was present. These support the owner's flashing
result; they do not establish smooth transition pacing.

Package `%TEMP%/opencode/m11-minibrowser-hosthtml-candidate-v99.appx` has SHA-256
`C2F5F43AD6E77CE9FCF88B025FE4B055DA00C11BE100ED4030FFA19D67F234D5`.
The matching `.identity.json` records actual payload hashes:

| Engine output | v99 SHA-256 |
| --- | --- |
| WebCore.dll | `1DB8B79724EFD6FFD1F72DE7EFAB4E57BD26FA0D9DFEF5336FA343A37CEFF99B` |
| JavaScriptCore.dll | `1619EC7D67542CDE89A287CA24DAFE4297AF8141E6D902080DC281E56FD1581A` |
| libEGL.dll | `D85B22A30930CF41C2ABAB0609DE63201D8861C98BD9DEE81C0E829ECC07F7DE` |
| libGLESv2.dll | `45E2A29BF9BCAA27EC79E69E09461029466823146BB96C4C5C4BFDA04AFF6240` |

## v100 performance comparison: device run, chrome still jumps

The v99 candidate still performed synchronous center-pixel `glReadPixels` on
geometry/exposure frames. Its shell was built with `-LayoutDiagnostics`, whose
snapshots force `UpdateLayout()` on the UI thread. Source inspection also shows
owner-thread diagnostic file writes; their separate cost has not been measured.
Removing readback and forced diagnostic layout provides the next comparison.

The compositor's `m_flushMS`, `m_compositeMS` and `m_swapMS` accumulators hold
milliseconds, but the old `*-us` formatter multiplied them by 1,000,000. v100
corrects this to 1,000; tile counters accumulate seconds and retain their existing
conversion. Divide only the three old stage fields by 1,000 to recover their
actual microseconds. For example, the first saved v99 window represents about
21.54 ms flush, 72.29 ms composite and 7.44 ms swap, not seconds of work. The
composite interval includes readback; it cannot isolate readback cost. These
stage windows also exclude page tick, layout and pre-paint surface synchronization.

v100 has the following recorded configuration:

- Native cache: `WKW10M_HOSTHTML_CANDIDATE:BOOL=ON`,
  `WKW10M_FRAME_READBACK_DIAGNOSTICS:BOOL=OFF`.
- `TextureMapperCompositorW10M` guards diagnostic readback with that option;
  geometry frames still log `gpu-painted-present ... readback=off`.
- Fresh shell identity: `Version=1.0.0.100`, `HostHtmlSmoke=true`,
  `LayoutDiagnostics=false`; the dark overlay and red diagnostic asset remain.
- Actual payload WebCore hash:
  `B8B721FA9E6FFCFFD4040A8402B189085308AB814463EDDC307A5A2C10FF7ABC`.
  The other three configured engine DLL hashes equal v99, retaining its ANGLE fix.
- Repaired dependency baseline: `%TEMP%/opencode/m10-engine-browser-stage-v33`
  with `%TEMP%/opencode/m10-engine-browser-stage-v33-identity.json`.
- Package `%TEMP%/opencode/m11-minibrowser-hosthtml-candidate-v100.appx` and
  matching `.identity.json`; shell output is
  `%TEMP%/opencode/m11-minibrowser-hosthtml-v100`.

Native build log `%TEMP%/opencode/m11-v100-engine-build.log` records successful
ARM WebCore rebuild/link with MSVC 14.44.35207, SDK 10.0.19041.0 and two jobs.
Shell log `%TEMP%/opencode/m11-v100-shell-build.log` records fresh ARM component,
metadata, compiled XAML, .NET Native package and signing checks plus visible-bounds
math 124/124. `%TEMP%/opencode/m11-v100-stage-1.log` records successful staging,
ARM/import audits and matching configured engine hashes. Additional checks pass:
rendering 68/68 and input/controller 58/58, both with ARM MSVC/clang compilation,
baseline/coherent-engine selection 5/5, public-control source 4/4, source doc audit
zero candidates and `git diff --check`.

OpenSpec 1.14.1 validation passes without strict mode. Strict validation reports
two existing requirement-length warnings (>500 characters): `Isolated host HTML`
in `uwp-web-navigation/spec.md` and `Bounded and distinguishable loading policies`
in `uwp-web-resource-loading/spec.md`. The spec deltas are unchanged from HEAD;
strict validation remains unresolved for the final integration gate.

Initial deployment retries timed out before submitting an install. After the
owner restored Portal connectivity, v100 installed (`Code=0`, `Success=true`),
launched with HTTP 200 and completed HostHtml at navigation ID 1 with `S_OK`.
Startup painted frames explicitly record `readback=off`. Interaction reports are
`%TEMP%/opencode/m11-v100-interaction-ux.txt` and
`%TEMP%/opencode/m11-v100-interaction-engine.txt`.

The eight saved profile windows show warmed composite-stage averages of
478–920 microseconds, with GPU waits now also appearing in swap costs. They are
not a matched benchmark or proof of chrome smoothness. The owner clarified the
remaining defect: web pixels no longer flash, but the address bar jumps instantly
between edit/display and positions imposed by keyboard/navigation-bar changes.
Source inspection confirms `GoToState(..., false)` installs state geometry with
no transition, and `ShellRoot.Margin` changes directly without chrome movement.

## v101 chrome movement: improved, timing/color still fail

v101 added `Views/Controls/ChromeLayoutTransition.cs` in the C# shell. Natural
`LayoutUpdated` observes final arrangement and uses render-only transforms to
preserve the prior visual position before moving to the final one. The real
TextBox moves without scaling its glyphs or replacing the focused input; a
sibling background handles size/opacity. Unload revokes layout observation;
width changes reset the baseline and Windows' animation setting is respected.
Browser viewport layout is not interpolated per animation frame.

Fresh ARM/.NET Native build, 124/124 margin math, 23/23 browser policy, signing,
coherent staging, installation (`Code=0`) and launch pass. Startup reports are
`%TEMP%/opencode/m11-v101-hosthtml-candidate.txt` and
`%TEMP%/opencode/m11-v101-hosthtml-engine.txt`. The owner reports movement is
smoother, but it lags the system transition and the editor background is black.
v101 therefore does not pass final chrome acceptance. Follow-up report retrieval
timed out at Portal; no v101 interaction report is claimed.

## Firefox Android source review and v102 follow-up

Current code is maintained under `mobile/android/` in `mozilla-firefox/firefox`;
the former `mozilla-mobile/firefox-android` repository is archived. Sources
inspected on 2026-10-13:

1. [`ImeInsetsSynchronizer.kt`](https://github.com/mozilla-firefox/firefox/blob/main/mobile/android/android-components/components/support/ktx/src/main/java/mozilla/components/support/ktx/android/view/ImeInsetsSynchronizer.kt)
   installs WindowInsets animation callbacks. IME `onProgress` uses the platform
   `interpolatedFraction`, reversed while hiding, with keyboard and system-bar
   insets accounted for separately. Its setup is gated to Android 13+.
2. [`BaseBrowserFragment.kt`, `setupIMEInsetsHandling`](https://github.com/mozilla-firefox/firefox/blob/main/mobile/android/fenix/app/src/main/java/org/mozilla/fenix/browser/BaseBrowserFragment.kt)
   anchors the bottom toolbar above the IME. On showing, web layout remains full
   size until animation completion, avoiding a burst of page reflows; on hiding,
   it expands the engine view first and moves the toolbar with the keyboard.
3. [`ViewYTranslationStrategy.kt`](https://github.com/mozilla-firefox/firefox/blob/main/mobile/android/android-components/components/ui/widgets/src/main/java/mozilla/components/ui/widgets/behavior/ViewYTranslationStrategy.kt)
   uses a 150-ms decelerating snap from current translation. This is the
   independent toolbar scroll/snap budget, not the IME's duration.
4. [`BrowserToolbar.kt`](https://github.com/mozilla-firefox/firefox/blob/main/mobile/android/android-components/components/compose/browser-toolbar/src/main/java/mozilla/components/compose/browser/toolbar/BrowserToolbar.kt)
   selects distinct edit/display content from state;
   [`BrowserEditToolbar.kt`](https://github.com/mozilla-firefox/firefox/blob/main/mobile/android/android-components/components/compose/browser-toolbar/src/main/java/mozilla/components/compose/browser/toolbar/BrowserEditToolbar.kt)
   explicitly separates themed editor surface from the input. These sources do
   not establish a 120-ms edit/display morph duration.

Windows 10 Mobile's
[`InputPaneVisibilityEventArgs`](https://learn.microsoft.com/en-us/uwp/api/windows.ui.viewmanagement.inputpanevisibilityeventargs?view=winrt-15063)
exposes `OccludedRect` and `EnsuredFocusedElementInView`, without an Android-style
IME fraction/duration. v102 adapts the event/progress ownership rather than
claiming access to that platform clock:

- A monotonic 150-ms system-motion fallback starts at Showing/Hiding or a changed
  system-inset notification. Later arrangements share its remaining time.
- Interrupted movement begins at the current visual but retains the existing
  deadline; another full 180-ms segment is not appended to each geometry update.
- A separately tuned 120-ms edit/display morph uses quadratic ease-out.
- The transparent focused-TextBox resource override from v101 is removed, and
  the original `TextControlBackgroundFocused`/foreground are restored. Background
  opacity is explicitly derived from `IsAddressEditing`, avoiding dependence on
  `GetAnimationBaseValue` versus VisualState setter precedence. Exact color/timing
  behavior still needs owner confirmation.

Package `WebKitProbe.MiniBrowser_1.0.0.102_arm__2932e6rj4m41p` builds/signs/stages,
installs (`Code=0`, `Success=true`) and launches (HTTP 200). Its fresh shell is
`%TEMP%/opencode/m11-minibrowser-hosthtml-v102`; staged candidate and matching
identity are `%TEMP%/opencode/m11-minibrowser-hosthtml-candidate-v102.appx` and
`.identity.json`. It reuses the four coherent v100 engine outputs, preserving
the accepted v99 ANGLE publication fix. Startup reports
`%TEMP%/opencode/m11-v102-hosthtml-candidate.txt` and
`%TEMP%/opencode/m11-v102-hosthtml-engine.txt` confirm `S_OK`, HostHtml Completed
at ID 1, ready backing synchronization and painted frames with `readback=off`.
Owner acceptance of keyboard movement, navigation-bar repositioning,
edit/display motion and editor color remains open. Task 11.0 remains open.

## v103: opaque edit surface and independent viewport

The owner supplied a Firefox Android screenshot and
`C:\Users\Longi\Downloads\video_2026-10-13_06-41-41.mp4`, then explicitly requested
full dark/light editing content covering the web, rather than a dim scrim. Video
analysis decoded 156 frames at 576x1280; final decoded PTS is 6.519 seconds.
The timestamped contact sheet is `%TEMP%/opencode/firefox-ui-reference-sheet.png`.
The edit/search surface is opaque and switches back to web content during keyboard
dismissal. Firefox's
[`AwesomeBarComposable.kt`](https://github.com/mozilla-firefox/firefox/blob/main/mobile/android/fenix/app/src/main/java/org/mozilla/fenix/search/awesomebar/AwesomeBarComposable.kt)
uses a full-size themed surface for suggestions, with the scrim as a distinct path.

v103 adds `WebLayoutRoot` and a separate chrome/edit `ShellRoot`, keeps address
height at 48 DIP in both modes, and applies system-only browser margins. An opaque
theme surface replaces the editing scrim. Viewport tests add four meaningful
IME-reduced-visible-bounds/system-bar cases; the total is 140/140 assertions.
It builds, stages, installs and launches successfully. Reports
`%TEMP%/opencode/m11-v103-interaction-ux.txt` and
`%TEMP%/opencode/m11-v103-interaction-engine.txt` show web height 611 remains
unchanged across repeated keyboard show/hide while chrome had a smaller edit
area. Native size changes belong to navbar transitions 611/659, not keyboard
animation frames. The owner still reports black text and insufficient field
background contrast; color acceptance fails.

## v105: native keyboard sequence and verified focused colors

The owner specified the interaction order: quick layer reveal, address morph,
then focus and ordinary Windows Phone keyboard avoidance. v105 uses an 80-ms
opaque surface reveal, a 120-ms input morph and cancelable deferred focus. Compact
address hit testing goes through the editing command instead of focusing the
TextBox before the surface is ready. Showing/Hiding set
`EnsuredFocusedElementInView=false`; manual IME margins are removed and the
independent reposition tween is suppressed while Windows owns keyboard movement.
This differs from the erroneous v93 combination of false plus manual avoidance.

The SDK 19041 `Generic/generic.xaml` default TextBox Focused state explicitly
sets `ContentElement.Foreground` to the black `TextControlForegroundFocused`
brush and `ContentElement.RequestedTheme` to Light (lines 29025–29042 in the
inspected SDK). v105 keeps a native `ContentElement` host in a minimal template
whose foreground comes from the address control, without forcing Light. Editor
fill is set explicitly, so stopping a background storyboard cannot restore a
transparent base behind the text.

The intermediate v104 diagnostic read `FrameworkElement.ActualTheme`, introduced
in UniversalApiContract v5 / SDK 16299 and unavailable on this Mobile platform.
Its initial report stopped after Starting; no dump analysis is claimed. v105
removes that call and uses supported requested-theme/color diagnostics. Its
complete run is the current native-focus/color evidence.

v105 installs with `Code=0`, launches and completes HostHtml. The owner reports
"much better", then confirms the colors in a supplied Lumia screenshot while
requesting square corners and improved vertical centering. Saved reports are
`%TEMP%/opencode/m11-v105-interaction-ux.txt` and
`%TEMP%/opencode/m11-v105-interaction-engine.txt`. Repeated focused-input/keyboard
records prove:

- App theme Dark; input and native content requested themes remain Default.
- Input and native content foreground both `#FFF5F5F5`.
- Field fill `#FF343238`, opacity 1 while editing and 0 in compact mode.
- Web viewport remains 411.4286x611.4286 through keyboard-only transitions;
  navbar changes still update the independent visible area normally.
- No GPU paint/swap/GL/unresolved-resize errors occur in the saved run.

## v106: rectangular field and native vertical centering

The owner rejected pill rounding as inconsistent with Windows Phone and supplied
a screenshot showing text above the field center. v106 removes CornerRadius 20,
passes `VerticalContentAlignment` through to the native content ScrollViewer,
centers the placeholder too, and uses symmetric zero vertical padding. This
removes the historical 2-DIP optical padding compensation. Optional layout
diagnostics now check the system-only/native-keyboard geometry and symmetric
padding instead of the retired app-owned keyboard margin.

Package `WebKitProbe.MiniBrowser_1.0.0.106_arm__2932e6rj4m41p` has a fresh ARM
component, compiled XAML and warning-free .NET Native build. Math 140/140,
metadata/public-component gates, signing and coherent staging pass; installation
returns `Code=0`, launch HTTP 200, initialization `S_OK`, HostHtml Completed ID 1
and readback-off painted presentation. The matching shell, candidate and identity
are `%TEMP%/opencode/m11-minibrowser-hosthtml-v106` and
`%TEMP%/opencode/m11-minibrowser-hosthtml-candidate-v106.appx` / `.identity.json`.

Startup/interaction files are `%TEMP%/opencode/m11-v106-hosthtml-candidate.txt`,
`%TEMP%/opencode/m11-v106-hosthtml-engine.txt`,
`%TEMP%/opencode/m11-v106-interaction-ux.txt` and
`%TEMP%/opencode/m11-v106-interaction-engine.txt`. In edit/focus and keyboard-show
records, native alignment is Center, padding `6,0,6,0`, both foregrounds remain
`#FFF5F5F5`, field `#FF343238` with opacity 1; web viewport stays unchanged.
Runtime values support the implementation but final visual centering/rectangle
acceptance still needs an owner observation. Light theme and landscape are not
claimed verified by this dark portrait run. Task 11.0 remains open for remaining
UX qualification and remote/TLS integration; ordinary capabilities remain zero.

### v106 owner result: vertical alignment fails

The owner supplied a compact-mode screenshot showing text even higher than the
field center, and separately requested consistent input/chrome heights across
display/edit. A reported `VerticalContentAlignment=Center` was insufficient:
the native content host itself still occupied the input's full vertical slot.
v106 does not pass this visual gate.

## v107: owner-confirmed centering and consistent geometry

The template now sets the native ContentElement ScrollViewer's own
`VerticalAlignment=Center`, allowing its intrinsic single-line height to be
arranged centrally. Compact/edit inputs both have 40-DIP height within 48-DIP
chrome, with 4-DIP vertical margins and 15-DIP text. Zero vertical padding remains.
The compact editing hit button uses an always-transparent template, avoiding
pointer-pressed background retained when the button collapses during editing.
The rectangular shape, full themed edit surface and native keyboard sequence
remain the current owner-requested UX.

Fresh shell output `%TEMP%/opencode/m11-minibrowser-hosthtml-v107` passes ARM
component/compiled-XAML/.NET Native build, 140/140 viewport/margin assertions,
signature and package gates. The matching candidate
`%TEMP%/opencode/m11-minibrowser-hosthtml-candidate-v107.appx` installs with
`Code=0`, launches HTTP 200, initializes `S_OK` and completes HostHtml at ID 1.
Its `.identity.json` records the same four coherent v100 engine payload hashes,
preserving v99's ANGLE publication repair and readback-off performance changes.

Saved accepted-run reports are `%TEMP%/opencode/m11-v107-accepted-ux.txt`
(24,857 bytes) and `%TEMP%/opencode/m11-v107-accepted-engine.txt` (4,590 bytes).
Compact and focused-edit snapshots record native/input/chrome heights **20/40/48**;
edit uses native/input foreground `#FFF5F5F5`, field `#FF343238`, opacity 1 and
padding `6,0,6,0`. Keyboard-only transitions retain the web viewport and do not
introduce paint/swap/GL/unresolved-resize errors. The owner explicitly selected
"both are correct" for vertical centering/caret and consistent display/edit height.
This is the visual acceptance of the requested dark portrait refinement, rather
than an inference from property values alone. Light-theme/landscape qualification
and task-11.0 remote/TLS integration remain open.
