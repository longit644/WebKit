# XAML probe: toolchain findings and remaining step

Date: 2026-10-04, revised after ARM MSVC validation. Owner decision: use C++/CX
for the ARM32 control with the MSVC `/ZW` authoring path.

## Question

Can a native component in this repository present itself as a real XAML
`UserControl`, constructable from markup? The earlier WRL attempt (device
1.0.0.73-75) answered neither `IContentControl` nor inner-object QI, leaving
open whether the platform forbids it or only that approach failed.

## Correction to the existing design record

`design-winrt-component.md` §2 states `cppwinrt.exe` is **"Absent (NuGet-only,
no network install assumed)"** and concludes the component must be WRL-only.
That is wrong. It is installed in the SDK:

```
C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\cppwinrt.exe
C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\arm64\cppwinrt.exe
```

The SDK also ships the C++/WinRT projection headers the earlier note believed
missing:

```
C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\cppwinrt\winrt\
    base.h, windows.ui.xaml.controls.h, windows.ui.xaml.h, ...
```

Consequence: deriving from `UserControl` is buildable, and the reason the WRL
attempt failed is a tool limitation, not a platform prohibition.

## Working generation pipeline

```powershell
# 1. IDL -> winmd + C headers (same dialect as WebKitWebView.idl).
$env:Path = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64;' + $env:Path
& 'C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/midlrt.exe' `
  /winmd build-arm-winrtcomponent/probe-component/XamlProbe.winmd `
  /metadata_dir 'C:\Program Files (x86)\Windows Kits\10\UnionMetadata\10.0.26100.0' `
  /I 'C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\winrt' `
  /I 'C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\um' `
  /I 'C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0\shared' `
  /h build-arm-winrtcomponent/probe-component/XamlProbe.h `
  /iid build-arm-winrtcomponent/probe-component/XamlProbe_i.c `
  Tools/uwp/WinRTComponent/XamlProbe.idl

# 2. winmd -> C++/WinRT projection. -platform and -metadata_dir are rejected
# by this cppwinrt; reference Windows.winmd explicitly instead.
& 'C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64/cppwinrt.exe' `
  -input build-arm-winrtcomponent/probe-component/XamlProbe.winmd `
  -reference 'C:\Program Files (x86)\Windows Kits\10\UnionMetadata\10.0.26100.0\Windows.winmd' `
  -output build-arm-winrtcomponent/probe-component/gen -component -optimize -overwrite
```

Both steps verified `EXIT=0`. Generated output includes
`winrt/WebKitProbe.h` and `winrt/impl/WebKitProbe.2.h`.

## The important result

The generated projection declares `XamlProbe` as deriving from `IXamlProbe`
plus the full inherited chain:

```
impl::base<XamlProbe, UserControl, Control, FrameworkElement, UIElement, DependencyObject>
impl::require<XamlProbe, IUserControl, IControl..IControl7, IFrameworkElement..,
              IUIElement..IUIElement10, IAnimationObject, IVisualElement,
              IDependencyObject, IDependencyObject2>
```

So the projection **delegates every inherited interface automatically**. That is
precisely the work that was infeasible by hand in WRL, and it is why the earlier
attempt produced an object that answered neither `IContentControl` nor
inner-object QI. This is strong evidence the declarative control shape is
achievable — but it is compile-time evidence only, not a device result.

## Compile requirements for a C++/WinRT target

Established by fixing real build failures:

- `/EHsc` and `_HAS_EXCEPTIONS=1` — `winrt/base.h` throws. The port builds
  `-D_HAS_EXCEPTIONS=0 /EHs-c-`, so these must be enabled for this target only.
  It links no engine code, so its exception setting cannot affect WebCore.
- `cxx_std_20` — clang rejects the SDK's `<experimental/coroutine>` header that
  C++17 pulls in.
- `/GR` — RTTI must be re-enabled alongside exceptions.
- `#include <unknwn.h>` before any `winrt/` header, else `_GUID` is incomplete.
- Include the generated output as `<winrt/WebKitProbe.h>`; a bare
  `<windows.ui.xaml.h>` resolves to the wrong C header.
- Do **not** define `WINRT_LEAN_AND_MEAN`; it strips the XAML projections.
- Do not mix the generated `probe/winrt/base.h` with the SDK's copy. Delete the
  generated copy and add both `probe/` and the SDK `cppwinrt/` as include dirs
  so a single `winrt/base.h` is used everywhere.

## Second attempt: what now fails, and the narrow blocker

Both plausible C++/WinRT shapes were compiled. Findings:

- `implements<T, IXamlProbe>` takes the **implementing type first**, not an
  interface. Passing `IXamlProbe` there yields "no type named `implements_type`".
  cppwinrt normally hides this behind a generated `XamlProbeT<T>` template, which
  this class does not get, so the equivalent template has to be written by hand.
- Passing `UserControl` as a further `implements<>` argument is **wrong**: it asks
  the type to *implement* `IUserControl`, so the compiler then demands
  `Content`, `Width` and the whole surface ("no member named `Content`").
- Real inheritance works instead — `struct XamlProbeT<T> : UserControl,
  implements<T, IXamlProbe>` — and the probe constructor then compiles and calls
  `Content`/`Width`/`Height` successfully. This got much further than WRL.
- Remaining failure, in both base orders:
  `winrt/base.h: new impl::heap_implements<D>` → **"call to deleted function
  `operator new`"**. `implements` deletes `operator new` and `heap_implements` is
  supposed to supply it, but with an extra non-`implements` base class the lookup
  finds the deleted declaration.

The activation factory also cannot use the C++/WinRT `IActivationFactory`
projection with this `base.h` (wrong return types, and `winrt::make_factory` is
absent). A WRL `RuntimeClass<..., IActivationFactory>` factory compiles fine, and
`WindowsCreateString` plus raw-buffer comparison work for the class-ID check.

## Most promising next step

The `operator new` problem is an allocation-policy detail, not a platform limit.
Options, in order of promise:

1. Give the probe its own allocation rather than `make<>`: construct with
   `Microsoft::WRL::Make<XamlProbe>()` or `new` inside the WRL factory, so
   C++/WinRT's `heap_implements` path is never instantiated.
2. Put `implements<T, IXamlProbe>` first and inherit `UserControl` through
   `impl::base<...>` explicitly, mirroring the generated projection instead of
   deriving from the projected class directly.
3. As a control experiment, instantiate the probe with no `UserControl` base at
   all. If that compiles and activates, the failure is isolated to multiple
   inheritance; if it fails identically, the blocker is the deleted `operator
   new` under this toolchain's `/D_HAS_EXCEPTIONS=0` forced include.

Whichever succeeds, the probe still needs a host that activates it inside a real
XAML tree on the phone. Only that run answers the original gate.

## Decisive ARM32 result

The newer compiler installation exposed a stronger blocker than the earlier
clang code-generation failure. With MSVC 19.44 ARM32, the SDK C++/WinRT base
header reaches this explicit branch:

```cpp
#if defined _M_IX86 || defined _M_X64
// supported
#elif defined _M_ARM64
// supported
#else
#error Unsupported architecture
#endif
```

Therefore C++/WinRT cannot author this ARM32 component, regardless of whether
the compiler is clang or MSVC. The generated `-component` projection remains
valid evidence for x64 and ARM64, but it is not an ARM32 solution.

## C++/CX result and decision

The installed MSVC ARM32 compiler accepts a minimal real `UserControl` with
`/ZW`, and the probe implementation compiles with the same path:

```text
MSVC 19.44.35229 for ARM
/ZW /EHsc /std:c++17
```

The C++/CX implementation uses a public `ref class XamlProbe : UserControl`
and retains a small WRL `IActivationFactory` shell only for DLL activation. It
does not manually delegate the inherited XAML interfaces. C++/CX is therefore
the selected authoring model for the ARM32 probe and eventual control.

The currently selected Windows 11 SDK 10.0.26100.0 has no `um\arm` libraries.
The Windows 10 SDK 10.0.19041.0 must also be installed so the DLL can link
`runtimeobject.lib`, `ole32.lib`, `uuid.lib`, and the ARM CRT startup objects.

## Remaining step

Install the Windows 10 SDK 10.0.19041.0, link the browser-free C++/CX probe,
and run it inside a real XAML tree on the phone. The device run must report
activation, default-interface identity, inherited XAML QI, measured size, and
teardown. That run is the only final answer to the original platform gate.
