# WebKitWebViewUWP — WPE WebKit core for UWP ARM32 (950 XL)

## Active M10 design

Read [DESIGN.md](DESIGN.md) for Sol's detailed C++/CX architecture, naming,
folder structure, public contract, compiler boundary and migration gates.
[M10 OpenSpec](../../openspec/changes/wkview-m10-uwp-cppcx-restructure/README.md)
contains the normative requirements, migration map and implementation tasks for
the implementing model. M9 is superseded history; M10 is not implemented yet.

Names use `UWP` (for example `PageControllerUWP`), lowercase `uwp/` directories,
and the unchanged public control `WebKit.WebKitWebView` authored directly in
`Source/WebKit/UIProcess/API/uwp/WebKitWebView.h/.cpp`.

Base: WPE tag `wpewebkit-2.54.0`, branch `uwpwebkit/2.54`.
Core only: `WTF + JSC CLoop + WebCore`. No browser chrome here.

## Rules

1. Upstream tag pristine. Our work on `uwpwebkit/2.54` only.
2. Preserve upstream structure and use the established WK_UWP / PLATFORM(UWP) guards for port-specific edits.
3. ASCII paths only. Native engine exceptions remain off; MSVC C++/CX components/apps enable `/ZW` and their required exception/runtime support separately.
4. Single engine-owning thread. MiniBrowser uses the validated Skia backend; JIT remains unsupported.
5. Tags = device proof: `wkview-m0-empty`, `wkview-m1-jsc`, `wkview-m2-parse`, `wkview-m3-render`.

## Layout

- `Tools/uwp/` (here) — port tooling: triplet, toolchain, compiler wrapper,
  ARM C/C++ runtime sources + prebuilt `lib/`, env script, this doc set.
- `WebKitLibraries/triplets|toolchains/` — vcpkg triplet + cmake toolchain.
- `Source/WebKit/UIProcess/API/uwp/` — current native wrapper; M10 places the
  C++/CX public control and decomposed native/input/rendering/platform services here.
- `Tools/MiniBrowser/uwp/` — sample browser; M10 consumes the reusable component.
- Patches generated via `git diff upstream...HEAD -- Source/`.

See [ARCHITECTURE.md](ARCHITECTURE.md) for the ownership boundary between the
browser framework, MiniBrowser, device test harnesses and platform tooling.
