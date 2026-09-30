# wkview-m0-toolchain — complete host build toolchain

## Why

M1 (host JSC configure with CLoop) is blocked: this PC has BuildTools skeleton
only — no `vcvarsall.bat`, no `MSVC/` compiler — so vcpkg refuses to build
`icu`, and WebKit's `build-jsc --system-information` fails on `nmake`.

## What

1. Install VS2022 C++ workload (elevated, owner runs it):
   `choco install -y visualstudio2022buildtools --package-parameters
   "--add Microsoft.VisualStudio.Workload.VCTools
   --add Microsoft.VisualStudio.Component.VC.Llvm.Clang
   --add Microsoft.VisualStudio.Component.Windows10SDK"`
2. Verify: `cl`, `vcvarsall.bat` present; vswhere reports complete instance.
3. `vcpkg install icu --triplet x64-windows` succeeds.
4. `build-jsc --system-information` passes (no nmake error).

## Non-goals

No engine code, no ARM cross, no device work. Pure host readiness.

## Acceptance

Owner pastes `cl` version + `vcpkg list icu` output; agent re-runs probe clean.
