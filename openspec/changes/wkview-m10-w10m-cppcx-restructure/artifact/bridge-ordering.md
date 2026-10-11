# M10 task 3.5 - bridge tests: interleaved pages, wrong-thread calls, close
during policy dispatch

Bridge behaviour for the frozen V1 schema in `artifact/native-abi.md` ("Callback schema and context ownership" and the owner-thread execution rule). This records what was implemented, what was measured, and what is deliberately not claimed. The callback/close ordering contract the task requires documented lives in "Callback/close ordering" below.

## Delivered

- `Source/WebKit/UIProcess/API/uwp/native/CallbackContextUWP.h/.cpp` - one addition only: a reentrant policy hook invoked from inside the starting entry point after the payload copy and before the verdict, so a close during policy dispatch can be driven without the context knowing what a module object is.
- `Tools/w10m/build/WKW10MModuleTestProvider.cpp` - two test-double behaviours: owner-thread enforcement (a recorded owner thread; every working slot rejects other threads with WrongThread before reading any argument) and close following the same deferred safe release as detach (an active attachment goes inactive at once, acknowledged now or by FireRelease). Defaults are unchanged, so the task 3.3 and 3.4 gates still pass against the same file.
- `Tools/w10m/build/BridgeOrderingTests.cpp` - bridge harness linking the consumers directly and reaching the provider only through the loader.
- `Tools/w10m/build/test-wkw10m-bridge-ordering.ps1` - the verification driver.

## Verification

```
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\w10m\build\test-wkw10m-bridge-ordering.ps1
```

1. ARM32 compile of all three sources with the native engine compiler at `/W4 /WX`: clean.
2. ARM32 compile of all three with the ARM32 MSVC `cl` that owns `/ZW`: clean.
3. An ARM32 module linked from all three objects reports `COFF-ARM`, `IMAGE_FILE_MACHINE_ARMNT`, `IMAGE_DLL_CHARACTERISTICS_APPCONTAINER`, and exports the unmangled `WKUWPGetEngineAPI`.
4. Bridge behaviour runs on this x64 host from x64 copies of the same sources. 12 checks pass, including:

```text
PASS interleaved/no-routing: first context sees only its 2 events ending at
     sequence 3, second context sees only its event at sequence 2
PASS interleaved/close-one, survivor (still delivers), baseline (0 open)
PASS thread/owner-works, worker-rejected (create/close/detach/state all 8,
     handle untouched, registration kept), owner-recovers
PASS close-during-policy/close (verdict intact, hook answer Ok, baseline 0),
     ack (released once, storage alive), no-stale-attach (1 InvalidArgument)
PASS close-deferred/close (release deferred), release (once on unwind)
```

## Callback/close ordering

The ordering this task proves, stated as the contract the real engine (tasks 4.x) and the future component (section 5) must both honor:

- Dispatch delivers to the attachment active for that page only. Sequences are strictly increasing per page; two pages never observe each other's payloads, and there is no process-global routed instance to observe them through.
- Detach and close both deactivate immediately. The release acknowledging the deactivation runs at once outside a dispatch, and after the unwind inside one; `detachCallbacks` reports the difference as Ok versus Pending, while `closePage` reports its own result and defers the release silently.
- A release runs exactly once per detachment. A second release, a delivery to a detached attachment, and an attach to a closed page are all refused; none of them touches a freed context.
- The creator's setup reference is released separately and last. Storage is freed only by that final release, which is why a Pending detach followed by an unrelated close can never free a context a dispatch still needs.
- Thread affinity is the outermost precondition. An off-owner-thread call is rejected with WrongThread before any argument is read, so a wrong-thread call can neither create nor disturb a registration; the owning thread is unaffected and service resumes when enforcement lifts.
- A close reentrant from inside policy dispatch completes normally: the close result is reported to the hook, the verdict still governs the starting answer, and the attachment follows the deferred release above. After the close, the page accepts no new attachment.

## Defects this task found and fixed

None in shipped code. The harness needed one correction during authoring: the first interleaved test read a context after its final release, the same use-after-release class of mistake task 3.4 fixed. It now asserts while a reference is held and proves freeing through the hook.

## Regression

- `test-wkuwp-engine-module.ps1` (27 checks against the extended provider): PASS. Provider defaults and the close path without attachments are unchanged.
- `test-wkuwp-callback-ownership.ps1` (40 checks): PASS.
- `test-wkuwp-engine-negotiation.ps1` (41 checks): PASS.
- `check-wkuwp-abi-layout.ps1` (C and C++ on both compilers, all four objects ARMNT): PASS. No frozen header was touched by this task.
- `python Tools/w10m/audit-source-docs.py`: no diagnostics for the new or touched files. `clang-format --style=file` applied to them.
- `openspec validate wkview-m10-w10m-cppcx-restructure --strict`: valid. `git diff --check`: clean.

## Not claimed

- No full `WebCore` or `WebKit` rebuild and no consumer target wiring, by the standing owner decision: `build-arm-uwp-skia` stays the pristine rollback reference, and wiring belongs to the section 5 component integration.
- No real engine callback behaviour. Dispatch order, thread affinity and close release belong to the test double; the engine's own slots still report `Unsupported` until tasks 4.x.
- No device evidence. The ARM32 module is built and inspected, never executed; bridge behaviour runs on the x64 host, which is legitimate because it is bookkeeping plus negotiation arithmetic that does not vary by architecture.
