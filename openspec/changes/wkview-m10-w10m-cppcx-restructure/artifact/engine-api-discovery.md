# M10 task 3.2 - engine discovery export, capabilities and build identity

Producer for the frozen V1 schema in `artifact/native-abi.md` ("Exact engine function table" and "Results and capabilities"). This records what was implemented, what was measured, and what is deliberately not claimed.

## Delivered

- `Source/WebCore/platform/uwp/WKUWPEngineAPI.cpp` - the `WKUWPGetEngineAPI` export, the delivered-capability mask and the module build identity.
- `Source/WebCore/PlatformWin.cmake` - the new source is compiled into the `WebCore` target under `WK_UWP`, and `Source/WebKit/Shared/uwp` is added to that target's private include directories so the frozen headers resolve by their own names.
- `Tools/w10m/build/WKW10MEngineAPITests.cpp` - negotiation harness that resolves the provider through `LoadLibrary`/`GetProcAddress("WKUWPGetEngineAPI")`.
- `Tools/w10m/build/test-wkw10m-engine-negotiation.ps1` - the verification driver.

### Negotiation rules implemented

In order, with no output written before a rejection:

| Condition | Result |
|---|---|
| `outAPI == nullptr` | 1 InvalidArgument |
| `header.structureVersion != 1` | 17 AbiMismatch |
| declared `structureSize` below the V1 table | 17 AbiMismatch |
| declared `structureSize` above the provider's write bound (4096) | 17 AbiMismatch |
| `minimumMajor != 1` | 17 AbiMismatch |
| `minimumMinor > 0` | 17 AbiMismatch |
| any requested capability bit not delivered | 18 MissingCapability |
| otherwise | 0 Ok |

On success the caller-declared extent is zeroed first, then the known prefix is filled and `header.structureSize` reports the bytes actually filled. All 24 slots are assigned; no slot is null, and a slot whose feature is not implemented yet reports 22 Unsupported rather than being callable-and-absent.

### Capabilities and build identity

The delivered capability mask is `0`. Every frozen V1 bit belongs to work that has not been implemented yet, and a provider that advertised a bit it does not honour would let a consumer's negotiation succeed and then fail at use. Each bit is named in the provider source with the task that will add it. `buildId` is the string literal `wkuwp abi=1.0 arch=arm32 build=<WKUWP_BUILD_STAMP>`, where the stamp defaults to the compiler's own `__DATE__`/`__TIME__` and can be replaced by the build system through `WKUWP_BUILD_STAMP` when task 10.3 stages identity manifests. A literal is used deliberately: no lazy initialization means no initialization race between two threads that both call discovery first.

## Verification

```
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\w10m\build\test-wkw10m-engine-negotiation.ps1
```

1. ARM32 compile with the native engine compiler (`clang-cl`, triple read from `Toolchain-ARM32-W10M-clang.cmake`, flags from `Tools/w10m/arm32-w10m-env.ps1`) at `/W4 /WX`: clean.
2. ARM32 compile with the ARM32 MSVC `cl` that owns `/ZW` at `/W4 /WX /EHsc`: clean. Both boundary compilers accept the same source, which a producer-only file cannot assume.
3. An ARM32 module built from that object reports `COFF-ARM`, `IMAGE_FILE_MACHINE_ARMNT`, `IMAGE_DLL_CHARACTERISTICS_APPCONTAINER`, and exports the exact unmangled name `WKUWPGetEngineAPI`.
4. Negotiation behaviour runs on this x64 host from an x64 copy of the same provider, loaded through `GetProcAddress`. 41 checks pass, including:

```text
PASS argument/null-output: null outAPI -> result=1 expected=1
PASS version/unknown-version-0/2/99 -> result=17, output untouched
PASS version/major-zero, major-two, minor-one, minor-huge -> result=17, output untouched
PASS size/zero-extent, one-byte, one-byte-short -> result=17, output untouched
PASS size/frozen-v1-extent: declared 128 with a 228-byte host table -> refused, untouched
PASS size/beyond-write-bound: declared 65536 -> result=17, output untouched
PASS capability/<each of the 10 frozen bits> -> result=18, output untouched
PASS capability/all-bits-set -> result=18, output untouched
PASS capability/mask: delivered mask=0x0
PASS success/result, filled-extent, structure-version, version
PASS success/extent-respected: bytes past the declared extent are untouched
PASS success/no-overrun, build-identity, build-identity-content, slots-callable
PASS success/larger-declared-extent: declared=filled+128, tail zero, nothing past it written
PASS success/identity-stable: two negotiations share one identity
PASS page/not-created: createPage -> 22, handle untouched
PASS page/no-navigation-id: navigate -> 22, id untouched
PASS page/state-untouched: getState -> 22, requiredBytes untouched
```

Every rejection row compares the caller's whole buffer against a snapshot taken after the caller wrote its own header, so "untouched" is a byte-for-byte fact rather than an assertion about the fields the test happened to read.

## Two defects this task found and fixed

### The frozen headers were not valid C++

`Source/WebKit/Shared/uwp/WKUWPEngine.h` forward declares eight structure tags so the table can name `const struct WKUWPEngineOptions*` members, and `WKUWPPlatformServices` had a named forward typedef. The definitions were anonymous `typedef struct { ... } Name;`, which C accepts as two unrelated types but C++ rejects as a typedef redefinition. The first provider written against these headers failed to compile as C++ on the very first attempt. Since the consumer half of this boundary is C++/CX, the definitions now name their tags (`typedef struct WKUWPEngineOptions { ... }`), making the forward declaration and the typedef the same type.

This changes no size, no offset, no enumeration value and no signature. It is verified by re-running the layout check in both languages on both compilers:

```
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\w10m\build\check-wkw10m-abi-layout.ps1
--- native (clang-cl, armv7-unknown-windows-msvc, C) ---     PASS
--- native (clang-cl, armv7-unknown-windows-msvc, C++) ---   PASS
--- cx (MSVC ARM32 cl, C) ---                                PASS
--- cx (MSVC ARM32 cl, C++) ---                              PASS
all four objects: COFF-ARM / IMAGE_FILE_MACHINE_ARMNT
```

`check-wkuwp-abi-layout.ps1` previously compiled the assertions as C only. It now compiles them as C and as C++ for both compilers, and inspects each produced object with `llvm-readobj` instead of assuming the command line implies ARM32. The check file uses a `WKUWP_STATIC_ASSERT` macro because MSVC accepts `_Static_assert` only as C.

### The provider could have written past a smaller caller's structure

The first implementation accepted any declared extent of at least 128 and then filled `sizeof(WKUWPEngineAPI)` bytes. On ARM32 those are the same number, so the contract was met there; on the 64-bit development host used to run the negotiation behaviour the table is 228 bytes, and the provider would have written 100 bytes past a caller that declared 128. Discovery now requires the declared extent to cover the structure it is about to fill, reports the bytes it actually filled, and carries a `static_assert` that the frozen 128-byte constant still fits. The harness asserts both rules instead of assuming one architecture.

## Not claimed

- No full `WebCore` rebuild. The new source is compiled with the target's own ARM32 flags and the resulting module is inspected, but the preserved `build-arm-uwp-skia` tree was not regenerated, so nothing here proves the shipping `WebCore.dll` exports the entry point yet. Regenerating that tree belongs to the native rebuild/orchestration work.
- No consumer. `EngineModuleUWP` (LoadLibrary, process pinning, identity compatibility) is task 3.3 and does not exist yet.
- No engine table slot behaviour. All 24 slots report `Unsupported`; page creation, navigation, input, rendering, platform services and shutdown are tasks 3.3-7.5.
- No device evidence. The ARM32 module is built and inspected, never executed; negotiation behaviour runs on the x64 host, which is legitimate because the behaviour is negotiation arithmetic that does not vary by architecture.
