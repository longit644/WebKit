# Morning handoff — 2026-10-04 night session

State when work stopped, for whoever picks this up first. Read
`xaml-probe-findings.md` in this directory for the full evidence.

## Headline

The XAML feasibility question was **answered by elimination tonight**, and not in
our favour. C++/WinRT — the option chosen earlier today — **cannot be used for a
native UWP component on this toolchain**. The blocker is clang 19's ARM32 backend,
not the phone and not our code.

## What was proven

The probe's C++ is now frontend-clean, including real inheritance from the SDK
`UserControl`:

```cpp
template <typename T>
struct XamlProbeT : UserControl, winrt::implements<T, IXamlProbe> { ... };
```

Everything needed for the control contract is therefore *expressible*: the
generated projection supplies the entire inherited chain (`IUserControl`,
`IControl1-7`, `IFrameworkElement1-7`, `IUIElement1-10`, `IDependencyObject`)
automatically — the delegation that was infeasible by hand in WRL.

What fails is code generation. Reduced to 12 lines with no `UserControl`, no
`winrt::make<>` and none of our code:

```
fatal error: error in backend: Cannot select: ch = cleanupret
fatal error: error in backend: Cannot select: ch = catchret
```

C++/WinRT requires exceptions; clang 19 cannot generate ARM32 EH cleanup for a
`winrt::implements<>` type. Only clang 19 is installed —
`C:\Users\Longi\llvm-tools` has `lld-link`, `rc.exe`, `flex`, `LIB.EXE` and
nothing else.

Also corrected earlier: `cppwinrt.exe` and the C++/WinRT headers **do** ship with
the SDK. The old design note claiming otherwise was simply wrong, and that wrong
note is why WRL was chosen in the first place.

## Decision needed from the owner

1. **Install a newer LLVM and re-run the 12-line repro.** If the ARM32 EH bug is
   fixed there, option 1 works and declarative XAML stays achievable. Cheap to
   test: `clang-cl --target=armv7-unknown-windows-msvc -c` the repro from
   `xaml-probe-findings.md`.
2. **Fall back to WRL + interim hosting API.** The control owns a `UserControl`
   the host parents, which already works via `xamlElement()`. Cost: no
   declarative `<web:WebKitWebView />`.

Recommendation: try (1) first. It is a single compiler download and one test, and
it preserves the entire M9 goal. If a newer clang still crashes, take (2) and
re-scope the XAML requirement explicitly rather than pretending it is met.

## Repo state

- Branch `uwpwebkit/2.54`, all work committed, working tree clean.
- Four commits this session: `01cc7a25b0` (backup), `6a28b449ee` (typed document
  facts), `e1de37cf51` (probe scaffolding), `9dd0f7a9a8` (probe findings).
- `Co-Authored-By` trailers were stripped from all four at the owner's request;
  do not add them again.
- `build-arm-winrtcomponent` builds `EXIT=0`. The probe target is deliberately
  **not** wired into CMake, so nothing is broken.
- The probe compiles standalone only by bypassing CMake:
  `clang-cl --target=armv7-unknown-windows-msvc -std:c++20 /EHsc /GR` with
  `-I build-arm-winrtcomponent/probe/gen` and the SDK `cppwinrt` include dir.
  Generated projection is regenerable via the recipe in `xaml-probe-findings.md`.

## Unrelated item still open

`build-arm-uwp-skia` WebCore and `build-arm-minibrowser` have not had their exit
statuses recaptured since the bridge change. `WebCore` was verified `EXIT=0` and
the new `WKGetPageDocumentStateUWP` export confirmed present in the DLL; MiniBrowser
has not been rebuilt.

## Still not proven by anyone

- The device has never run the probe. Nobody has confirmed whether Windows 10
  Mobile 15254 accepts a projection-derived `UserControl` at activation.
- Fixture-style visual checks (CSS applied, PNG decoded, web font used) still rely
  on historical device logs from earlier sessions, not on re-verification.
- `sol-decisions-for-space-bunny-alpha.md` is not independent review. It is the
  same author as the architecture draft, and it already contained one factual
  error about the toolchain. Treat it as one opinion, not validation.