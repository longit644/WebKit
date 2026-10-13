# HostHtml presentation: Lumia v92

Date: 2026-10-12. Source checkpoint: `89adccf24b`, with local uncommitted 11.0 changes.

## Device and package

- Lumia 950 XL / RM-1085; OS `15254.603.armfre.feature2_rs3svc.200106-1633`.
- Device Portal: `https://192.168.1.52`.
- Package: `WebKitProbe.MiniBrowser_1.0.0.92_arm__2932e6rj4m41p`.
- Application: `WebKitProbe.MiniBrowser_2932e6rj4m41p!App`.
- Install accepted (HTTP 202); deployment completed with `Code=0`, `Success=true`;
  launch HTTP 200. App remains in the process list as running; no v92 crash dump.
- Signed package: `%TEMP%\opencode\m11-minibrowser-hosthtml-candidate-v92.appx`.
- Full build/payload/baseline provenance:
  `%TEMP%\opencode\m11-minibrowser-hosthtml-candidate-v92.identity.json`.
- Package audit: 28 ARMNT/AppContainer binaries; Skia present; recursive import,
  metadata and required asset audit passed. Signing and signature verification passed.
- Fresh shell: `%TEMP%\opencode\m11-minibrowser-hosthtml-v92\MiniBrowserW10M-arm.appx`
  (HostHtmlSmoke, warning-free, .NET Native, compiled XAML).
- Engine: resumed `m10-clean-final-2jobs` WebCore with candidate presentation slots;
  no PCH macro mismatch and no enum-float warnings after the fix.

## What changed since v90/v91

- Engine forwards negotiated `resize`, `attachCompositor`, `detachCompositor`,
  `renderCompositor` with frozen validation; close detaches the visual first.
  Candidate mask adds `GPUCompositor`; ordinary builds stay at zero.
- `PageControllerW10M` forwards the same four slots, gating compositor calls on
  the GPU capability and preserving originating HRESULTs.
- `WebKitWebView` hosts a `SwapChainPanel` as its `Content`, owns a
  `SwapChainPresenterW10M`, resizes the engine page from `ActualWidth`/
  `ActualHeight` plus panel `CompositionScale`, attaches once, renders dirty
  frames through the shared `FrameSchedulerDriverW10M` callback, and detaches
  before close. Steady geometry short-circuits so resize/attach run only on
  real layout changes (v91 spammed 1,591 resizes; v92 logs three real ones).

## Device result

Public report (`m11-v92-hosthtml-candidate.txt`):

```text
package=1.0.0.92
initialized: result=S_OK
starting: id=1;kind=HostHtml;uri=about:blank
completed: id=1;kind=HostHtml;outcome=Completed;http=False;status=0
terminal: source=about:blank;back=False;forward=False;loading=False;title=;status=Host content Completed.
```

Native report (`m11-v92-hosthtml-engine.txt`, 89 lines):

```text
presentation-css-width result=411
presentation-css-height result=611
presentation-scale-milli result=3500
gpu-paint-backend-skia-ganesh result=1
gpu-texturemapper-context-ready result=0
ANGLE (Qualcomm, Qualcomm Adreno 430 ..., D3D11-11.18.1078.53) result=0
gpu-root-layer-attached result=1
gpu-compositor-enabled result=0
navigation-committed-id result=1
document-bytes-parsed result=374
navigation-completed result=0
gpu-surface-resize revision=2 css=411x609 scale=3.500 ... ready=1
gpu-document-composited-scrolling result=1
presentation-first-frame result=0
```

The 609→611 height pair reflects real shell layout settling (progress/status
chrome), not per-frame churn. `presentation-first-frame` records a real
`eglSwapBuffers` into the attached panel.

## Remaining acceptance

Initialization, engine commit/parse/finish, public Starting/Completed,
history neutrality (Back/Forward false), and first-frame presentation are
proven. Device Portal exposes no screenshot endpoint on this phone
(`GET /api/screenshot` returns HTTP 404), so visible pixels still need owner
confirmation on the handset: the demo heading `MiniBrowser app-owned content`
should be visible above the address chrome. Remote navigation and TLS/consent
qualification follow the visual gate. Task 11.0 remains open; ordinary builds
continue to advertise capability mask zero.

## Focused regression checks

- Input/PageController contract: 58/58 checks; both ARM boundary compilers pass.
- Rendering/presenter contract (WARP): 67/67 checks; ARM separation passes.
- Dependency-baseline guard: 4/4 tests.
- Public-control contract: 4/4 tests.
- Source documentation audit: 0 candidates; audit tests 9/9.
- `git diff --check`: passed (CRLF/LF notices only).
