# wkview-m1-host-jsc — host JSC CLoop build (x64)

## Why

Before touching ARM cross or AppContainer shims, prove the full codegen chain
(perl + bison + ruby + clang-cl + cmake + ICU) produces a working JavaScriptCore
on this PC. CLoop because UWP forbids JIT pages — the same interpreter config
the phone will use.

## What

1. Configure + build JSC host x64 Release with `--cloop` via
   `Tools/Scripts/build-jsc` (uses vcpkg ICU just installed).
2. Run `jsc` shell: `print("wkview-m1")` + a basic GC/regex smoke test.
3. Record wall time + peak RAM (sizes the later ARM/JSC work).

## Non-goals

No ARM, no UWP, no WebCore, no device. Host-only proof.

## Acceptance

`jsc` binary runs the smoke script; log pasted in tasks; tag `wkview-m1-jsc`
only after owner acks (tag ≠ device proof, records host milestone).
