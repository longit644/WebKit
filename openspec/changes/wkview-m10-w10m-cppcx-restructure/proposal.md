# Proposal - M10 W10M C++/CX architecture and restructuring

## Why

The W10M port mixes reusable engine behavior, WRL application composition, prototype WinRT contracts, fixture assertions, and hard-coded build support. The owner has selected C++/CX for the UWP-facing layer and requested a complete naming, source-placement, lifecycle, and build redesign that another model can implement without improvising architectural boundaries.

## What Changes

- Supersede M9's unfinished plan, preserving its implementation and package-specific device evidence; do not archive M9 as completed.
- Author the public `WebKit.WebKitWebView` and Windows 10 Mobile application/OS projections in C++/CX, keeping `WebKitWebView.h/.cpp` directly under `Source/WebKit/UIProcess/API/w10m/`.
- Split native page ownership, DLL binding, rendering, input, dispatcher services, navigation, sessions, and diagnostics into named responsibilities.
- Define a versioned, compiler-neutral C ABI between the exception-disabled native engine and MSVC `/ZW` components. Public events use structured callbacks, not parsed diagnostic logs.
- **BREAKING:** replace the prototype's incomplete contract with URI-valued Source, observable state, unified terminal events, asynchronous initialization, explicit Close, and documented error domains. Publish a contract/metadata revision; do not silently reuse an incompatible IID or ABI layout.
- Make C++/CX declarations authoritative for generated component metadata; separate legacy MIDL contracts and compatibility exports from the new projection.
- Migrate MiniBrowser and diagnostic application projections, extract fixture orchestration into a device runner, and retain Skia and existing WebCore loading/security semantics.
- Establish reproducible native/MSVC build domains, ARM32 runtime/metadata/XAML gates, dependency auditing, and device acceptance before broad migration.
- Resolve independent-review gaps with opaque instance-bound certificate decisions, explicit warning HTML/Source transitions, serialized notifications, independent async joiners, isolated trust/global-font conflict handling and process-pinned engine shutdown.

## Capabilities

### New Capabilities

- `uwp-platform-architecture`: framework/application/test ownership and exhaustive naming/move accounting.
- `uwp-cppcx-component`: C++/CX authoring, metadata identity, activation, and runtime compatibility.
- `uwp-native-engine-bridge`: versioned C ABI, typed per-page notifications, thread affinity, and detachment.
- `uwp-webview-control`: declarative public control, configuration, observable state, initialization, input, rendering, and lifetime.
- `uwp-web-navigation`: stable IDs, source/history/title semantics, cancellation, terminal outcomes, and host HTML.
- `uwp-web-resource-loading`: asynchronous loading, isolated sessions, security, certificate consent, and bounded resource policies.
- `uwp-application-hosting`: component-consuming MiniBrowser and C++/CX diagnostic/test hosts.
- `uwp-build-packaging`: separate compiler domains, generated output placement, reproducible staging, and ARM dependency closure.
- `uwp-device-validation`: replacement fixtures, assertion aggregation, visual/lifecycle evidence, and owner acceptance.

### Modified Capabilities

None. `openspec list --specs` reports no archived specifications. The three overlapping M9 delta capabilities are replaced by M10's authoritative requirements, not synchronized into completed specifications.

## Impact

Affected areas include `Source/WebKit/UIProcess/API/w10m/`, new shared ABI headers in `Source/WebKit/Shared/w10m/`, authored Windows 10 Mobile adapters in WebCore/WTF/PAL, `Tools/MiniBrowser/w10m/`, `Tools/w10m/`, relevant toolchains/projects/staging scripts, and current Windows 10 Mobile documentation. WebCore/WTF/JavaScriptCore algorithms, curl, Skia, DirectX COM interfaces, and third-party code retain native types and appropriate compiler settings.

## Non-goals

No whole-engine `/ZW` conversion, third-party renaming, WebView2 compatibility, JIT/multiprocess promise, or automatic claim of universal website support. Host script execution, persistent/shared profiles, and permission/download/new-window deferrals remain future capabilities. This proposal creates planning artifacts only; all implementation and device gates remain unchecked.
