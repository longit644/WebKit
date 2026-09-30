# wkview-m3-shims-1 — builtins-arm + first WK_WEBKITVIEW source shims

## Why

M2 proved configure reaches port deps but ARM link needs
`clang_rt.builtins-arm.lib` (absent from scoop LLVM23 and VS clang 19),
and Win-port sources need AppContainer shims before WebCore configures.

## What

1. Acquire `clang_rt.builtins-arm.lib` (official LLVM release, user-space
   extract — no admin). Verify with `llvm-lib /list` or link probe.
2. First shim batch (all `#if defined(WK_WEBKITVIEW)` + `WebKitView:`):
   - WTF: generic RunLoop selection for the new port define.
   - WTF: `getCurrentProcessID()` for `getpid()` call sites.
   - JSC: drop `JSStringRefBSTR` from build lists.
   - WebCore: stub `PublicSuffixStore` (no libpsl).
3. Re-run ARM configure (`PORT=Win`); extend `ARM-WALLS.md` with results.

## Non-goals

No .appx, no device, no GPU/JIT. Configure-level progress only.

## Acceptance

builtins lib present + link probe passes; shim batch committed; ARM
configure reaches further than M2 (or new walls filed). Owner ack →
tag `wkview-m3-shims-1`, open M3-2 (curl/OpenSSL prebuilts).
