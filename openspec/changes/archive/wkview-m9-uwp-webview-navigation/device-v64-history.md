# MiniBrowser 1.0.0.64 engine-history acceptance run

Device: Windows 10 Mobile ARM32, Device Portal `https://192.168.1.52`.
Package: `WebKitWebView.MiniBrowser_1.0.0.64_arm__2932e6rj4m41p`
(App ID `WebKitWebView.MiniBrowser_2932e6rj4m41p!App`; install verified in the
package list before launch — HTTP 202 alone is not sufficient evidence).
Startup: `http://192.168.1.69:8002/index.html?uwp-suite=1`.
Second origin: `http://192.168.1.69:8003`.
Both fixture servers running (ports 8002 + 8003).

Implementation under test:
- `BackForwardClientUWP` (`Source/WebCore/platform/uwp/PageUWP.cpp`): retained
  `Vector<Ref<HistoryItem>>` + cursor, capacity 100, forward truncation on new
  navigation, pointer-identity `goToItem`/`containsItem`, generic parent update
  in `setChildItem`, `close()` clears. Installed per page in `WKCreatePageUWP`.
- `PageFrameLoaderUWP`: `shouldGoToHistoryItem` returns Yes;
  `createHistoryItemTree` delegates to
  `HistoryController::createItemTree` (required: `EmptyFrameLoaderClient`
  returned null, so no item was ever recorded). `EmptyFrameLoaderClient.h`
  changed `final` → `override` on exactly those two declarations.
- Bridge: `WKGoBackPageUWP` / `WKGoForwardPageUWP` / `WKReloadPageUWP` /
  `WKStopPageUWP` + `canGoBack`/`canGoForward` in `WKPageStateUWP`.
- Shell: Back/Forward/Reload buttons call the engine actions (Forward button
  added); the `m_history` URL vector is removed entirely.

## Results from LocalState/minibrowser.log

| Check | Evidence | Result |
| --- | --- | --- |
| Back after two entries | back-count=2, history-back=1 | Pass |
| Back lands on index | history-back-document=1 | Pass |
| Forward | history-forward=1, forward-document=1 | Pass |
| Reload second.html | reload-requested=1, reload-document=1 (new provisional load observed) | Pass |
| Same-document anchor | same-fragment=1, same-scrolled=1 (scrollY > 0) | Pass |
| Back from anchor to second | same-back=1, same-back-document=1 | Pass |
| HTTP-error document | http-error-document=1 | Pass |
| New navigation truncates forward | branch-forward-count=0, branch-truncated=1 | Pass |
| Latin-1, CSP script/image, CORS allow/deny | all 1 | Pass |
| Replacement cancels pending 3 s image | replacement-pending=1, replacement-stable=1 | Pass |
| Stop cancels pending 3 s image | stop-pending=1, stop-stable=1 | Pass |
| Scroll restoration across Back | scroll-before=0x12c (300), scroll-after=0x12c, scroll-restored=1 | Pass |
| Suite completion | fixture-suite-completed=1 | Done (completion marker, not an aggregate verdict) |

No `uwp-load-data` was ever logged for a `delay?ms=` URL: abandoned delayed
loads delivered no bytes to any document.

## Attribution (closed with MiniBrowser 1.0.0.65)

1.0.0.65 logs the failing URL in both fail paths. The 1.0.0.65 run reports:

```
navigation-error-url: http://192.168.1.69:8002/slow.html: 0x00000001
```

with the suite completing normally. Root cause, traced in source:

- `LoaderStrategyUWP::cancelledError` (`LoaderStrategyUWP.cpp:279`) returns
  `loadError(url, 1, "Load cancelled")` — code 1 is our own cancellation code.
- It surfaces via `FrameLoader::cancelledError` → `dispatchDidFailLoad` when an
  in-flight page load is stopped or replaced.
- The failing URL is always slow.html: the only page abandoned while still
  loading (its 3 s delayed image pending) — by Stop once and by replacement
  navigation twice in the 1.0.0.64 run. The count varies with timing (whether
  the image was still pending at abandonment); 1.0.0.65 caught one instance.

This is correct, expected behavior: a cancelled load reporting cancellation for
the page being abandoned. No data was delivered to any document in any case
(no `uwp-load-data` for any `delay?ms=` URL in either run), and every stability
assertion holds. Cancellation acceptance for the delayed-image cases is now
fully closed. (Page/control-teardown cancellation remains a separate open item
under 4.4.)

## Coverage limits

- Same-document coverage is anchor navigation + Back only; `pushState`/`replaceState`
  JS-API history behavior is not exercised.
- Reload verified for a full-document GET reload only.
- Local `about:` pages now go through engine actions; engine history does not
  record app-supplied HTML loads, so Back/Reload there are no-ops by design.
  The local demo text still invites Back/Reload and should be revisited.
- HTTPS/mixed-content/invalid-cert, budgets, page/control teardown, lifecycle:
  still outstanding.
