# M10 4.8 - Opaque certificate-exception tokens

TLS verification fails closed, and only an explicitly consumed token bound to its issuing session, failure revision, host and effective port arms a session-scoped exception. Numeric navigation IDs never establish identity, and reserved document URLs grant nothing. Proven on the Lumia, with both controls failing at local navigation ID 1.

## Delivered

- `Source/WebCore/platform/uwp/CertificateExceptionStoreUWP.h/.cpp` - the consent store: per-session monotonic failure revisions, at most one pending token, single preservation arming, and armed host/port keys. `noteFailure` supersedes, `issuedToken` reports zero/zero when nothing is eligible, `consumeToken` checks issuer, revision, host and port at once and mutates nothing on failure, `invalidatePending` drops eligibility while armed trust survives, and `allowsRequest` enforces session/host/ effective-port scope. Everything runs on the owner thread, so check-and-consume is indivisible without locking.
- `Source/WebCore/platform/uwp/LoaderStrategyUWP.h/.cpp` - the old host-only exception map is gone. `hasProceedException` now enforces session/host/port through the store (the transport call site is untouched); `removeProceedExceptions` erases the session's tokens and exceptions at teardown; `addProceedException` is removed with its only caller.
- `FrameLoaderClientUWP.cpp` - the `minibrowser:` proceed branch now fails closed (Ignore plus a `navigation-proceed-reserved` report) instead of arming trust from a document URL. Provisional start supersedes pending tokens unless a token-associated warning armed preservation; cancelled failures invalidate; other failures record a new revision. The shell's blocked-page proceed link goes inert until the section-8 migration wires it to the token API.
- `TransientPageUWP.cpp` - warning shows arm token preservation alongside failure preservation; generic shows arm neither.
- Suite phases 53-66 plus `Done` extensions: owner fail, peer fail at ID 1, cross-issuer rejection, stale rejection, warning preservation, generic invalidation, single consumption with port/session isolation and reuse rejection, refailure, remote-command invalidation, peer teardown.
- `PageHostUWP.h` - token halves for the cross-issued probe.

## Verification

```
cmake --build build-arm-uwp-fresh --target WebCore --parallel 8
```

- Fresh-tree `WebCore` links: ARMNT, AppContainer, 25 exports.
- Device run `WebKitProbe.EngineBrowser 1.0.0.19`, log `m10-engine-48-run-v19.log` (167012 bytes), all true:
  - `cert-owner-armed`, `cert-peer-armed` (distinct issuers despite ID 1).
  - `cert-cross-rejected`, `cert-cross-intact` (A's token dies on B; a failed consume mutates nothing).
  - `cert-stale-rejected` (superseded revision dies).
  - `cert-warn-preserved`, `cert-generic-invalidated` (token follows the same split as the recorded failure).
  - `cert-consumed`, `cert-port-isolated` (peer loop allowed; peer other port and owner loop denied), `cert-used-rejected`.
  - `cert-refail-armed`, `cert-remote-invalidated`, `cert-peer-torn-down`.
  - No `timeout`, `suite-completed: 1`.
- The `session-isolated` conjunct (`!allowsRequest(ownerSession, loopURL)`) is asserted inside `cert-port-isolated` rather than as its own marker; the single marker passing proves all three enforcement conjuncts.
- No regressions in the same run: cookie teardown, terminal duplicates, replaced-final, warning/generic preservation markers all true; no late-success marker.
- `navigation-proceed-reserved` never fired (nothing navigates a reserved URL in the suite); the fail-closed branch is code-reviewed, not device-proven.

Final DLL SHA-256:

```text
4.8 WebCore.dll: 68201A456DCC5A4EFD8989B370B705506E399EB2693442637C90A86233A6EC01
```

## Not claimed

- Retry-execution success is not device-proven: the fixture has no TLS endpoint, so a consumed token arms enforcement that the loop failure cannot exercise. Atomic single consumption plus scoped enforcement are proven; the retry navigation itself is section-5/8 work.
- The public `proceedWithCertificateFailure` slot stays unimplemented; host-facing token plumbing arrives with the section-5 component.
- `native-fault-code 0xe06d7363` fires as in 4.1-4.7 runs; the suite still completes. Still unexplained, unchanged by 4.8.
