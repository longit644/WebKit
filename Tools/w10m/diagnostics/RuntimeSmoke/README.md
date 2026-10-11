# RuntimeSmoke — C# application host over the unchanged native payload

M10 task 8.5. The application host (activation, window, launch and result
sequencing) is C#/XAML in `host/`; the native payload (CRT probe, JavaScript
evaluation, WebCore load and first-page snapshot driver) stays native in
`payload/` and is behavior-identical to the retired `SmokeHost.cpp` flow.
`stage-app.py` remains shared at `Tools/w10m/RuntimeSmoke/`; this app reuses
it through `scripts/stage-runtimesmoke.py`.

## Layout

- `payload/RuntimeSmoke.cpp` — CRT/TLS probe exporting `WKRunRuntimeSmoke`
  (moved byte-identical from `Tools/w10m/RuntimeSmoke/`).
- `payload/SmokeDiagnostics.{h,cpp}` — first-page diagnostics driver moved out
  of the retired host: JavaScriptCore evaluation, WebCore load, snapshot
  render and D3D11 presentation, exposed as `WKRunSmokeDiagnostics`. It calls
  the engine `WKRenderFirstPageUWP` export through the declared
  `WKRenderFirstPageUWPType` instead of an inline `reinterpret_cast`.
- `host/` — C# UWP app (`WebKitRuntimeSmokeHost.exe`, class
  `WebKitWebView.RuntimeSmoke.App`). Project shape, targeting
  (10.0.19041.0 / MinVersion 10.0.15063.0), `PhoneIdentity`, exact-size
  logos and the nuget.org feed pin follow the proven C# proof project
  verbatim; `Microsoft.NETCore.UniversalWindowsPlatform` is 6.0.15 (never
  6.2.x, which cannot install on the phone).
- `scripts/` — new build/compare/stage tooling for this app only. Shared
  build scripts are not modified by this migration except for moved-path
  reference updates (see below).

## Expected native results (must stay byte-identical)

`scripts/compare-runtimesmoke-semantics.py` verifies every value below
against the pre-migration sources on the host.

- Failure mask `runtime-smoke-failure-mask: 0x00000000` is required. Bits:
  1 global init ran other than exactly once; 2 TLS/POD canary mismatch;
  4 function-static value wrong; 8 TLS writeback lost; 16
  division/remainder/float conversion wrong; 32 thread/event spawn failed;
  64 TLS construct/destroy count mismatch; 128 contended static init ran an
  incorrect number of times.
- CRT canaries: TLS value 73, POD value 19, function-static value 91,
  4 workers with IDs from 100, single-construction expectation 1,
  contention delays 20 ms (static guard) and 10 ms (TLS writeback).
- Arithmetic: -100000000007 / 97 = -1030927835 rem -12 (signed 64),
  100000000007 / 97 = 1030927835 rem 12 (unsigned 64), -1007 / 17 = -59
  rem -4 (signed 32), 1007 / 17 = 59 rem 4 (unsigned 32), and the exact
  4294967296.0f <-> 4294967296 round trip.
- JavaScript evaluation must be 4950 (sum of 0..99 over 100 loop
  iterations, source line 1); `javascript-evaluation: 0x00000000`.
- WebCore load: `load-webcore: 0x00000000`; render `page-render-result:
  0x00000000`; D3D11 `page-d3d11-initialize`/`page-d3d11-present:
  0x00000000`.
- Engine snapshot stages (emitted by the unchanged `FirstPageW10M.cpp` /
  `EngineW10M.cpp` through the same log sink) keep their names and order;
  fontconfig boolean steps report 1 for success.

## Line-protocol mapping (host lines only)

Payload lines are unchanged. Host lines changed with the application model:

| Retired native host line | C# host line | Note |
| --- | --- | --- |
| `entrypoint-reached`, `initialize-main-apartment`, `core-application-factory`, `core-application-run-returned` | — | Native CoreApplication/ViewSource model retired with `SmokeHost.cpp` |
| `view-activated` | `window-activated` | Same meaning: window ready before diagnostics |
| `open-log`, `host-started`, `page-viewport-width`, `page-viewport-height` | identical | Same names, same `0x%08lx` format |
| — | `diagnostics-returned` | New: native entry return value (0 required) |
| — | `host-pinvoke` | New: only on managed interop failure |

The historical `worker-started` payload label is retained; since version
1.0.0.10 the sequence runs on the owning UI/engine thread.

## Build, stage and package

All outputs go under `%TEMP%\opencode`; shared build trees are only read. Use
the PowerShell driver to discover/configure the ARM toolchain and keep every
build output fresh. Its output parameters are `PayloadDir`, `HostDir`,
`HostObjDir`, and `AppxDir` (there is no `OutDir` parameter).

```powershell
$tag = 'm10-85-<unique-run-tag>'
$payload = Join-Path $env:TEMP "opencode\$tag-payload"
$hostOutput = Join-Path $env:TEMP "opencode\$tag-host"
$hostObj = Join-Path $env:TEMP "opencode\$tag-host-obj"
$appxDir = Join-Path $env:TEMP "opencode\$tag-apppackages"
$stage = Join-Path $env:TEMP "opencode\$tag-stage"
$appx = Join-Path $env:TEMP "opencode\$tag-RuntimeSmoke.appx"

# ARM native payload plus ARM Release C# host (.NET Native), into fresh dirs.
& Tools/w10m/diagnostics/RuntimeSmoke/scripts/build-runtimesmoke.ps1 `
  -PayloadDir $payload -HostDir $hostOutput -HostObjDir $hostObj -AppxDir $appxDir

# Semantic proof against pre-migration HEAD, plus authored C/C++ docs audit.
python Tools/w10m/diagnostics/RuntimeSmoke/scripts/compare-runtimesmoke-semantics.py
python Tools/w10m/audit-source-docs.py --check `
  Tools/w10m/diagnostics/RuntimeSmoke/payload/SmokeDiagnostics.cpp `
  Tools/w10m/diagnostics/RuntimeSmoke/payload/SmokeDiagnostics.h

# Stage the signable package layout from the actual MSBuild test appx.
python Tools/w10m/diagnostics/RuntimeSmoke/scripts/stage-runtimesmoke.py `
  --host-appx "$appxDir\RuntimeSmoke_1.0.0.11_ARM_Test\RuntimeSmoke_1.0.0.11_ARM.appx" `
  --payload-build $payload `
  --engine build-arm-w10m-skia/bin --prefix C:/vcpkg/installed/arm-w10m-webkit `
  --stage $stage `
  --reader "C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/Llvm/x64/bin/llvm-readobj.exe"
& "C:/Program Files (x86)/Windows Kits/10/bin/10.0.19041.0/x64/makeappx.exe" `
  pack /d $stage /p $appx /o
```

The source C# XML documentation follows the `Tools/MiniBrowser/w10m/csharp` style:
types and members have `<summary>` elements, and documented parameters/return
values use `<param>`/`<returns>`. MSBuild's ARM Release/.NET Native build is
the compile check; the source-docs audit covers the newly authored C/C++ files
and intentionally omits the byte-identical moved `RuntimeSmoke.cpp` payload.

## Device run (coordinator)

The package identity is `WebKitWebView.RuntimeSmoke_1.0.0.11_arm__<publisher>`.
Sign with the `CN=WebKitWebView` development certificate, install the
certificate plus the ARM `Microsoft.VCLibs.140.00` and .NET Native
frameworks, deploy through Device Portal, launch
`WebKitWebView.RuntimeSmoke_<publisher>!App`, and fetch
`LocalState/runtime-smoke.txt`. Expect a zero failure mask,
`javascript-evaluation: 0x00000000` (4950), `load-webcore: 0x00000000`,
`page-render-result: 0x00000000` and D3D11 present success. Increase the
manifest version for every changed repackage; the phone rejects a different
package with the same identity/version.

## Consumer reference updates done by this move

- `Tools/w10m/build/check-wkw10m-legacy-surface.ps1` — smoke-surface list
  follows the moved payload; the retired host byte-identity check is
  replaced by the semantic comparison above.
- `Tools/w10m/build/stage-engine-regression.py` — runtime manifest path
  follows the new `host/Package.appxmanifest` (host binary name unchanged).
- `Tools/w10m/ARCHITECTURE.md`, `Source/WebCore/platform/w10m/EngineW10M.h`
  and `LegacyExportMapW10M.h` — prose follows the new location.
- `stage-app.py` stays shared at `Tools/w10m/RuntimeSmoke/` (also used by
  MiniBrowser); its `--host` default still names `WebKitRuntimeSmokeHost.exe`.
