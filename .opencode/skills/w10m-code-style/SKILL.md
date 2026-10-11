---
name: w10m-code-style
description: Use when editing or reviewing this repository's Windows 10 Mobile/W10M port, including Source/WebCore/platform/w10m, Source/WebKit/UIProcess/API/w10m, Tools/MiniBrowser/w10m/csharp, Tools/w10m/toolchain, WinRT IDL, or WebKitWebViewComponent.
---

# WebKit Windows 10 Mobile coding conventions

Use this skill for Windows 10 Mobile implementation, toolchain, MiniBrowser, WinRT component, and related probe changes in this repository. UWP remains the underlying Windows app model and API surface. Read the surrounding code and preserve existing behavior unless the task explicitly requests a behavior change.

## Documentation and spacing

- Add a concise, why-focused `//` doc comment for every authored class, struct, enum, method, field, callback, and non-obvious constant. For public contracts, document each method and each enum value individually.
- Put a blank line before a symbol's doc comment and between adjacent symbols, including the end of a method/class and the next comment, `extern "C"` block, namespace, or declaration. Keep a doc comment immediately above its symbol.
- Comments explain behavior, ownership, ABI meaning, limits, or why a choice is required. Avoid restating the identifier or adding comments to obvious syntax.
- Keep class-level comments above the class declaration. For IDL, document the runtimeclass/interface and every property, method, event pair, delegate and enum value.
- In C and assembly shims, document ABI entry points and explain register, layout, or sentinel values at the point of use. In Python and PowerShell helpers, use concise docstrings for modules, public functions, and non-obvious helpers.

## Constants and error values

- Replace unexplained numeric literals with named constants or enums, documenting every enum value. Keep ABI-mandated numeric values explicit and stable.
- Use existing semantic constants first: `CURLE_*` for curl transport errors, `httpStatus*` from `HTTPStatusCodes.h` for HTTP response statuses, Win32 constants for Win32 flags/errors, and the UWP bridge result/error enums for adapter-local values.
- Do not confuse curl error codes with `UWPNetwork` adapter error codes or `WKPageResultUWP` HRESULT bridge codes. Keep each domain named at its source.
- For booleans written to diagnostic logs, use `WKPageLogTrueUWP` and `WKPageLogFalseUWP`; distinguish diagnostic success values from boolean values.
- Keep temporary test phase numbers named and documented, especially when they are part of the device-log protocol.

## UI, HTML and application customization

- Do not build the MiniBrowser blocked/error page as an inline HTML string. Use `Tools/MiniBrowser/w10m/csharp/Assets/blocked.html`, HTML-escape values before token substitution, and keep an embedded fallback only for missing/unreadable files.
- Resolve the blocked page in this order: explicit host path, app `LocalState/blocked.html`, packaged `blocked.html`, embedded fallback. Hosts that use `WebKit.WebKitWebView` may instead handle `NavigationFailed` and load their own page through `NavigateToString`.
- Include the consented proceed markup only for certificate-verification failures. Do not offer a proceed/bypass action for DNS, connection, policy, or other errors.
- Keep demo pages and navigation fixtures as clearly identified test content; do not use them as a substitute for application UI or error handling.

## WinRT and generated files

- Preserve the acronym `CX` in authored names for C++/CX code, namespaces, types, project names and metadata/resource names: use `CXCompileProbe`, not `CxCompileProbe`. Lowercase file/script slugs such as `cx-probe` may remain lowercase. Regenerate derived XAML/WinMD outputs after renaming their authored inputs; do not hand-edit generated files or rewrite historical package evidence.

- `Tools/w10m/CXComponent/WebKitWebView.idl` is the source of truth for the WinRT contract and its documentation. Regenerate `.winmd` and MIDL outputs using the durable recipe in `Tools/w10m/CXComponent/README.md`. Do not manually edit generated MIDL outputs: changes will be overwritten.
- The component uses C++/CX, not C++/WinRT: the SDK's C++/WinRT base header rejects 32-bit ARM. Authored public control sources live in `Source/WebKit/UIProcess/API/w10m/`; `Tools/w10m/CXComponent/` holds only the prototype being replaced, the browser-free gate probe, and build support. Generate nothing into the source tree.
- `/ZW` does not generate the activation entry point. A `/ZW` component has no `DllGetActivationFactory` and no `/EXPORT` directive, so the link must pass `/export:DllGetActivationFactory` explicitly and the activation factory is hand-written. Never feed an IDL-generated WinMD back into the `/ZW` compilation; the header's `runtimeclass` collides with the CX `ref class` (C2011/C3094).
- Put WRL implementation details and their documentation in `WebKitWebViewComponent.cpp`; use the minimal async declarations in `WebKitWinRTBase.h` only where the installed SDK projection is unavailable.
- Preserve ABI names, IIDs, enum numerics, threading/marshaling metadata and exports unless the task explicitly changes the public contract.
- For C# Windows 10 Mobile consumers, use XML `///` documentation on public APIs, `x:Bind` for view bindings, and the repository's MVVM/dependency-injection patterns.

## Formatting and validation

- Run `clang-format --style=file` on authored `.cpp` and `.h` files after edits. The root `.clang-format` is based on WebKit style; class and enum opening braces stay on the declaration line. Preserve deliberate blank lines around documentation comments after formatting.
- Do not run clang-format on generated MIDL output, `.idl`, `.c`, CMake, PowerShell, Python, shell, or assembly files.
- Before compiling, audit documentation and spacing with `python Tools/w10m/audit-source-docs.py`; inspect every remaining diagnostic.
- Validate fixture behavior with `python test_server.py` from `Tools/w10m/NavigationTests` when fixture code changes.
- Build only the affected Windows 10 Mobile targets after the full review. For ARM32, configure/build with the repository's `Tools/w10m/arm32-w10m-env.ps1` and use named targets (for example `WebCore` or `JavaScriptCore`) rather than the unsupported all-target gtest build.
- Do not commit, amend, push, or create a PR unless explicitly requested.
