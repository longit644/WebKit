# M10 5.1: authored public C++/CX contract

## Source and revision

The authored root is `Source/WebKit/UIProcess/API/uwp/`, with `WebKitWebView.h/.cpp`, `WebKitNavigationTypes.h`, `WebKitNavigationEventArgs.h/.cpp`, `WebKitWebViewOptions.h/.cpp`, and `WebKitCertificateFailureToken.h/.cpp`. Public names have no implementation `UWP` suffix. Native helpers retain it.

Metadata revision 2 uses the new default interface `WebKit.IWebKitWebViewV2`, IID `B0CACD92-A344-4B7B-9E94-99C3C3BFB35C`. No incompatible prototype IID is reused. The revision is recorded by this interface identity and the build manifest, separately from the compiler's ordinary WinRT type version 1.

`WebKit.dll` and `WebKit.winmd` come from the same ARM32 MSVC objects. The entry-point shell delegates to `Platform::Module::GetActivationFactory`, serving the compiler's parameterized-constructor/static interfaces and the actual XAML inheritance. It allows activation only for the control and options. Native callbacks require the component to remain resident (`DllCanUnloadNow=S_FALSE`).

## Measurements and fixes

- Self-referential public `TypedEventHandler<WebKitWebView, Args>` initially produced C4462. A named, explicitly default interface resolves the GUIDs, as documented by Microsoft's C4462 reference; suppressing the diagnostic was not necessary.
- Explicit `Version(2)` on the authored interface produced C3163; on runtime types it duplicated the compiler's version attributes. Contract revision 2 therefore uses the new interface identity and manifest, with compiler version attributes left intact.
- `/D_WINRT_DLL` enables the compiler/runtime's actual DLL factory map and activation exports. Earlier probe observations about `/ZW` alone do not imply this macro-enabled component needs hand-authored factory interfaces.
- Compiler map entries can exist for internal-constructor types despite absent `activatable` metadata. The public entry point rejects those direct requests.

## Verification

All commands succeeded on 2026-10-08 with MSVC 14.44.35207 / SDK 10.0.19041.0:

```powershell
.\Tools\uwp\build\build-arm32-cx-component.ps1 `
  -OutDir "$env:TEMP\opencode\m10-cx-component-51-arm-v8"
.\Tools\uwp\build\test-cx-component-contract.ps1 `
  -OutDir "$env:TEMP\opencode\m10-cx-component-51-host-v5"
```

- ARM `/W4 /WX` compilation and full DLL/WinMD link: pass.
- ARMNT/AppContainer image and both activation/lifetime exports: pass.
- Metadata: exact URI/property/method/event/option/payload signatures, eight runtime classes, four exact enums including `Remote=0`/`HostHtml=1`, named default interface, new IID, hidden internal members, no public token/event activation and expected authored filenames: pass.
- External WinMD-only consumer: pass. Four negative modes fail for the intended private-constructor/missing-setter errors.
- x64 runtime factories/projected defaults: **19 checks, zero failures**. Includes real options activation, generated options-constructor and static interface QI, internal-class rejection, residence and a valid `RPC_E_WRONG_THREAD` construction failure without partial output in a non-XAML host.

ARM payload SHA256:

- `WebKit.dll`: `1552694338C86221C735E551E5311ADB4A84F69979FCC10821BBA3A3EE695441`
- `WebKit.winmd`: `4AFC9E0093F7DC565327A506D0D29A1B675722B39E7310E46AE3DCFFB4515950`

Generated IDL, build identity and negative compile diagnostics remain in the respective output directories. Navigation/init wiring, compiled-XAML integration and new-control phone behavior are subsequent tasks, not this acceptance claim.
