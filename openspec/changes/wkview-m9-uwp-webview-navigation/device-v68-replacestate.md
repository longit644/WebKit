# MiniBrowser 1.0.0.68 replaceState history acceptance run

Device: Windows 10 Mobile ARM32, Device Portal `https://192.168.1.52`.
Package: `WebKitWebView.MiniBrowser_1.0.0.68_arm__2932e6rj4m41p`
(install verified in the package list before launch; app terminated before
launch for a fresh log).
Startup: `http://192.168.1.69:8002/index.html?uwp-suite=1`.
Both fixture servers running (ports 8002 + 8003).

Fixture: `/replacestate.html` runs `history.pushState({}, "", "?v=1")` then
`history.replaceState({}, "", "?v=2")` on load and writes
`PASS replaced: ?v=2` into the DOM. Suite phases 24–28 (PageUWP.cpp): wait
for the `?v=2` URL, engine Back, verify the landing URL, engine Forward,
verify the `?v=2` URL again, return to index.

A 1.0.0.67 run first caught a test-design error (since corrected): Back from
`?v=2` lands on the `replacestate.html` page-load entry with an empty query —
pushState+replaceState net one entry on top of the page-load entry, not zero.
Expecting index.html there timed out phase 26, correctly reporting
`fixture-suite-completed: 0`.

## Results from LocalState/minibrowser.log (1.0.0.68 fresh run)

| Check | Evidence | Result |
| --- | --- | --- |
| push+replace recorded as `?v=2`, script ran | replacestate-url=1 | Pass |
| Back from replaced state | replacestate-back=1 | Pass |
| Back lands on no-query URL (no `?v=1` entry) | replacestate-back-noreplace=1 | Pass — proves replaceState mutated instead of pushing |
| Forward to replaced state | replacestate-forward=1 | Pass |
| Forward lands on `?v=2` | replacestate-forward-document=1 | Pass |
| Suite completion | fixture-suite-completed=1 | Done (completion marker, not an aggregate verdict) |

If `replaceState` had pushed a third entry, Back would have landed on `?v=1`
and phase 26 would have timed out instead of passing.

## Coverage limits

- History `state`-object identity across traversal is not asserted (no JS
  evaluation bridge exists to read it back); URL-based verification only.
- With this run, engine-history coverage is: Back/Forward/Reload, anchor
  same-document + Back, `pushState` + traversal, `replaceState` no-push +
  traversal, forward truncation on new navigation, scroll restoration.
