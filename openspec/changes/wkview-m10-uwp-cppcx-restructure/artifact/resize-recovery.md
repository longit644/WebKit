# M10 6.3: resize evidence (partial — task remains open)

## What changed

- `TextureMapperCompositorUWP::resize` now invalidates the offset subtree and the
  page root in addition to the viewport. Rationale: in the composited-scrolling
  path the viewport draws no content itself, so invalidating it alone leaves
  portrait-rasterized document tiles cached across a rotation. The next frames
  then show stale portrait-width tiles until a scroll repaints them — exactly the
  reported symptom (clipped landscape that corrects after interaction).
- `WebKitWebViewUWP::resize` no longer paints synchronously inside `SizeChanged`.
  It marks redraw pending so presentation happens on the next SDK Rendering
  callback, after ANGLE has consumed its own panel notification. Previously the
  wrapper could paint into the old surface while ANGLE still reported old extents.
- `TextureMapperCompositorUWP::render` synchronizes the actual EGL backing size
  against the expected CSS/scale geometry before drawing, keeps frame demand while
  a panel notification is in flight, and logs bounded `gpu-surface-resize`
  diagnostics (revision, CSS, scale, target, before/after, attempt, ready).
- The MiniBrowser sample sets `ApplicationViewBoundsMode_UseVisible` before any
  content exists and keeps the view alive (`minibrowser-desired-bounds-visible`).
  No fullscreen/transparent overlay is requested: content must scale above the
  opaque system bars like Edge, not render beneath them.

## Device evidence (v39)

Lumia 950 XL, `mc-001173b46c8b`, `MobileCoreProd`, OS
`15254.603.armfre.feature2_rs3svc.200106-1633`, portal `https://192.168.1.52`.
Package `WebKitProbe.EngineBrowser_1.0.0.39_arm__2932e6rj4m41p`, App ID
`WebKitProbe.EngineBrowser_2932e6rj4m41p!App`. Install Code=0/Success=true,
launch HTTP 200.

Rotation portrait → landscape → portrait → landscape, pausing without
tap/scroll:

```text
control-viewport-width/height: 411x663 -> 731x343 -> 411x663 -> 731x343
gpu-surface-resize revision=2 css=731x343  scale=3.500 target=2559x1201 before=1440x2322 after=2560x1202 attempt=1 ready=1
gpu-surface-resize revision=3 css=411x663  scale=3.500 target=1439x2321 before=2560x1202 after=1440x2322 attempt=1 ready=1
gpu-surface-resize revision=4 css=731x343  scale=3.500 target=2559x1201 before=1440x2322 after=2560x1202 attempt=1 ready=1
```

No `gpu-surface-resize-unresolved`, no CPU fallback, no frame/render failure in
the run. One `control-compositor-initialize`, one exclusive GPU attachment, no
duplicate swap chain. Native `page.png` capture (`32E3ACE8...`, 229842 bytes)
shows the full correct layout including the bottom-right fixed layer, proving
engine layout/viewport geometry is right.

## Still open (why 6.3 is not checked)

1. **Landscape system navigation-bar overlay in the sample shell.** Owner photos
   on v39 show the left-side system bar covering page content in landscape while
   portrait lays out correctly above the opaque bar. `UseVisible` is confirmed
   active (`minibrowser-desired-bounds-visible: 1`) yet the panel still reports
   full-window width (731), so the overlay is shell chrome the engine cannot see:
   EGL reaches the right size and the native capture is complete. Proper
   `VisibleBoundsChanged` handling needs the typed delegate, which has no native
   MIDL specialization for WRL; it belongs to the C++/CX app-shell migration
   (task 8.1), where the subscription is ordinary CX code. Manual bar hiding
   shows the full page, confirming engine output is correct.
2. **Lost-device recovery is untested.** No device-removed/device-reset path has
   been exercised; presenter teardown/reattach on `DXGI_ERROR_DEVICE_REMOVED`
   is not implemented.

SHA256:

| Payload/report | SHA256 |
|---|---|
| Signed v39 APPX | `C8D977AD46F63E3D139AE2EEDDA9B9AA3824CF8C6A91C9EDD6404A8B3AE8EBCE` |
| Landscape log, 220870 bytes | `8C9C0371D6ECFF054B3DDDD3E42D3C487A6448FF54BDCF0845470AE3D0C70112` |
| Native page.png, 229842 bytes | `32E3ACE8A27CAC8006D1627F7B239296E8706635200D69DFAC7631CE90B679EF` |
| Rebuilt WebCore.dll | `9213C1BBE442AD02493796C945FCB3805B2A8082E6D4A12018021E423C18B5CF` |
| Rebuilt MiniBrowserUWP.exe | `E63C208662028C8B0266E6CBF4AF76D0A469CF084B21B9AC01F51EBC3E22ABF4` |

## Bonus: OVERRIDE page is expected diagnostics

The `OVERRIDE Navigation failed` screen for `http://192.168.1.69:9/second`
(error 2) is the packaged blocked-template test using closed port 9. The same
run logs `fixture-suite-completed: 1` and `minibrowser-blocked-test-done: 2`.
Not a product failure.
