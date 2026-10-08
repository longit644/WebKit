# M10 4.11 - Immutable trust snapshots and font configuration freeze

TLS verification uses immutable per-session CA-file snapshots enforced per
transfer, with no cross-session connection reuse and no native-store
augmentation. Font configuration freezes at first successful setup and
conflicting later requests fail before page creation. Proven on the Lumia
with a real TLS fixture endpoint and two controls on opposite bundles.

## Delivered

- `Source/WebCore/platform/uwp/TlsPolicyUWP.h/.cpp` - per-session CA-file
  snapshots (path plus SHA-256 over the bytes read at snapshot time),
  first-wins immutability, and teardown erasure. Shared digest helper.
- `Source/WebCore/platform/network/curl/CurlRequest.h/.cpp`,
  `CurlContext.h/.cpp` - additive per-request SSL policy: explicit CA
  bundle, native-store augmentation flag, connection reuse flag, applied at
  handle setup after process defaults. Default callers keep byte-identical
  handle setup on every platform.
- `Source/WebCore/platform/uwp/ResourceLoadUWP.cpp` - every HTTPS transfer
  stages its session snapshot with native augmentation off and reuse off.
- `Source/WebCore/platform/uwp/PageUWP.cpp` - navigations snapshot instead
  of mutating the process trust singleton; teardown erases trust and policy
  sessions alongside cookies, consent and cache partition.
- `Source/WebCore/platform/uwp/EngineUWP.h/.cpp` - frozen font directory
  plus content fingerprint (SHA-256 over sorted font names and contents);
  mismatches report new code 9 before any page creation, live pages
  untouched.
- `Tools/uwp/NavigationTests/server.py` - additive `--tls-port` (with
  `--tls-cert/key/ca` defaulting to `tls/`), serving the same fixtures over
  TLS. `tls/` holds the fixture CA, server cert/key (SAN IP + localhost)
  and an unrelated CA; chain verified (server validates against fixture CA,
  not against the unrelated one).
- Suite phases 78-91: owner bundle presence, peer bundle snapshot/hash/
  immutability, owner fail-closed, peer bundle verification, second-peer
  unrelated snapshot, concurrent opposite outcomes under one suspension,
  font conflict, dual teardown with survivor intact.

## Verification

```
cmake --build build-arm-uwp-fresh --target WebCore --parallel 8
```

- Fresh-tree `WebCore` links: ARMNT, AppContainer, 25 exports.
- Device run `WebKitProbe.EngineBrowser 1.0.0.33`, log
  `m10-engine-411-run-v33.log` (217213 bytes), all true:
  - `tls-owner-bundle` (shell startup bundle present, first-wins context).
  - `tls-snapshot-a/hash/immutable` (peer bundle frozen, rebinding ignored).
  - `tls-owner-fails-closed` (package bundle rejects the fixture server,
    code 60: no native-store augmentation sneaks the private CA in).
  - `tls-peerA-committed` (bundle A verifies over real TLS).
  - `tls-snapshot-b`, `tls-concurrent-owner/peer` (opposite outcomes under
    one suspension, sharing only timing).
  - `tls-font-conflict` (code 9), `tls-peerB-torn-down`,
    `tls-teardown-intact` (survivor keeps its frozen hash).
  - No `timeout`, `suite-completed: 1` (all 4.3-4.10 gates pass in-run).
- Fixture server invocation (host):
  `server.py --host <LAN-IP> --port 8002 --tls-port 8004`
  (second instance for `:8003` as before; TCP 8004 inbound allowed).
- Findings on the way, all fixed and re-proved here:
  - HTTPS navigations must bypass the relative-only suite helper otherwise
    the TLS base appends to the HTTP base (caught by log inspection).
  - `CurlRequest` builds its easy handle asynchronously, so per-transfer
    options must be staged before scheduling, not poked afterwards.
  - The suite must snapshot fixture bundles on fresh peers: the shell's
    startup navigation legitimately freezes the owner session first.

Final DLL SHA-256:

```text
4.11 WebCore.dll: A676C97C262DF6B713AE548C37BB509582A10D4EC7DB90110A9A23CC5CD22C1D
```

## Not claimed

- Retry-execution after token consume still has no TLS exercise beyond
  enforcement (4.8); public token plumbing waits for section 5.
- Queue-overflow code 14 stays implemented but unreachable (4.9).
- Other web storage APIs remain out of scope; session TLS options wiring
  belongs to the public component (5.2).
- `native-fault-code 0xe06d7363` fires as in 4.1-4.10 runs; the suite still
  completes. Still unexplained, unchanged by 4.11.
