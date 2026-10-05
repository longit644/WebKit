# M10 build and packaging contract

Companion to [../design.md](../design.md), [native-abi.md](native-abi.md) and
[acceptance-matrix.md](acceptance-matrix.md).

## Build domains

Native engine: existing CMake/Ninja native compiler configuration, exception/RTTI
settings and WebCore/JSC targets. CX: MSVC ARM32 UWP projects with `/ZW`, compatible
language standard, exceptions and the validated UWP C/C++ runtime. CX projects
must not import engine-global /EHs-c-/_HAS_EXCEPTIONS=0 or fake RTTI/EH support.
Native helper libraries without projections can remain native; all public OS/app
projection targets use the CX domain. Test/probe payloads retain native compilers.

Projects/props/orchestration live in Tools/uwp/WinRTComponent and build/; source
stays in API/uwp. MiniBrowser/diagnostic projects reference the component, not its
implementation .cpp files. Use separate build directories, initially
build-arm-uwp-skia (engine), build-arm-uwp-component (CX), build-arm-minibrowser
(app), and build-arm-uwp-tests (probes/runner). Shared outputs are explicit staged
inputs, never accidental PATH/library search resolution.

## Discovery and identity

Detect VS via vswhere, SDK tools/headers/metadata via installation paths and
explicit version override. Require ARM32 um/ucrt/runtime libraries and CX
platform.winmd references; do not infer support from a checked installer workload.
Initial SDK candidate is Windows 10 10.0.19041.0. Record exact chosen compiler,
SDK, Windows target/minimum phone build, compiler flags, runtime libraries,
engine ABI/build ID, component contract revision, WinMD/XAML provider paths,
asset hashes, package identity/version and signing identity fingerprint.

Also record the SSL backend and proof that CA-file-only bundle trust is actually
applied on Windows without native-store augmentation; no singleton mutation or
cross-session TLS/connection-cache reuse. Record canonical global font fingerprint
and explicit conflict handling. Successful asset staging is not trust/font proof.

Record shell discovery output and full exit codes. No C:\Users-specific paths
or short-name assumptions in new checked-in configuration. Fail before compilation
on absent or wrong-architecture libraries. Configuration support is determined by
actual runtime closure; unsupported Debug cannot silently become Release.

## Gate 1: complete browser-free component

Compile, link and package a CX UserControl with generated WinMD/activation, a
visible child, an identity property and a bindable Source-like property. Inspect
ABI interfaces/class base and test compiled XAML plus the sample's runtime markup
path if used. Use the actual XAML metadata provider produced by the SDK build;
do not substitute successful RoActivateInstance for markup construction.

Audit imports for vccorlib, CRT, EH helpers, OS contracts and architecture. Verify
supported runtime package deployment on the phone, constructor exception-to-HRESULT,
reference count/lifetime, init/startup and final-release behavior. If existing
arm-crtstart/native shims are proposed, first demonstrate these guarantees; no
automatic inclusion of the prototype's link recipe. A missing supported runtime
is a gate blocker, not permission for dummy EH/RTDynamicCast implementations.

After successful binding, engine DLL code is process-pinned in M10; hot-unload
is deferred. This does not waive per-page cleanup: audit normal close/recreate
counts and drain accepted callbacks/platform requests/global hooks before orderly
dispatcher shutdown. Document terminal OS-thread-loss quarantine separately.

## Metadata source of truth

CX declarations generate the component WinMD, activation and XAML glue. New public
signature changes receive a documented metadata contract revision/new IID as
needed. The public runtime class name remains WebKit.WebKitWebView. Emit
`WebKit.dll` / `WebKit.winmd` under build output from target WebKitUWPComponent;
the metadata output root must match the public WebKit namespace under CX rules.
Generate matching consumer references and inspect dependency-property/event/interface/base
metadata. Legacy WebKitWebView.idl/MIDL products are migration references only;
never stage a conflicting independently generated WinMD under the new identity.

Generated headers/cpp/object/PDB/WinMD/XAML glue remain under build outputs.
Public authored headers have stable source names. Stage matching engine+component+
app and explicit font/CA/HTML assets. No app worker downloads remote HTML.

### Where CX declarations end and MIDL begins, as measured

The rule above says CX declarations generate the WinMD. That is not literally
what the toolchain does, and the difference matters because getting it wrong
produces a component that links and never activates. All of the following was
measured on this toolchain, not inferred:

- `/ZW` alone does **not** generate the activation entry point. A component
  compiled with `/ZW` and nothing else has no `DllGetActivationFactory` symbol and
  no `/EXPORT` directive, and the resulting DLL exports nothing.
- Activation therefore comes from two sources that are both load-bearing: the
  hand-written WRL `IActivationFactory` in the component source, and the explicit
  `/export:DllGetActivationFactory` link flag. Removing either yields a DLL that
  builds cleanly but cannot be activated.
- The MIDL descriptor is the metadata source, declaring the runtime class, its
  default interface, its base and its threading model. It is not optional
  decoration and not superseded by CX declarations.
- The CX compilation must **not** consume the IDL-generated WinMD. Compiling
  against the generated header fails with `C2011` redefinition and `C3094`,
  because the header's `runtimeclass` collides with the CX `ref class` of the same
  name. The two describe one contract from two directions and must be combined by
  the build, never by feeding one to the other.
- The interface UUID must be declared identically in both places. The CX
  `public interface class` UUID and the IDL `uuid` are currently the same value;
  a mismatch would produce a contract that appears to build but exposes the wrong
  IID.

`Tools/uwp/build/build-arm32-cx-probe.ps1` encodes this arrangement and verifies
the result: it compiles with `/ZW`, generates the WinMD, links against the Store
runtime flavor, and then checks the produced binary is COFF-ARM, carries
`IMAGE_DLL_CHARACTERISTICS_APPCONTAINER`, and exports
`DllGetActivationFactory`. A component that fails only the activation check still
links with exit code 0, so the script asserts on the binary rather than trusting
the link.

### Tool invocation constraints worth preserving

Each of these was a failure before it was a rule:

- `/AI` names the **directory** holding an assembly. Passing the `platform.winmd`
  file itself reports `C1107: could not find assembly 'platform.winmd'`, which
  reads as a missing reference when the reference is present.
- `midlrt` needs a host `cl.exe` on `PATH` for its preprocessor (`MIDL1005`), and
  resolves the IDL's imports only from `Include/<version>/winrt`.
- `midlrt /metadata_dir` must point at `UnionMetadata`. The `References/` tree
  holds split per-contract winmds with no union `Windows.winmd` (`MIDL4034`).
- `midlrt /winrt` writes output relative to the current directory and rejects an
  absolute `/out` path (`MIDL2212`), so the build must run in the output
  directory rather than naming a destination.
- These SDK tools write banners to stderr. Under a fail-fast script that becomes
  a `NativeCommandError` on a healthy run, so the tool's exit code must decide
  success.
- The ARM32 component links the **Store** flavor of the MSVC runtime
  (`lib/arm/store`), not the desktop flavor. The desktop `libvcruntime` defines
  `__vcrt_initialize` but also references desktop-only EH and PAL symbols
  (`RtlUnwindEx`, `__GSHandlerCheck`, `_fltused`) that an AppContainer cannot
  resolve. `msvcurt.lib` is absent from every installed ARM toolset and is not
  required.

## Packaging gates

Staging checks ABI/build ID, component metadata revision, ARM machine on every
binary, recursive imports, required assets and USE_SKIA identity. Reject a Cairo
MiniBrowser engine, ARM64 dependency or incompatible engine before makeappx.
Sign/install only using explicit package identity and configured certificate;
no secrets go into evidence. Capture fresh phone logs under a unique run ID.

Native builds use named targets with eight parallel jobs; CX uses validated
MSBuild project/configuration/platform arguments. Document exact reproducible
commands after discovery (do not invent untested link flags). Verification is
per affected slice. Preserve previous package and record rollback mapping.
