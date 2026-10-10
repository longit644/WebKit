# FpuProbe: C# host over the native FPU/ARM payload (M10 task 8.6)

Isolates ARM32 `setjmp`/`longjmp` and the Windows CRT frame argument on the
phone. `0xc0000028` is `STATUS_BAD_STACK`, not a floating-point exception.
The application host (Application/activation/window/launch) is C#; the probe
work itself stays native and behavior-identical to the pre-migration host.

## Layout

| Path | Role |
|---|---|
| `payload/FpuProbe.cpp` | Native payload: log protocol, CRT frame/integer/FP probes, `WKFpuProbeRun` entry |
| `payload/FpuProbePayload.h` | Payload contract: retained constants, `wkProbe*` declarations, derived expectations |
| `payload/FpuProbePayload.def` | Exported surface: `WKFpuProbeRun`, `wkProbeSetjmpSp`, `wkProbeSetjmpR11`, `wkProbeCrtSetjmp` |
| `payload/CrtFrameProbe.S` | Unchanged: SP and R11 frame-argument adapters tail-calling the real CRT |
| `host/` | C# host: `App`, `MainPage`, `NativeMethods` (P/Invoke), manifest, assets |
| `scripts/build-fpuprobe.ps1` | New build/stage/package script (shared scripts untouched) |
| `scripts/check-fpuprobe-expectations.ps1` | Host-side expectation proof (inventory + run + ARM inspection) |
| `scripts/FpuProbeHostCheck.cpp` | Host-runnable arithmetic proof (plain x64 C++, no ARM assembly) |
| `CMakeLists.txt` | Builds the ARM `FpuProbePayload.dll` only; no host executable |

`Tools/uwp/FpuProbe/AppxManifest.xml` (native host, identity 1.0.0.11,
EntryPoint `WebKitFpuProbe.ViewSource`) was deleted; `host/Package.appxmanifest`
(identity 1.0.0.12, EntryPoint `WebKitFpuProbe.App`) replaces it. The whole
tree moved from `Tools/uwp/FpuProbe/` to `Tools/uwp/diagnostics/FpuProbe/`
per the migration map, matching the sibling diagnostic migrations.

## Retained expected results (pre-migration inventory, unchanged)

Constants: `kLongjmpReturnValue = 7`, `kCrtProbeReturn = 11`,
`kIntProbeReturn = 9`, `kFpscrTrapEnableMask = 0x1f00`, operands
2.0/4.0 with 1.0/2.0/3.0/5.0 FP chain, scale 1000, integer 1+2 then +4.

Derived report values in `LocalState\fpuprobe.log` (`stage: 0x%08lx`):

| Stage | Expected |
|---|---|
| `probe-baseline-multiply` | `0x1F40` (8000) |
| `probe-inside-before-longjmp` | `0x1770` (6000) |
| `probe-fp-result-x1000` | `0x7530` (30000) |
| `probe-int-before-longjmp` / `probe-int-value` | 7 |
| `probe-int-setjmp-returned` then `probe-int-after-longjmp` | 0 then 9 |
| `probe-crt-sp-return` | 0 then 11 |
| `probe-crt-sp-survived`, `probe-log-opened`, `probe-run-entered`, `fpuprobe-completed` | 1 |
| `fpuprobe-startup` | 0 (`S_OK`) |
| `probe-fpscr-trap-bits` | 0 (user mode reads FPSCR but cannot set trap bits) |
| `probe-setjmp-returned` then `probe-after-longjmp` | 0 then 7 |

Frame variants: `wkProbeSetjmpSp` (SP, executed) and `wkProbeSetjmpR11` (R11,
kept available; the R11 call crashes as reproduced in 1.0.0.10 and is still
not executed). Retired with the WRL host: `probe-activate-hresult`,
`probe-skipping-fault-handler`, the `CoreApplication::Run` event loop and
`ExitProcess`; the C# host owns lifetime and verifies the same log.

## Build and package (host machine only, fresh `%TEMP%\opencode` outputs)

From the repository root in Windows PowerShell:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\uwp\diagnostics\FpuProbe\scripts\build-fpuprobe.ps1 -OutDir "$env:TEMP\opencode\m10-fpuprobe-86"
```

Uses `--parallel 4`; resolves MSVC 14.44.35207, SDK 10.0.19041.0 and the
Community 2022 MSBuild via the shared discovery files (read-only).
Expects: warning-free ARM payload DLL (`IMAGE_FILE_MACHINE_ARMNT`,
`APPCONTAINER`, four `wkProbe*`/`WKFpuProbeRun` exports), host arithmetic
check 7/7 PASS, warning-free C# ARM Release with .NET Native engaged,
packed `FpuProbe_1.0.0.12_ARM.appx` signed with `CN=WebKitWebView` and
verifying with `signtool verify /pa`. `-SkipSign` is build-only.

Expectation proof alone:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\uwp\diagnostics\FpuProbe\scripts\check-fpuprobe-expectations.ps1 -OutDir "$env:TEMP\opencode\m10-fpuprobe-86-check"
```

## DEVICE RUNBOOK (coordinator only; not executed here)

Package identity: `WebKitWebView.FpuProbe_1.0.0.12_arm__2932e6rj4m41p`.
App ID: `WebKitWebView.FpuProbe_2932e6rj4m41p!App`.

```powershell
python Tools/uwp/device-portal.py --url https://<phone> --insecure install <OutDir>\package\FpuProbe_1.0.0.12_ARM.appx
python Tools/uwp/device-portal.py --url https://<phone> --insecure launch 'WebKitWebView.FpuProbe_2932e6rj4m41p!App' 'WebKitWebView.FpuProbe_1.0.0.12_arm__2932e6rj4m41p'
python Tools/uwp/device-portal.py --url https://<phone> --insecure get '/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=WebKitWebView.FpuProbe_1.0.0.12_arm__2932e6rj4m41p&path=%5CLocalState&filename=fpuprobe.log'
python Tools/uwp/device-portal.py --url https://<phone> --insecure get '/api/filesystem/apps/file?knownfolderid=LocalAppData&packagefullname=WebKitWebView.FpuProbe_1.0.0.12_arm__2932e6rj4m41p&path=%5CLocalState&filename=fpuprobe-csharp.txt'
```

Expected device results: `fpuprobe-csharp.txt` reports `version=1.0.0.12`,
`result=PASS`, `failures=0` (21 host checks: window/page/run/completion plus
the 17 report assertions above); `fpuprobe.log` carries the native lines
with the exact values in the table. Increase the manifest version before any
changed-content redeploy; the phone rejects a different package under the
same identity/version.
