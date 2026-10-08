# M10 4.3 - NavigationControllerUWP IDs, generations and typed signals

Per-navigation state (source/title/HTTP/change/redirect-hop/failed-URL) moved
into `NavigationControllerUWP`; suite commands allocate fresh IDs and every
commit, redirect, traversal, reload and same-document mutation is attributed
by real loader signals on the Lumia.

## Delivered

- `Source/WebCore/platform/uwp/NavigationControllerUWP.h/.cpp` - `beginNavigation`
  mints monotonically increasing nonzero IDs; `activeNavigationId`,
  `loadGeneration` (provisional counting) and `documentGeneration` (commit
  counting) are separate counters; typed setters/getters cover source, title
  (+UTF-8), HTTP status, change kind, redirect hops and the failing URL.
- `Source/WebCore/platform/uwp/FrameLoaderClientUWP.h/.cpp` - provisional,
  commit, failure, response, title, fragment, pushState and replaceState hooks
  feed the controller; the suite `navigate` helper allocates the command ID
  first and stashes it for commit attribution.
- `Source/WebCore/platform/uwp/PageHostUWP.h` - owns the controller as
  `navigation`; `PageUWP.cpp` forwards the C entry points; `PlatformWin.cmake`
  adds the new TU.

## Verification

```
cmake --build build-arm-uwp-fresh --target WebCore --parallel 8
```

- Fresh-tree `WebCore` links: `bin/WebCore.dll` is
  `IMAGE_FILE_MACHINE_ARMNT`, AppContainer, 25 exports (24 legacy plus
  `WKUWPGetEngineAPI`).
- Device run `WebKitProbe.EngineBrowser 1.0.0.5` on the Lumia 950 XL,
  log `m10-engine-43-run-v5.log` (102477 bytes):
  - `fixture-navigation-allocated`: 16/16 nonzero and distinct from the
    previous active ID - every suite push mints a fresh command ID.
  - `fixture-navigation-redirect-stable: 1` - `/redirect` lands on index.html
    under the command ID with the document generation advanced.
  - `fixture-navigation-traversal-stable: 1` - back/forward allocate nothing.
  - `fixture-navigation-reload-allocated/new: 1` - same-URL reload mints a
    fresh ID and the reloaded commit keeps it with a newer generation.
  - `fixture-navigation-fragment-stable`, `push-stable`, `replace-stable`:
    1 - fragment scroll, pushState and replaceState keep the command ID.
  - `fixture-navigation-http/url: 1` - controller accessors report HTTP 200
    and the index.html source URL of the landed document.
  - `fixture-suite-completed: 1`.

Final DLL SHA-256:

```text
4.3 WebCore.dll: C70BA888C3B8FA9541499ED1665732EFC6441C001A267E9DEE558021DCE15335
```

## Not claimed

- `control-resize` remains `0`: landscape rotation is still cropped/stretched;
  recovery behaviour is task 6.3.
- `native-fault-code 0xe06d7363` fires at startup and shutdown as in the 4.1/4.2
  runs; the suite still completes. The fault is unexplained, unchanged by 4.3.
- Exactly-once terminal outcomes for veto/Stop/replacement/failures are task
  4.4; 4.3 proves attribution, not terminality.
