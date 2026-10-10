# Native acronym conventions

Owner-selected WebKit-style spellings are **CPU**, **GPU** and **OK** in native
type/enum identifiers. Leading variable/function acronyms use lowerCamelCase,
such as `gpuPages` and `cpuStride`; SDK-owned identifiers keep SDK spelling.
Public C++/CX API declarations remain PascalCase and compiler WinMD remains the
authority. Native internal enums use `enum class`.

Canonical source names now include `PresentationModeUWP::CPU/GPU`,
`WKUWPResultOK`, `WKUWPCapabilityCPUPaint` and
`WKUWPCapabilityGPUCompositor`. Per owner instruction, mixed-case source names
are replaced throughout callers/tests and removed from the declarations;
no compatibility aliases are retained. Enum types/widths, numeric values,
structures, slots, exports and public interface identities are preserved.

Checks on 2026-10-10:

- `check-wkuwp-abi-layout.ps1 -OutDir <fresh-output>` passes ARM C and C++ with
  both clang-cl and MSVC, including every frozen layout/value under the canonical names.
- `test-rendering-contract.ps1` passes **49/49** actual DirectX ownership/metrics
  checks and both native ARM compilers after CPU/GPU/type spelling updates.
- `test-frame-scheduler.ps1` passes **34/34** and both native ARM compilers.

Existing historical records and retained legacy `WKPageResultOkUWP` /
`FirstPageResultOkUWP` contracts keep their recorded names until the section-10
compatibility cleanup gate. New WKUWP consumers use canonical acronym spelling.
