# M10 7.4: capability reporting and platform-service docs

## Reporting rule

A capability bit is advertised only when its end-to-end production path works:
the engine must honour the bit and the control must consume it through
production forwarding. Engine-side work alone does not advertise; neither does
a host-side service the engine never calls. The mask therefore stays zero:
production initialization still fails Unsupported by design, engine slots
report Unsupported, and the 7.5 runtime owner is unwired. Flipping any bit now
would let negotiation succeed and fail at use — the silent-failure mode task
3.2 prevents. Per-bit engine state and unblocking conditions are recorded in
the provider ledger; per-service availability lives in `API/uwp/USAGE.md`.

## What changed

- `Source/WebCore/platform/uwp/WKUWPEngineAPI.cpp` (comments only, no
  behavior change): the mask rule now states the end-to-end condition instead
  of "still being built", and the per-bit ledger records engine state plus the
  unblocking condition for all ten bits. The stale line claiming the clipboard
  bit arrives "with the engine clipboard forward in task 7.2" is corrected:
  7.2 shipped the device-proven host route while the engine still self-serves
  clipboard internally, so the bit needs an engine-forward decision plus 7.5
  wiring.
- `API/uwp/USAGE.md` "Deferred capabilities": per-service availability list
  (navigation/HTML/certificates, CPU/GPU presentation, input/focus, clipboard,
  sessions/trust/fonts) naming the required bit and the fail-closed behavior
  where unwired.
- `artifact/public-api.md`: capability reporting rule with pointers to the
  provider ledger and the USAGE.md list.
- `artifact/migration-map.md` M7 row: precise current state — canonical font
  fingerprint, device-proven host clipboard route beside the retained
  engine-internal path, no-op drag images, 7.5 wiring pending, nothing
  regressed.

## Consumer reporting verified to match

- `PageControllerUWP::bind` requires exactly StructuredNotifications,
  HostHtml and CertificateDecisions — the bits for the slots it forwards
  (setCallbacks/navigate/loadHtml/history/reload/stop/proceed/closePage).
- `sendInput`/`setFocused` require the Input bit — the bit for the slots they
  forward.
- `SwapChainPresenterUWP::initializeForCompositor` requires the GPUCompositor
  bit — the bit for the compositor slots it attaches.
- With the provider mask at zero, every one of these fails fail-closed
  (E_NOTIMPL) and every requested bit fails negotiation with
  MissingCapability; nothing claims an unhonored capability from either side.

## Checks and commands

On 2026-10-10, PowerShell 7.6.6, MSVC 14.44.35207, SDK 10.0.19041.0,
native ARM clang-cl 19.1.5:

```powershell
.\Tools\uwp\build\test-wkuwp-engine-negotiation.ps1 -OutDir "$env:TEMP\opencode\m10-74-negotiation"
cmake --build build-arm-uwp-fresh --target WebCore --parallel 8
python Tools/uwp/audit-source-docs.py --check Source/WebCore/platform/uwp/WKUWPEngineAPI.cpp
```

- Negotiation gate rerun: all discovery checks pass, including
  MissingCapability for every requested bit with outputs untouched and
  delivered mask `0x0` — reporting behavior unchanged.
- Affected engine target rebuild after the ledger edit: the touched TU
  recompiles and `WebCore.dll` relinks; rebuilt image is ARMNT/AppContainer
  with 24 legacy exports plus discovery. Remaining warnings are the known
  toolchain/PCH notes, unchanged from prior builds.
- Documentation audit on the edited provider: 0 candidates. Edited-file line
  endings kept uniform (CRLF file stays CRLF); `git diff --check` clean.

## Remaining gates

Mask changes belong to the tasks that complete each end-to-end path; the first
candidate is 7.5 wiring for the clipboard bit, pending its engine-forward
decision. Sections 8-11 own production-forwarding integration for the rest.
No advertising change is left open by this review.
