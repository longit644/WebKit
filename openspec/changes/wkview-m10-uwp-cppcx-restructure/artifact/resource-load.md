# M10 4.6 - ResourceLoadUWP extraction with unchanged delivery

The file-local curl transfer class moved to its planned files with no
behavioral change: cookies, redirects, trust exceptions, budgets, error
mapping and transfer logging are the same code in a new TU. The full device
suite passes on the extracted build with identical delivery markers.

## Delivered

- `Source/WebCore/platform/uwp/ResourceLoadUWP.h/.cpp` - the transfer class
  moved verbatim (create/ref/deref/resume/cancel/start/fail plus the five
  curl callbacks and its retained state), with its file-local transfer
  machinery: the log sink, `reportTransfer`, `reportTransferURL` and the
  shared adapter-domain `loadError` builder (declared in the new header for
  the strategy's error factories).
- `Source/WebCore/platform/uwp/LoaderStrategyUWP.cpp` - keeps the strategy
  only: subresource creation, transfer map, suspend/resume, sync/ping/
  preconnect refusals, the error factories and the proceed-exception map.
  Keeps `DocumentPage.h` (inline `Frame::page`), request/response and URL
  includes its bodies still need.
- `Source/WebCore/PlatformWin.cmake` - the new TU joins the `WebCore`
  target; no header list changes, matching the 4.2 precedent.

## Verification

```
cmake --build build-arm-uwp-fresh --target WebCore --parallel 8
```

- Fresh-tree `WebCore` links: ARMNT, AppContainer, 25 exports.
- Move fidelity is structural, not trusted: no `ResourceLoadUWP` class or
  curl-callback identifier remains in the strategy TU, no strategy state
  (`m_loads`, `m_suspended`) exists in the transfer files, and `git diff
  --check` is clean.
- Device run `WebKitProbe.EngineBrowser 1.0.0.10`, log
  `m10-engine-46-run-v10.log` (145401 bytes), `suite-completed: 1`, no
  timeout, with per-type delivery intact:
  - CSS `fixture-css-heading: 1`, scripts `fixture-script-order: 1`.
  - `fixture-fetch: 1`, `fixture-xhr: 1` (early probes read 0 before the
    async callbacks settle; final reads 1).
  - Image `assets/image.png` MIME length 9 with 73-byte completion on every
    page; font `assets/font.ttf` 633-KB completion with
    `fixture-web-font-custom/glyph: 1`.
  - Encoding `fixture-suite-latin1: 1`; index/style/script/image/font MIME
    lengths and HTTP 200 responses logged per transfer.
  - Cookie probes read pass 0 / fail 0 exactly as in the pre-extraction
    v1.0.0.9 run: unchanged by this task, out of its scope.

Final DLL SHA-256:

```text
4.6 WebCore.dll: CA543B221427902A08AFB5A2FD27108B630438F8B452883FF1F903AC94890FF0
```

## Not claimed

- No behavioral change: security (scheme validation, cookie filtering,
  proceed-exception trust bypass) and response delivery (redirect budget and
  rewriting, MIME/attachment policy, byte budget, curl error passthrough)
  are the same code in new files.
- No new suite phases: extraction adds no engine behavior to prove; the
  full 4.3-4.5 suite passing unchanged IS the verification.
- The unsettled cookie promise probes predate this task and read identically
  before and after; cookie semantics are task 4.7.
