# M10 task 3.4 - retained callback/service contexts and detach acknowledgment

Consumer ownership layer for the frozen V1 schema in `artifact/native-abi.md` ("Callback schema and context ownership", "Platform service and completion schema" and the getState rule). This records what was implemented, what was measured, and what is deliberately not claimed.

## Delivered

- `Source/WebKit/UIProcess/API/uwp/native/CallbackContextUWP.h/.cpp` - `WebKit::PageCallbackContextUWP` (intrusive-retained per-page callback context with copied payloads, policy verdicts and detach/destruction hooks) and `WebKit::PageStateSnapshotUWP` (caller-owned state snapshot with bounded size-probing retry).
- `Source/WebKit/UIProcess/API/uwp/native/PlatformServiceUWP.h/.cpp` - `WebKit::PlatformServiceContextUWP` (host-side platform service with a fixed operation table and exactly-once completion including cancellation).
- `Source/WebKit/UIProcess/API/uwp/native/EngineModuleUWP.h/.cpp` - two forwarding methods only: `setPageCallbacks` and `detachPageCallbacks`, with the same unbound/unknown/null validation as page creation. No 3.3 behaviour changed.
- `Tools/w10m/build/WKW10MModuleTestProvider.cpp` - extended with a simulated attach/detach engine (retain-once, replace rejection, Pending during dispatch, deferred release), event dispatch with buffer scrambling, and size-probing and ever-growing getState modes. Defaults are unchanged, so the task 3.3 gate still passes against the same file.
- `Tools/w10m/build/CallbackOwnershipTests.cpp` - ownership harness linking the consumers directly and reaching the provider only through the loader.
- `Tools/w10m/build/test-wkw10m-callback-ownership.ps1` - the verification driver.

### Ownership rules implemented

- The callback table carries context plus retain/release/policy/notify entry points; the engine retains once before the table activates, and the creator's setup reference is released separately. Storage is freed only by the final release, proved with a destruction hook.
- Event spans are copied into context-owned storage on every delivery, before the provider may reuse its buffers. Policy answers refuse by default, allow when armed, and travel a configured handler failure as its result.
- Detach deactivates immediately. A Pending answer during dispatch defers the release until the unwind; a second release or a later delivery is refused. Replacing an active attachment is refused; re-attaching after acknowledgment succeeds.
- The snapshot helper retries only on BufferTooSmall with a strictly larger reported need, gives up after five attempts, fails fast on absurd needs or allocation failure, and never retries other answers.
- An accepted clipboard request completes exactly once: cancel delivers the platform cancellation HRESULT, reads deliver borrowed text once, rejections and repeats complete nothing, and the seventeenth concurrent request fails with BudgetExceeded. Write text is copied on acceptance; read spans stay borrowed during the completion call. A null completion, unknown page or kind, content-bearing read, or duplicate identifier is rejected before acceptance.
- Every consumer entry point answers null or malformed inputs with frozen result codes; nothing throws across the boundary.

## Verification

```
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\w10m\build\test-wkw10m-callback-ownership.ps1
```

1. ARM32 compile of all four sources with the native engine compiler at `/W4 /WX`: clean.
2. ARM32 compile of all four with the ARM32 MSVC `cl` that owns `/ZW`: clean.
3. An ARM32 module linked from all four objects reports `COFF-ARM`, `IMAGE_FILE_MACHINE_ARMNT`, `IMAGE_DLL_CHARACTERISTICS_APPCONTAINER`, and exports the unmangled `WKUWPGetEngineAPI`.
4. Ownership behaviour runs on this x64 host from x64 copies of the same sources. 40 checks pass, including:

```text
PASS attach/install (refcount 2, one retain), detach-immediate, setup-release
PASS payload/copy: URI/title survive buffer scrambling, kind/sequence recorded
PASS policy/refuse, allow, handler-failure (exact negative result preserved)
PASS guards/null-starting (InvalidArgument, output untouched), null-notify and
     dispatched null deliveries suppressed without faulting
PASS detach/pending (answer 20, no release, storage alive)
PASS detach/release-after-unwind, storage-freed, no-double-release (19),
     suppressed delivery (19), close-baseline
PASS attach/replace-rejected (19, second context never retained),
     reattach-after-ack
PASS snapshot/retry (2 attempts, exact growth), need-only (only requiredBytes
     written), bounded (5 attempts then 21), no-retry (22, 1 attempt)
PASS service/write-accept, write-copy, cancel-once (1 completion, cancellation
     HRESULT), cancel-twice (1 InvalidArgument), no-second-delivery
PASS service/read-accept, read-deliver (borrowed text copied during call),
     read-once
PASS service/rejections (kind/completion/text/read-text), table-full (23),
     drain-once (16 completions, 0 pending)
PASS service/retain, wake, engine-release, setup-release (single freeing)
PASS service/null-guards, unknown-operations
```

## Defects this task found and fixed

### An off-by-one hid the retry path

The snapshot helper started with exactly the need the test provider reports, so the fetch succeeded on its first attempt and the retry logic ran zero times while claiming to be tested. The initial size is now smaller than any real need, so the growth path is always exercised.

### The unwind test freed through a live pointer

The first release-after-unwind test read the context's counters after the release that destroyed it. The test now asserts the release count while the setup reference keeps the object alive, and proves freeing separately through the destruction hook plus a refused second release.

### A test expected destruction while holding a reference

Related: the first version expected the unwind release to destroy an object that still had its setup reference. The corrected test distinguishes the acknowledgment (release count) from the freeing (setup release), which is the actual contract: the caller releases its own setup reference separately.

## Regression

- `test-wkuwp-engine-module.ps1` (27 checks against the extended provider): PASS. Provider defaults were preserved exactly for this reason.
- `test-wkuwp-engine-negotiation.ps1` (41 checks): PASS.
- `check-wkuwp-abi-layout.ps1` (C and C++ on both compilers, all four objects ARMNT): PASS. No frozen header was touched by this task.
- `python Tools/w10m/audit-source-docs.py`: no diagnostics for the eight new or touched files. `clang-format --style=file` applied to them.
- `openspec validate wkview-m10-w10m-cppcx-restructure --strict`: valid. `git diff --check`: clean.

## Not claimed

- No full `WebCore` or `WebKit` rebuild and no consumer target wiring, by the standing owner decision: `build-arm-uwp-skia` stays the pristine rollback reference, and wiring belongs to the section 5 component integration.
- No real engine callback behaviour. Dispatch, detach acknowledgment and state growth belong to the test double; the engine's own slots still report `Unsupported` until tasks 4.x.
- No device evidence. The ARM32 module is built and inspected, never executed; ownership behaviour runs on the x64 host, which is legitimate because it is bookkeeping plus negotiation arithmetic that does not vary by architecture.
