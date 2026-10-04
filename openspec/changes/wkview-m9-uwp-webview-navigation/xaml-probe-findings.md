# XAML probe: toolchain findings and remaining step

Date: 2026-10-04. Owner decision: use C++/WinRT for the control, with
exceptions and C++20 enabled for that target only.

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
C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\cppwinrt.exe
C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\arm\cppwinrt.exe
```

The SDK also ships the C++/WinRT projection headers the earlier note believed
missing:

```
C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\cppwinrt\winrt\
    base.h, windows.ui.xaml.controls.h, windows.ui.xaml.h, ...
```

Consequence: deriving from `UserControl` is buildable, and the reason the WRL
attempt failed is a tool limitation, not a platform prohibition.

## Working generation pipeline

```powershell
# 1. IDL -> winmd + C headers (same dialect as WebKitWebView.idl).
$env:Path = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Tools\MSVC\14.44.35207\bin\Hostx64\x64;' + $env:Path
& 'C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64/midlrt.exe' `
  /winmd Tools/uwp/WinRTComponent/XamlProbe.winmd `
  /metadata_dir 'C:\Program Files (x86)\Windows Kits\10\UnionMetadata\10.0.22621.0' `
  /I 'C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\winrt' `
  /I 'C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\um' `
  /I 'C:\Program Files (x86)\Windows Kits\10\Include\10.0.22621.0\shared' `
  /h Tools/uwp/WinRTComponent/XamlProbe.h `
  /iid Tools/uwp/WinRTComponent/XamlProbe_i.c `
  Tools/uwp/WinRTComponent/XamlProbe.idl

# 2. winmd -> C++/WinRT projection. -platform and -metadata_dir are rejected
# by this cppwinrt; reference Windows.winmd explicitly instead.
& 'C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64/cppwinrt.exe' `
  -input Tools/uwp/WinRTComponent/XamlProbe.winmd `
  -reference 'C:\Program Files (x86)\Windows Kits\10\UnionMetadata\10.0.22621.0\Windows.winmd' `
  -output build-arm-winrtcomponent/probe -optimize
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

## Decisive isolation: C++/WinRT is not viable on this toolchain

All C++ errors in the probe were resolved. What remains is a **clang 19 ARM32
backend crash**, reproduced down to 12 lines with no involvement from our code,
`UserControl`, or even `winrt::make<>`:

```
fatal error: error in backend: Cannot select: ch = cleanupret
fatal error: error in backend: Cannot select: ch = catchret
```

Minimal repro (compiles for x64; crashes only for ARM32):

```cpp
template <typename T> struct PT : winrt::implements<T, IInspectable> {
    static void* operator new(size_t n) { return ::operator new(n); }
};
struct P : PT<P> {};
IInspectable o = winrt::make<P>();   // crashes; so does: P* p = new P(); QI(p)
```

What was eliminated one variable at a time:

| Variant | Result |
|---|---|
| Probe as written | `cleanupret` crash |
| Same, `-Od` instead of `-O2` | still crashes — not an optimisation bug |
| Sized `operator delete` removed | still crashes |
| **No `UserControl` base at all** | still crashes (`catchret`) |
| **`winrt::make<>` bypassed**, plain `new` + `QueryInterface` | still crashes |
| `XamlProbeT<T> : UserControl, implements<T, IXamlProbe>` | **frontend clean**, only codegen fails |

So the trigger is `winrt::implements<>` itself combined with exception handling.
C++/WinRT *requires* exceptions (`winrt/base.h` throws, which is why
`_HAS_EXCEPTIONS=0` had to be overridden), and clang 19 cannot generate the ARM32
EH cleanup for such a type.

No alternative compiler is installed: only VS BuildTools clang 19, and
`C:\Users\Longi\llvm-tools` holds only `lld-link`, `rc.exe`, `flex` and `LIB.EXE`.

### Consequence for the owner's option 1

Option 1 (C++/WinRT) is **not achievable with the current toolchain**. It is not a
platform limitation on Windows 10 Mobile — the earlier conclusion still stands that
declarative XAML is probably fine. It is a compiler limitation on ARM32.

Two ways forward, both needing an owner decision:

1. **Install a newer LLVM** and re-run the 12-line repro. If a later clang fixed
   the ARM32 EH cleanup bug, option 1 becomes viable and the generated projection
   is already proven to supply the whole inherited interface chain.
2. **Fall back to WRL with the interim hosting API.** Give up declarative
   `<web:WebKitWebView />` for now; the control owns a `UserControl` that the host
   parents, which already works through `xamlElement()`.

Hand-writing the `IUserControl` delegation in WRL is the third option and remains
impractical: the generated projection shows it is mechanical, but it is dozens of
methods per interface across `IUserControl`, `IControl1-7`, `IFrameworkElement1-7`,
`IUIElement1-10` and `IDependencyObject`.

## Remaining step

Derive the implementation from the generated type and confirm it activates and
lays out. Unresolved detail: cppwinrt does not emit an `XamlProbeT<T>` template
here (neither plain nor `-component`), and the generated `impl::base<XamlProbe>`
binds the shim to the generated type. Whether deriving a local struct from
`winrt::WebKitProbe::XamlProbe` and registering `make_factory` on the derived
type dispatches `get_ProbeId` correctly is the last compile-time unknown. If it
does not, declare the probe with an explicit implementation stub in the IDL so
cppwinrt emits the `T` template.

After it builds, the probe still needs a host that activates it inside a real
XAML tree on the phone and reports interface identity plus measured size. That
run is the only thing that can answer the original blocker, and it needs a
person on the device.