# Implementation handoff from Sol

This is a planning-only delivery. Another model implements the unchecked tasks; Sol's design is the authority for names, scope, boundaries, ABI and acceptance.

## Read order

1. [../README.md](../README.md), [../proposal.md](../proposal.md), [../design.md](../design.md) and [Tools/uwp/DESIGN.md](../../../../Tools/uwp/DESIGN.md).
2. public-api.md, native-abi.md, migration-map.md, build-and-packaging.md.
3. ../specs/*/spec.md, acceptance-matrix.md and [../tasks.md](../tasks.md).
4. Historical M9 control/fixture/loader maps and the exact device records listed in the transfer ledger; they are evidence references, not conflicting policy.

## Starting state

The prior session left modifications to Tools/uwp/WinRTComponent/CMakeLists.txt, README.md and XamlProbeComponent.cpp, plus M9 handoff/findings. These include an ARM-compilable CX probe with manual factory, but no final DLL/metadata/device proof. Preserve them in baseline accounting and review refcount/exception behavior. The shipping prototype component remains WRL. Never infer completion from the design author's earlier broad claims about C++/WinRT or SDK runtime support.

## Mandatory operating rules

- Start with tasks section 1 (G0 baseline), then prove section 2 (G1), not by converting the monolithic component first.
- No broad renames before completing the exact manifest for that area.
- Runtime/CRT/XAML feasibility failures block migration; record the actual missing symbol/tool/import or phone result and resolve through a design-consistent build.
- Keep the public name and API root location. CX declarations own metadata; no incompatible IID reuse or competing MIDL-generated component WinMD.
- No event generation from diagnostic strings, global raw routing, app downloader, shell history, fixture hooks or URL-based certificate consent.
- Keep standard C++ for engine/payload/DirectX/transport; /ZW only in its build domain.
- Follow the corrected public-api.md transition tables and native-abi.md exact V1 schema. Task 3.0 conformance precedes producers/consumers. Numeric local navigation IDs are never certificate capabilities; use opaque tokens and explicit warning HTML.
- Test PropertyChanged reentrancy and separate caller async actions. Keep CA trust per session and fonts explicitly global/conflict-checked; do not preserve singleton trust mutation under renamed wrappers.
- Pin engine module for process lifetime and implement runtime-owned cleanup/quiescence; no hot-unload promise. G4 requires both section 6 and all section 7 tasks.
- Each task needs command results and applicable device evidence. Keep partial/ blocked tasks unchecked and document remaining criteria.
- Do not delete user changes, historical evidence, working package, or old tests before the safe replacement gate. No Co-Authored-By trailers.
- Commit/push/tag only if explicitly requested. Avoid gratuitous scope expansion.

## Design changes

If an implementation cannot meet a normative requirement, document the precise blocker and request a change to the design/spec before weakening it. Source/event semantics, user-visible names, ABI/lifetime, security and completion gates must not be silently changed to match convenient code. Experimental runtime options are not accepted until G1 evidence exists.
