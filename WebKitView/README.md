# WebKitView — WPE WebKit core for UWP ARM32 (950 XL)

Base: WPE tag `wpewebkit-2.54.0`, branch `wpe-uwp-arm32/main`.
Core only: `WTF + JSC CLoop + WebCore`. No browser chrome here.

## Rules

1. Upstream tag pristine. Our work on `wpe-uwp-arm32/main` only.
2. Every upstream edit guarded: `#if defined(WK_WEBKITVIEW)` + `WebKitView:` comment.
3. ASCII paths only. Exceptions off (`_HAS_EXCEPTIONS=0`, `/EHs-c-`).
4. Single engine thread. Software render first, no JIT/GPU in M1-M3.
5. Tags = device proof: `wkview-m0-empty`, `wkview-m1-jsc`, `wkview-m2-parse`, `wkview-m3-render`.

## Layout (later)

- `WebKitView/Driver/` — C ABI engine driver (after M3).
- `WebKitView/Control/` — XAML `WebKitView` control + demo (after core renders).
- Patches generated via `git diff upstream...HEAD -- Source/`.
