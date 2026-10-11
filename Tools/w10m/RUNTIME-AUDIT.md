# ARM-UWP runtime audit — 2026-10-01

## Verified compiler contracts and corrections

`toolchain/runtime-abi-probe.cpp` was compiled to LLVM IR and Thumb assembly with the same x64-host Clang 19.1.5 and ARM MSVC target used for the engine. The probe establishes contracts that symbol-name guesses had previously missed:

- `_Init_thread_epoch` is an `int` TLS variable, not a function. `_Init_thread_header` and `_Init_thread_footer` return void. The guard protocol uses zero for uninitialized, minus one for in-progress initialization, and completed negative epochs. The implementation now serializes competing initializers with an SRW lock and condition variable.
- `__tls_guard` is a TLS byte. `__dyn_tls_on_demand_init` takes no arguments and returns void. Entries in `.CRT$XDU` are no-argument initializer functions.
- `__tlregdtor` returns int and receives a single no-argument destructor trampoline. It does not receive an object pointer. The new TLS-backed destructor list replaces the inferred two-argument FLS implementation.
- `__stou64(float)` converts single precision to unsigned 64-bit, and `__u64tos(unsigned long long)` converts unsigned 64-bit to single precision. Their prior string conversion implementations were incorrect.
- ARM MSVC division helpers receive the denominator first. They return both quotient and remainder: r0/r1 and r2/r3 for 64-bit, r0 and r1 for 32-bit. `crdiv.S` implements this register ABI by calling compiler-rt divmod helpers. Ordinary C quotient-only wrappers were incorrect.

`crtls.cpp` defines the ARM linker-recognized `_tls_used`, real TLS section bounds, `_tls_index`, and a callback array. Dynamic initialization runs on native thread attach and on demand for threads that existed before DLL loading. The DLL startup explicitly initializes the loading thread after global constructors. TLS destruction runs at thread detach and for the unloading thread at process detach. `.CRT$XCA` and `.CRT$XCZ` now bracket real global initializer sections instead of unsectioned arrays.

Mimalloc's ARM callback directives now use undecorated ARM symbols instead of x86-decorated names. Its TLS callbacks remain in `.CRT$XLB`/`.CRT$XLY`; dummy callback symbols are removed. The AppContainer path declines privileged large-page allocation rather than attempting OpenProcessToken/LookupPrivilegeValue/AdjustTokenPrivileges.

The filesystem supplement also corrects `_All_data` to 0x3E, uses the proper MOVEFILE_REPLACE_EXISTING constant, and preserves the exact requested access mask rather than inadvertently escalating FILE_READ_ATTRIBUTES to generic write. These are initial corrections; filesystem semantics are not yet fully audited.

## Build and binary evidence

The runtime archive rebuilt successfully and the full engine build completed at `[7528/7528] Linking CXX shared library bin\WebCore.dll`. WebCore, JavaScriptCore, libEGL, and libGLESv2 now have nonzero TLS directories and callback addresses. The inspected TLS template sizes are 168, 168, 152, and 180 bytes respectively. Their architecture remains ARMNT and their AppContainer flags remain present.

`audit-dlls.py` recursively inventoried 26 local DLLs from WebCore through the ARM dependency prefix. All were ARMNT; the ICU data DLL lacked the AppContainer flag while the other inspected DLLs had it. The rebuilt engine no longer directly imports the mimalloc token-privilege functions. Remaining review candidates include legacy console/module/version APIs and filesystem entry points. The inventory identifies review candidates, not an SDK/device legality verdict. System/framework dependencies, including MSVCP140.dll, VCRUNTIME140.dll, D3DCOMPILER_47.dll, and API-set DLLs, must still resolve on the phone.

The isolated probe DLL and native WinRT host in `RuntimeSmoke/` compile and link without `/FORCE:MULTIPLE`. `WebKitRuntimeSmoke.dll` exports `WKRunRuntimeSmoke` and has a populated TLS directory. The app exercises native thread creation/teardown, per-thread object and POD state, constructor contention, global initialization, integer division/remainder, and float conversions, then attempts JavaScript evaluation and WebCore DLL loading. MakeAppx successfully validated and packed the unsigned package at `build-arm-runtime-smoke/WebKitRuntimeSmoke.appx`, containing 28 ARM binaries. See `RuntimeSmoke/README.md` for build commands and failure-mask meanings.

## Remaining work

- Rebuild the installed third-party DLLs with the new embedded runtime. Their current binaries predate these fixes; zero TLS directories are expected for C-only modules but do not establish correct startup or arithmetic helpers. The staged package is useful for the isolated probe and exploratory loader diagnostics, not authoritative full-engine verification.
- Finish startup/termination review: C initializer sections, failed DLL attach cleanup, EXE startup wrappers, atexit capacity/concurrency, fixed security cookie, allocation failure/alignment, locale facet ownership, and `_onexit` function-type handling remain unresolved.
- Finish filesystem ABI and behavior review, including symlink following, requested/available stat fields, copy/update races, reparse names, final-path flags, and UTF conversion behavior.
- Resolve duplicate exported runtime aliases in the engine/dependency chain; the main engine toolchain still uses `/FORCE:MULTIPLE`. Merely sharing TLS metadata or compiler guard data across DLLs is not a valid fix.
- Expand on-device validation beyond the smoke test to filesystem behavior, multiple DLL lifecycles, allocator stress, and first-page/font/GPU rendering.

## On-device smoke verification

The signed diagnostic package version 1.0.0.2 installed and ran on the connected Lumia 950 XL (RM-1085), stock OS `15254.603.armfre.feature2_rs3svc.200106-1633`. Device Portal returned successful deployment status and supplied the app's `LocalState/runtime-smoke.txt` with a zero CRT/TLS failure mask, successful JavaScript evaluation (expected 4950), and successful WebCore DLL loading. The log is preserved in `openspec/changes/wkview-m7-uwp-fonts/runtime-smoke-lumia950xl.txt`.

The first unsigned deployment failed with 0x800B0100. An app-specific `CN=WebKitWebView` development signing certificate was created in the build user's personal certificate store, and signing resolved deployment. A changed package with the same version was rejected; increasing the package version resolved the update without removing app data. Startup logging now uses the standard `%ls` wide-string specifier and includes a pre-WinRT temporary-log fallback and activation logging. The diagnostic window remains blank by design.

This proves the tested isolated runtime contracts and this package's initial JavaScript execution/WebCore loading on the phone. It does not complete the CRT/STL/import audit or establish page rendering, font output, networking, clipboard, ANGLE rendering, or correctness of third-party DLLs carrying the previous runtime.

## First-page runtime follow-up

The first-page test exposed additional dependency/runtime defects. FreeType returned Array_Too_Large for a valid bundled font until relinked with the corrected runtime helpers. Fontconfig then faulted with exception 0x80000002 at the start of its local strdup wrapper; the dump showed ARM state despite a Thumb function, and lld had reported locally-defined dllimport aliases (LNK4217). Explicit __imp_* pointers in crrt.cpp use function-address relocations that preserve the Thumb bit. Relinking fontconfig with these definitions eliminated those warnings and allowed regular/bold font registration on the phone.

WebCore page creation next faulted at PlatformStrategies::loaderStrategy because the standalone harness had not installed a host strategy provider. The local-only harness now supplies loader/media strategies; external requests are unsupported. The following Cairo fault was resolved in the tested path after rebuilding/relinking the remaining font/painting dependencies with the current runtime: Cairo, HarfBuzz/ICU, and pixman. These artifacts are staged from build-tree overrides, not yet installed as a refreshed complete vcpkg prefix. Cairo's generated RSP link rule was locally adjusted to retain _tls_used; that build-tree adjustment is not a durable port recipe.

Version 1.0.0.9 reached HTML parsing, layout and Cairo painting but its RunAsync handoff returned E_INVALIDARG. Version 1.0.0.10 performs the single snapshot on the owning UI/engine thread and presents directly, preserving the engine thread's lifetime. Device logs report success through D3D11 Present, the process remained running, and the owner confirmed visible page/text/shapes. Evidence: `openspec/changes/wkview-m7-uwp-fonts/first-page-lumia950xl.txt`. This expands verification to the tested static-page font/layout/paint/presentation path; the broader audit and persistent browser host remain open.

## TextureMapper/ANGLE follow-up — 2026-10-02

MiniBrowser 1.0.0.8 crashed in ANGLE's global `std::mutex::lock` on its first EGL platform-display request. The current MSVC headers' constexpr mutex initialization is incompatible with the phone's older inbox `msvcp140.dll`. UWP ANGLE now defines `_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR` so that runtime initializes its own lock storage. MiniBrowser 1.0.0.9 installed and progressed through EGL context creation on the Adreno 430, TextureMapper initialization, and root graphics-layer attachment, confirming that this initialization blocker was passed.

Its first draw then faulted in the D3D matrix-uniform setter. Disassembly establishes a compiler ABI defect: the ARM32 MSVC virtual-member-pointer thunk loads the vtable and target into **r1**, overwriting the first explicit argument (uniform location). The receiver is in r0. The observed location register contained the setter's Thumb function address instead of the valid location. `toolchain/arm-virtual-call-probe.cpp` independently reproduces this with Clang 19.1.5; the direct virtual-call control uses r12 for its target and preserves r1. ANGLE's ARM-UWP Clang uniform dispatch now uses direct virtual calls for scalar/vector and matrix setters. MiniBrowser 1.0.0.10 builds and packages successfully; its device deployment is pending after a portal timeout. This compiler issue may affect other virtual-member-pointer call sites; the workaround is currently scoped to the observed ANGLE uniform path.
