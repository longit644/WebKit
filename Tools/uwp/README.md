# WebKitViewUWP — WPE WebKit core for UWP ARM32 (950 XL)

Base: WPE tag `wpewebkit-2.54.0`, branch `uwpwebkit/2.54`.
Core only: `WTF + JSC CLoop + WebCore`. No browser chrome here.

## Rules

1. Upstream tag pristine. Our work on `uwpwebkit/2.54` only.
2. Every upstream edit guarded: `#if defined(WK_WEBKITVIEW)` + `WebKitView:` comment.
3. ASCII paths only. Exceptions off (`_HAS_EXCEPTIONS=0`, `/EHs-c-`).
4. Single engine thread. Software render first, no JIT/GPU yet.
5. Tags = device proof: `wkview-m0-empty`, `wkview-m1-jsc`, `wkview-m2-parse`, `wkview-m3-render`.

## Layout

- `Tools/UWP/` (here) — port tooling: triplet, toolchain, compiler wrapper,
  ARM C/C++ runtime sources + prebuilt `lib/`, env script, this doc set.
- `WebKitLibraries/triplets|toolchains/` — vcpkg triplet + cmake toolchain.
- `Source/WebKit/UIProcess/API/uwp/` (M4) — `WebKitWebViewUWP` control,
  mirroring `API/gtk/` structure.
- `Tools/MiniBrowser/uwp/` — test shell skeleton (mirrors `win/`).
- Patches generated via `git diff upstream...HEAD -- Source/`.
