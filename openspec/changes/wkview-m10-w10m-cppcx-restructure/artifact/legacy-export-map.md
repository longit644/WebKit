# M10 task 3.6 - legacy export forwarders with explicit maps

Explicit legacy-to-V1 map for the frozen V1 schema in `artifact/native-abi.md`, keeping the existing smoke consumers on unchanged numeric and layout semantics while new consumers bind only the negotiated table. This records what was implemented, what was measured, and what is deliberately not claimed.

## Delivered

- `Source/WebCore/platform/uwp/LegacyExportMapUWP.h` - the explicit map: every one of the 24 legacy exports with its consumer status (21 wrapper-bound, 1 smoke-only, 2 declared-unbound), its exact signature typedef, its V1 destination and owning task, plus compile-time proofs that every legacy result code is numerically identical to its V1 counterpart, every validation limit matches its V1 bound, and both legacy state structures keep their exact ARM32 layouts.
- `Tools/w10m/build/wkw10m-legacy-surface-check.c` - signature-freeze translation unit assigning each of the 23 declared exports to its recorded typedef, so drift fails to compile on both boundary compilers.
- `Tools/w10m/build/check-wkw10m-legacy-surface.ps1` - the verification driver.

### What "forwarder" means in this task

The result-code forwarding is implemented and proven: legacy `WKPageResultUWP` values are numerically identical to `WKUWPResult` for every value both define, so forwarding is the identity map, and the wrapper's HRESULT mapping already agrees with the V1 public mapping. The per-export call forwarding (legacy arguments into V1 table calls) needs engine state that does not exist yet - the page registry, the bound table and the 4.x behaviours - so each map entry names its V1 destination and owning task instead of pretending the call already works. Writing those callable forwards now would invent argument mappings the contract does not define.

## Verification

```
powershell.exe -NoProfile -ExecutionPolicy Bypass -File Tools\w10m\build\check-wkw10m-legacy-surface.ps1
```

1. The map compiles clean for ARM32 with both boundary compilers as C++ at `/W4 /WX`, with all 23 signature assignments holding. Only C++ is exercised because every consumer of this surface is C++.
2. The preserved `build-arm-uwp-skia/bin/WebCore.dll` exports all 24 legacy names unmangled, inspected read-only. The tree itself is never configured, built or written.
3. Every `GetProcAddress` literal in the new `native/` sources is `WKUWPGetEngineAPI`; test files add only the `WKUWPTestProvider*` harness exports and the two component activation exports the task 2.4 harness resolves by design. No legacy `WK*UWP` identifier appears in the new consumer sources.
4. The smoke surface (`PageUWP.h/.cpp`, `FirstPageUWP.cpp`, the wrapper and the diagnostic hosts) is byte-identical to the committed HEAD, so this slice provably changed none of the semantics it claims to retain.

### Map highlights

- 21 exports are wrapper-bound with V1 destinations across tasks 3.4 (policy callbacks) through 7.2 (clipboard service); `WKScrollPageUWP` is the one entry with no direct slot, mapping to input plus state via `sendInput`.
- `WKInitializeEngineUWP` is declared-unbound (called in-engine only) and maps to `initialize` in task 4.1.
- `WKGetPageDocumentStateUWP` is declared-unbound and maps to nothing callable: the structured equivalent arrives with task 4.3, and the map states it must not be presented as a working feature meanwhile.
- `WKRenderFirstPageUWP` is undeclared and smoke-only; its `FirstPageUWP.cpp` spelling and SmokeHost's inline cast were reviewed matching, and its payload moves under diagnostics/ in task 8.5 instead of gaining a declaration.
- The first-page result codes 0-8 mirror the legacy page-result family, including values 2 and 3 that stay reserved gaps in the page results.

## Defects this task found and fixed

None in shipped code. Two script defects were corrected during authoring: a toolset path joined one segment too many, and a PowerShell array-nesting bug that passed collections instead of files to the consumer scan. A third, substantive correction: the scan initially rejected the task 2.4 harness for resolving the component activation exports, which it does by design; the allowlist now names them explicitly instead of silently passing.

## Regression

- No shared source was modified by this task: no frozen header, no consumer file and no test-double file changed, so the 3.2, 3.3, 3.4 and 3.5 gates are unaffected by construction. The new gate itself re-proves the frozen result numerics it shares with those gates.
- `python Tools/w10m/audit-source-docs.py`: no diagnostics for the new files. `clang-format --style=file` applied to the map header (the `.c` check and `.ps1` driver are excluded by convention).
- `openspec validate wkview-m10-w10m-cppcx-restructure --strict`: valid. `git diff --check`: clean.

## Not claimed

- No callable per-export forwards: they need engine state from tasks 4.x, and inventing argument mappings now would fabricate semantics.
- No rebuilt engine and no tree regeneration: the preserved tree stays the pristine rollback reference by standing owner decision.
- No device behaviour of any export. Presence and signatures are proven; the phone behaviour of the smoke consumers is tasks 8.x and 9.x territory.
