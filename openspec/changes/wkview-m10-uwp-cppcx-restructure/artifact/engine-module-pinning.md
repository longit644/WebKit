# M10 task 3.3 - EngineModuleUWP process pinning and runtime ownership

Consumer for the frozen V1 schema in `artifact/native-abi.md` ("Shutdown,
dispatcher loss and module pinning" and "Exact engine function table"). This
records what was implemented, what was measured, and what is deliberately not
claimed.

## Delivered

- `Source/WebKit/UIProcess/API/uwp/native/EngineModuleUWP.h` and
  `EngineModuleUWP.cpp` - the `WebKit::EngineModuleUWP` consumer: module
  resolution by name, process-lifetime pinning, per-page ownership and shutdown
  latching. The `native/` subdirectory matches the migration map's planned home
  for the framework-side native services.
- `Tools/uwp/build/WKUWPModuleTestProvider.cpp` - a configurable test-double
  provider, test tooling only. It proves nothing about the real engine provider,
  which task 3.2 verified separately.
- `Tools/uwp/build/EngineModuleUWPTests.cpp` - ownership harness that links the
  consumer directly and reaches every provider image only through the loader.
- `Tools/uwp/build/test-wkuwp-engine-module.ps1` - the verification driver.

### Binding and identity rules implemented

In order, with the module released again on every rejection:

| Condition | Result |
|---|---|
| already bound | 0 Ok, no second load |
| null or empty module path | 1 InvalidArgument, before any load |
| loader reports the module missing | originating negative HRESULT, unchanged |
| discovery export absent | 17 AbiMismatch |
| discovery itself rejects | the provider's own result, unchanged |
| reported extent below the table, foreign major, or any null slot | 17 AbiMismatch |
| otherwise | 0 Ok, and the module is never freed again |

A second `bind` on a bound object is idempotent and performs no second load. The
identity check reads nothing past the reported extent: the size comparison comes
first, and every one of the 24 slots is confirmed callable before the pin.

### Ownership rules implemented

- The page registry is a fixed 64-entry array, never a heap container, because no
  cross-module allocation may cross this boundary. A full registry refuses
  creation with 19 InvalidState.
- `closePage` on a zero handle or a handle this object never registered reports 1
  InvalidArgument without reaching the engine. An engine Ok or Closed answer ends
  the registration; any other answer keeps it, because the page still exists.
- Operations on an unbound object report 27 NotInitialized; creation after a
  successful `beginShutdown` reports 24 ShuttingDown without calling the engine.
- `beginShutdown` is forwarded every time so the engine's own idempotence
  decides, and additionally latches locally on success. `pollShutdown` passes the
  engine's answer through, including 20 Pending.
- The destructor frees nothing: the pin is process lifetime, and per-page state
  is integers only.

### Loader branch

Inside an AppContainer the desktop loader entry point is unavailable by design,
so the consumer uses the packaged loader there; the engine ships inside the app
package, which is exactly what that mechanism is for. Desktop test builds keep
the ordinary loader. The branch is a compile-time `WINAPI_FAMILY` selection in
one documented helper, and both sides are exercised: the ARM32 builds define the
App family, the x64 host builds do not.

## Verification

```
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\uwp\build\test-wkuwp-engine-module.ps1
```

1. ARM32 compile of the consumer and the test provider with the native engine
   compiler at `/W4 /WX`: clean.
2. ARM32 compile of both with the ARM32 MSVC `cl` that owns `/ZW`: clean.
3. An ARM32 module linked from both objects reports `COFF-ARM`,
   `IMAGE_FILE_MACHINE_ARMNT`, `IMAGE_DLL_CHARACTERISTICS_APPCONTAINER`, and
   exports the unmangled `WKUWPGetEngineAPI` the consumer resolves.
4. Ownership behaviour runs on this x64 host from x64 copies of the same
   sources. 27 checks pass, including:

```text
PASS bind/compatible, bind/identity, bind/idempotent (no second discovery call)
PASS bind/pinned: image loaded, open pages 0
PASS capability/delivered and capability/missing (18, unbound)
PASS identity/foreign-major, truncated, null-slot, missing-export (17, unbound)
PASS argument/null-path (1, unbound)
PASS unbound/operations: create/close/shutdown/poll all 27, handle untouched
PASS page/create, close, recreate, baseline (open count 1, 0, 1, 0)
PASS page/unknown: foreign/zero handles rejected, engine close count unchanged
PASS page/failed-creation: 22, handle untouched, count 0
PASS shutdown/begin, creation-blocked (24, engine not called), poll-pending (20),
     poll-done, idempotent
PASS pinning/bound, after-failure, after-shutdown (image loaded, still bound)
PASS pinning/rejection-unloads: rejected bind leaves the image unloaded
```

Load state is observed with `GetModuleHandleExW` using
`GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT`, so the observation itself never
changes a refcount. The lifecycle and rejection images are file copies the
harness process never loads; the rejection image is built, not copied, with a
truncated reported extent baked in, because DLL globals do not survive an unload
and arming a fresh image through setters would lose the setting.

## Defects this task found and fixed

### The desktop loader is unavailable under the App family

The first consumer called `LoadLibraryW` unconditionally, which does not exist
when `WINAPI_FAMILY_PC_APP` is defined; the native ARM32 compile failed on the
first attempt. The consumer now selects the packaged loader under the App
family. This is not test-only scaffolding: it is the mechanism the real
component will use to load the packaged engine.

### A rejected bind must be observable as unloaded

The first harness armed the rejection image through setters on the image the
harness itself holds, which cannot show unloading. The driver now builds a
dedicated rejection image with the truncation baked in, and the harness binds a
path it never loads itself.

### A failed bind left a null dereference in the harness

The first harness read the negotiated table after a bind that had already been
reported failed. Every such site now returns after reporting, and unbuffered
output was added so a fault still identifies its row.

## Regression

- `check-wkuwp-abi-layout.ps1` (C and C++ on both compilers, all four objects
  ARMNT): PASS. No frozen header was touched by this task.
- `test-wkuwp-engine-negotiation.ps1` (41 checks): PASS.
- `python Tools/uwp/audit-source-docs.py`: no diagnostics for the four new
  files. `clang-format --style=file` applied to them.
- `openspec validate wkview-m10-uwp-cppcx-restructure --strict`: valid.
  `git diff --check`: clean.

## Not claimed

- No full `WebCore` or `WebKit` rebuild, by explicit owner decision: the
  preserved `build-arm-uwp-skia` tree was not touched and stays the rollback
  reference. The consumer is proven by compiling it with its future target's
  flags and by behaviour tests, not by tree integration.
- No `EngineModuleUWP` target wiring. It belongs to the component integration
  work (section 5), which can link the exact files proven here.
- No real page, navigation, input, rendering or platform-service behaviour. The
  working page slots in these tests belong to the test double; the engine's own
  slots still report `Unsupported` until tasks 3.4-7.5.
- No device evidence. The ARM32 module is built and inspected, never executed;
  ownership behaviour runs on the x64 host, which is legitimate because it is
  bookkeeping plus negotiation arithmetic that does not vary by architecture.
