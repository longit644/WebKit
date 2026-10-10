# M10 7.1: dispatcher hop and neutral clipboard service

## Ownership and service contract

`platform/ClipboardServiceUWP.h/.cpp` is the host-side V1 clipboard service with
neutral callbacks and no app objects. Exactly-once completion holds across
acceptance, transport failure and cancellation; a canceled operation completes
Closed synchronously. Pending operation IDs are unique and reusable after
completion. The service compiles without C++ exception unwinding on ARM and its
transports must not throw.

`platform/DispatcherUWP.h/.cpp` is the private owner-thread dispatcher hop: an
internal-only sealed ref class that reports `HasThreadAccess` and enqueues one
`DispatchedHandler` with the originating HRESULT preserved. It is never
constructed from XAML and contributes no WinMD surface of its own.

`platform/ClipboardPlatformUWP.h/.cpp` is the CX projection boundary: a WinRT
text transport (`DataPackage` publish, `GetTextAsync` read) and a
`CoreDispatcher` port behind `IServiceDispatcherUWP`. A clipboard without text
completes OK with an empty span, because positive HRESULTs other than S_OK are
contract result codes and S_FALSE must never travel the completion. UTF-8 is
validated strictly in both directions, including overlong, surrogate and range
rejection.

Request failures keep their originating value: S_OK becomes OK and negative
results pass through unchanged, which the frozen contract defines as an
originating HRESULT. Retain/release routing returns void per the contract, so a
null context is ignored. `postWake` stays null until the 7.5 runtime owner
exists; `buildTable` documents that scope. Capability mask remains **0**; no
clipboard bit is advertised until the runtime owner wires the table in 7.5.

## Checks and commands

On 2026-10-10, PowerShell 7.6.6, MSVC 14.44.35207, SDK 10.0.19041.0,
native ARM clang-cl 19.1.5:

```powershell
.\Tools\uwp\build\test-wkuwp-clipboard-service.ps1
```

- ARM32 compile of the neutral service with the native engine compiler, using
  the toolchain flags exactly at `/W4 /WX`: pass with no warnings. No `/EHsc`
  is appended; see the toolchain finding below.
- ARM32 compile of the neutral service with the MSVC ARM32 compiler that owns
  `/ZW`, at `/W4 /WX` without `/ZW`: pass with no warnings. Any ref class, hat
  type or `Windows::` reference would fail here, so this is the neutrality
  proof for the service translation unit.
- ARM32 `/ZW` compile of the dispatcher and WinRT transport with the exact
  component flags (`/ZW /EHsc /MD /std:c++17 /W4 /WX /D_WINRT_DLL`, PC_APP
  family, store references and union metadata): pass with no warnings.
- ARM32 module link of the service object: `COFF-ARM`,
  `IMAGE_FILE_MACHINE_ARMNT`, `APPCONTAINER`.
- Engine purity gate (`check-uwp-engine-purity.py`): **PASS**; the neutral
  service header carries no CX/WinRT token and no engine header includes the
  new dispatcher/service headers.
- Host behaviour from x64 copies of the same sources: **83/83**, covering
  frozen kind values, write OK exactness, failed-write origin preservation,
  kind/completion/text/span/port validation, duplicate-ID rejection with the
  original left pending, ID reuse after completion, same-thread read exactness
  with a readable borrowed span, repeated-completion suppression, cancel-Closed
  semantics with late-completion suppression, worker-thread marshal delivery
  through a captured dispatcher task, refused-enqueue counting with the
  operation left pending until cancel, foreign-thread rejection before any
  argument is consumed, and borrowed-table routing (header, context, thunks,
  null postWake, retain/release counting, null-context handling).

## Toolchain and contract findings

Appending `/EHsc` after the toolchain's `/EHs-c-` trips an LLVM 19.1.5 ARM
backend crash (`Cannot select: cleanupret`) on this translation unit, bisected
to the constructor's `unique_ptr` member initialization under the mixed EH
flags. The service step therefore uses the toolchain flags exactly, which also
matches the real engine build that disables unwinding. The MSVC ARM, `/ZW` and
x64 host compiles keep their established flag sets.

Two contract defects were caught before the run. The retain/release thunks were
first declared to return `WKUWPResult`, but the frozen table types them void,
so a null context is now ignored instead of answered. The transport first
reported an empty clipboard as S_FALSE, which the frozen result codes read as
`InvalidArgument`; it now completes OK with an empty span. `DispatcherUWP.h`
was missing its `<windows.h>` include for `HRESULT`.

## Remaining gates

This accepts the neutral service, the dispatcher hop and the WinRT transport
sources with their ownership rules. Component target wiring belongs to the 7.5
runtime owner, which also supplies `postWake`, cleanup-ticket accounting and
shutdown hooks. Copy/cut/paste behaviour, delayed completion after Close and
device clipboard runs retain the 7.2 gate. No device behaviour is claimed here.
