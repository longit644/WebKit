# M10 - UWP C++/CX architecture and restructuring

Author: Sol. Status: planning artifacts; implementation not started by this change.
M10 supersedes unfinished M9 architecture/requirements while preserving its
implementation and historical device evidence. No milestone completion is implied.

## Authority and source placement

Public type remains WebKit.WebKitWebView. Authored files stay directly in
Source/WebKit/UIProcess/API/uwp/WebKitWebView.h/.cpp. All UWP application/component/
OS projections use C++/CX; engine/native helpers and third-party code keep native
types and build settings. The primary developer design is
[Tools/uwp/DESIGN.md](../../../Tools/uwp/DESIGN.md).

## Artifacts

- proposal.md: motivation, scope, capabilities and breaking prototype changes.
- design.md: architectural decisions, alternatives, risks and migration gates.
- [artifact/public-api.md](artifact/public-api.md): types, opaque tokens, Source/HTML/notification transitions, caller async actions and trust/font configuration.
- [artifact/native-abi.md](artifact/native-abi.md): exact versioned C schema, pack4 ARM32 offsets, function table and resource/context/shutdown ownership.
- [artifact/baseline.md](artifact/baseline.md): measured G0 baseline, rollback package identity and the two blocking toolchain gaps.
- [artifact/migration-map.md](artifact/migration-map.md): source/symbol destinations and M9 transfer ledger.
- [artifact/build-and-packaging.md](artifact/build-and-packaging.md): compiler domains, metadata and runtime gates.
- [artifact/acceptance-matrix.md](artifact/acceptance-matrix.md): host/device evidence, review resolutions and reference event traces.
- [artifact/implementation-handoff.md](artifact/implementation-handoff.md): starting state and rules for the implementing model.
- specs/: nine normative capability deltas with testable scenarios.
- tasks.md: dependency-ordered implementation work, initially all unchecked.

## Commands

```powershell
openspec status --change wkview-m10-uwp-cppcx-restructure
openspec validate wkview-m10-uwp-cppcx-restructure --strict
openspec instructions apply --change wkview-m10-uwp-cppcx-restructure
```

`design.md` remains at the root; the six supporting documents live in `artifact/`.
Read the handoff before applying. Complete linked/packaged/compiled-XAML phone
feasibility before broad migration; an ARM probe object is insufficient. Mark
tasks complete only with evidence; archive only after owner acceptance.

## Independent-review corrections

The original review found contract gaps, not architectural rejection. Revised
contracts specify cross-instance opaque consent tokens, explicit warning HTML/null
Source semantics, notification transactions, distinct initialization actions,
immutable per-session trust/global-font conflicts, precise ABI conformance and
runtime-owned process-pinned shutdown. G4 depends on both rendering and OS services.
The resolution ledger records required verification; all implementation tasks remain
unchecked and no review correction claims device/runtime support.
