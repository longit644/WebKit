# Morning handoff — 2026-10-04 night session

State after the ARM32 toolchain validation. Read `xaml-probe-findings.md` for
the full evidence.

## Headline

C++/CX is selected for the ARM32 declarative XAML probe and eventual control.
The MSVC ARM32 compiler accepts `/ZW` and a real `UserControl`; C++/WinRT is
not an ARM32 option because the SDK `winrt/base.h` explicitly rejects `_M_ARM`.

## What was proven

- `cppwinrt -component` generates the correct inherited XAML projection, but
  that projection cannot compile for 32-bit ARM with either clang or MSVC.
- MSVC 19.44.35229 ARM accepts a C++/CX `public ref class XamlProbe :
  UserControl` with `/ZW /EHsc /std:c++17`.
- The real C++/CX probe source compiles for ARM32. Its WRL code is limited to
  the activation-factory shell; the control itself is a C++/CX `UserControl`.
- The selected Windows 11 SDK 10.0.26100.0 has no `um\arm` libraries.

## Remaining installation

Select **Windows 10 SDK 10.0.19041.0** in the Build Tools installer. It is
needed for ARM32 UWP link libraries, including `runtimeobject.lib`, `ole32.lib`,
and `uuid.lib`. Do not select the ARM64/ARM64EC UWP support item for this target.

## Repo state

- Branch `uwpwebkit/2.54`, HEAD `a096d427c0`.
- Uncommitted probe implementation and documentation changes are intentional.
- `Co-Authored-By` trailers remain prohibited.
- The shipping `WebKitWebView` target is unchanged and the probe remains out of
  normal CMake until its standalone ARM32 DLL links.

## Next steps

1. Install the Windows 10 SDK 10.0.19041.0.
2. Regenerate the probe metadata with that SDK's MIDLRT compiler.
3. Compile and link `XamlProbeComponent.cpp` with C++/CX `/ZW`, the ARM SDK
   libraries, `vccorlib.lib`, and `Tools/uwp/lib/arm-crtstart.lib`.
4. Run the browser-free DLL inside a real XAML tree on the phone and record
   activation, default-interface identity, inherited XAML QI, measured size,
   and teardown.
5. Only after that device gate passes, convert the public WebKit control to the
   same C++/CX `UserControl` shape and resume the typed navigation bridge.

## Still open

- No device has run the new C++/CX probe.
- Fixture-style visual checks rely on historical device logs.
- `build-arm-minibrowser` has not been rebuilt since the bridge changes.
