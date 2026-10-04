# MiniBrowser 1.0.0.66 pushState history acceptance run

Device: Windows 10 Mobile ARM32, Device Portal `https://192.168.1.52`.
Package: `WebKitWebView.MiniBrowser_1.0.0.66_arm__2932e6rj4m41p`
(install verified in the package list before launch).
Startup: `http://192.168.1.69:8002/index.html?uwp-suite=1`.
Both fixture servers running (ports 8002 + 8003; servers must be restarted
after any `server.py` change — the first 1.0.0.66 run hit the old server
without `/pushstate.html` and timed out at phase 21, which correctly produced
`fixture-suite-completed: 0` rather than false passes).

Fixture: `/pushstate.html` runs `history.pushState({}, "", "?pushed=1")` on
load and writes `PASS pushed: ?pushed=1` into the DOM. Suite phases 20–24
 (PageUWP.cpp): wait for the pushed URL, engine Back, verify the un-pushed
same-document URL, engine Forward, verify the re-pushed URL, return to index.

## Results from LocalState/minibrowser.log (fresh run after app terminate)

| Check | Evidence | Result |
| --- | --- | --- |
| pushState entry recorded + script ran | pushstate-url=1 (URL query `pushed=1`, DOM `PASS pushed`) | Pass |
| Back from pushed state | pushstate-back=1 | Pass |
| Back lands on un-pushed same-document URL | pushstate-back-document=1 (path same, query empty, document Complete, no reload) | Pass |
| Forward to pushed state | pushstate-forward=1 | Pass |
| Forward lands on pushed URL | pushstate-forward-document=1 | Pass |
| Suite completion | fixture-suite-completed=1 | Done (completion marker, not an aggregate verdict) |

No new navigation transfers occur for the Back/Forward traversals (same
document, no reload), which is the correct pushState behavior. The earlier
phases (redirect, Back/Forward, Reload, anchor, truncation, scroll, security,
cancellation) are unchanged from the 1.0.0.64 run and were not re-asserted here
beyond reaching phase 20.

## Coverage limits

- `replaceState` (which modifies the current entry rather than pushing) is not
  exercised; only `pushState` + traversal is verified.
- History `state` object identity across traversal is not asserted, only URLs.
