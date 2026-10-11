# Design - M10 UWP C++/CX restructure

## Context

See `proposal.md` for motivation. The current component is a monolithic WRL prototype in Tools; the native wrapper mixes XAML, DLL binding, gestures and frame profiling; PageUWP embeds device fixtures. Existing uncommitted CX probe work compiles to an ARM object but has no linked/device proof. Native engine flags disable exceptions; the installed modern SDK does not supply ARM32 link libraries. No archived OpenSpec capability exists.

The detailed developer design is [Tools/w10m/DESIGN.md](../../../Tools/w10m/DESIGN.md). [artifact/public-api.md](artifact/public-api.md), [artifact/native-abi.md](artifact/native-abi.md), [artifact/migration-map.md](artifact/migration-map.md), [artifact/build-and-packaging.md](artifact/build-and-packaging.md) and [artifact/acceptance-matrix.md](artifact/acceptance-matrix.md) refine its contracts; `specs/` defines requirements. Supporting-document basenames below refer to this `artifact/` directory. This design.md stays at the change root for OpenSpec.

## Goals / Non-Goals

**Goals:** consistent names/placement across all authored UWP integration, C++/CX OS/application/component projections, typed cross-compiler ownership, reusable multi-instance control, preserved native loading/Skia behavior, and an implementation-ready handoff from Sol to another model.

**Non-Goals:** forcing native algorithms or third-party dependencies into ref classes, enabling `/ZW` on WebCore/WTF/JSC/PAL, synchronizing incomplete M9 deltas as accepted specs, or claiming any device behavior from planning validation.

## Decisions

### D1. Keep the user's public name and authored source home

Use `WebKit.WebKitWebView`, authored directly in `Source/WebKit/UIProcess/API/w10m/WebKitWebView.h/.cpp`. Native/input/rendering services live in subdirectories; Tools owns projects and packaging. A separate WinRT implementation directory and a new public namespace were considered, but the owner requested the existing API location and no public rename.

### D2. Use C++/CX projections with a native engine boundary

MSVC `/ZW` owns all UWP XAML/WinRT projections. Native engine/compiler settings remain separate across a versioned C ABI. A global `/ZW` migration would introduce compiler/runtime coupling into upstream engine code; merely replacing WRL syntax would retain the same ownership defects. C++/WinRT is not this milestone's model; its installed header's ARM rejection is version-specific evidence.

### D3. Compiler metadata is the component source of truth

C++/CX public declarations generate WinMD and activation/XAML integration. Historical MIDL outputs do not define new interfaces independently. New IIDs and contract revision identify incompatible prototype changes. Retaining the old IID while changing Source/events would corrupt consumers; hand-authoring the whole XAML inheritance chain repeats the earlier failed approach.

### D4. Separate owner-thread page state from XAML lifetime

Each control owns a controller/page/session; a retained module/shared service pump is process-scoped. Native policy is synchronous and ordinary notifications are owner-thread dispatched with stable IDs and copied payloads. Unloaded pauses presentation, whereas Close ends the instance. Process-global raw sink routing and destroy-on-every-Unloaded were rejected for multi-instance/reparenting safety.

### D5. Replace prototype API semantics deliberately

Source is URI-valued requested state while pending, effective state on commit, and restored to the previous commit on pre-commit refusal/failure. Read-only state is bindable. Exactly one NavigationCompleted outcome replaces the split terminal events. Domain/code pairs and HTTP presence are explicit. See `public-api.md`; no string/log inference or redundant Navigate API is used.

### D6. Preserve actual loading/security and move fixtures to a runner

WebCore/curl preserves responses, origins, history and session isolation. App policy supplies certificate consent through a token-bound method, never URL magic. Replace each old fixture assertion before removal. Moving the assertions into normal MiniBrowser startup would retain the production/test coupling.

### D7. Split native and UWP project builds

Use CMake/Ninja engine builds and MSBuild CX/XAML projects with shared discovery and a staged identity manifest. A single compiler-global CMake setup cannot correctly combine `/ZW` and clang ARM exception-disabled objects. Full runtime, DLL link, WinMD and phone gates precede broad migration, rather than treating the successful probe object as feasibility completion.

### Review resolutions (D8-D13)

- **D8 Opaque certificate capability:** page-local IDs remain stable, but consent uses a nonconstructible WebKitCertificateFailureToken object plus engine page/session/failure pair. Public numeric token overload is removed. A global navigation-ID allocator was considered but would conflate navigation diagnostics with cross-instance capability identity.
- **D9 HTML and Source transitions:** HostHtml has an explicit Starting/terminal lifecycle and history-neutral transient document. Null Source follows a per-state table. Only the explicit warning-token HTML overload preserves consent; accidental preservation across every generic HTML/navigation command was rejected.
- **D10 Notification transactions:** immutable snapshot and terminal obligation precede PropertyChanged. Defer/revalidate reentrant commands FIFO, finish owed outcomes before Close revocation. Synchronous policy still evaluates cancellation before a load begins. Generation dropping alone cannot satisfy exactly-once delivery.
- **D11 Configuration isolation:** CA-file-only trust is immutable per session and cached TLS/connection reuse is isolated/disabled across sessions. Fonts explicitly remain process-global; conflicting canonical path/content is rejected, not ignored. Options/ranges/deadlines and cancellation are fully specified in public-api.md.
- **D12 Async/shutdown:** each caller gets its own async action over shared init; readiness is the success milestone. RuntimeOwnerUWP owns cleanup before enqueue; quiesce callbacks/workers/platform completions before dispatcher destruction. WebCore module is process-pinned; hot-unload is deferred rather than promised despite global engine hooks. Terminal OS thread loss has a documented quarantine fallback, never normal foreign-thread XAML destruction.
- **D13 Exact ABI:** native-abi.md specifies signatures, values, layouts, table negotiation, service acknowledgment and CPU/GPU COM ownership. A schema freeze with both compilers precedes producer/consumer work; deviations revise the design. G4 now requires section 7 OS-service tasks in addition to rendering work.

## Risks / Trade-offs

- [ARM runtime/XAML tools incomplete] -> Gate 1 proves complete DLL/XAML/device feasibility; stop with exact missing dependency before migrating consumers.
- [Native shims incompatible with CX EH] -> Audit imports and test constructor failure; use supported runtime initialization, not placeholder EH functions.
- [Metadata default interface differs from prototype] -> Inspect generated identities and version the public contract; do not package conflicting WinMDs.
- [Reentrant event destroys its page] -> Retain dispatch lifetime and defer destructive release, invalidate generations, then verify Close-in-handler.
- [Large rename hides regression] -> Use an explicit exhaustive manifest, dependency-ordered slices and baseline packages, not one global replacement.
- [Old device results overclaimed] -> Record package/build identity on each new scenario and keep historical evidence separate from M10 acceptance.

## Migration Plan

Tasks follow baseline -> complete CX feasibility -> neutral ABI -> engine extraction -> framework control/services -> apps -> replacement tests -> cleanup and device acceptance. Preserve existing uncommitted work and compatibility exports until their replacement passes. Keep M9 as superseded history with a requirement/evidence transfer map. Roll back a failing slice using a recorded matching package and engine/component identity. No commit/push/tag is implied.

## Open Questions

The independent review identified genuine contract gaps in the original draft; D8-D13 and the revised API/ABI/specs resolve those choices. Exact tool/runtime/XAML versions, dual-compiler ABI conformance, per-session SSL backend enforcement and device behavior remain unverified feasibility measurements, not assumed support. If measurement contradicts a frozen contract, revise this change before proceeding.
