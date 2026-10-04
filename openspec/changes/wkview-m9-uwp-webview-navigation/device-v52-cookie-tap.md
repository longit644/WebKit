# M9 v1.0.0.52 cookie/tap investigation

## Observed evidence

- v49-v51 failed when the first request queried the new per-page cookie session.
- v51 native fault logging identified an access violation at `0x6a66691d`
  in `sqlite3.dll` (base `0x6a620000`, instruction RVA `0x4691c`).
- Disassembly showed a hash bucket indexed by the remainder in `r1` after
  calling an obsolete ARM division shim. That shim neither reordered the
  denominator/numerator nor returned the required remainder.
- SQLite was clean-rebuilt against the current `Tools/uwp/lib/arm-crtstart.lib`.
  The rebuilt call targets a shim that swaps arguments, passes a remainder
  pointer to the compiler helper, and reloads the remainder into `r1`.
- v52 installation reported Success=true; launch returned HTTP 200.
- Device logs show the cookie query returning length 10 for `/cookies/echo`
  after `/cookies/set`, and a completed 24-byte echo response.
- Several pointer sequences reached `tap-stage-after-dispatch` for both press
  and release with no native fault recorded in the retrieved log.

This establishes recovery from the cookie-query startup crash and successful
mouse-event dispatch in the observed sequences. It does not establish link
activation, button handler execution, session-isolation acceptance, persistent
storage, or the complete M9 milestone. Owner confirmation/capture of PASS cookie
and link/button outcomes remains necessary.

## Code corrections

- XAML `GetCurrentPoint(element)` coordinates are device-independent pixels;
  do not divide them by SwapChainPanel composition scale.
- Hit-test diagnostics must use a checked anchor downcast, including when
  the hit node is a button, text node, or other element.
- DOM and HTTP cookies now use the same ephemeral per-page storage session.
  The adapter reads/writes cookies using WebKit's NetworkDataTaskCurl pattern,
  respecting `allowCookies()` and keeping Set-Cookie fields separate.

## Required staging override

Until the rebuilt SQLite is installed into the dependency prefix, stage with:

```text
--override C:/vcpkg/buildtrees/sqlite3/arm-uwp-webkit-rel
```

Using the old installed SQLite DLL reintroduces the broken ARM division shim.
The clean rebuild requires SDK um/shared/ucrt and MSVC include directories in
`INCLUDE` for the resource compiler. Existing FreeType, fontconfig, HarfBuzz and
ICU overrides remain required by the established Skia staging recipe.
