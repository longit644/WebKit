# MiniBrowser 1.0.0.63 deterministic acceptance run

Device: Windows 10 Mobile ARM32, Device Portal `https://192.168.1.52`.
Package: `WebKitWebView.MiniBrowser_1.0.0.63_arm__2932e6rj4m41p`.
Startup: `http://192.168.1.69:8002/index.html?uwp-suite=1`.
Second origin: `http://192.168.1.69:8003`.

Both fixture servers must stay running. The first attempt timed out with curl
error 28 while the owner had stopped the server. After the owner restarted the
servers, the installed package completed the suite. The earlier timeout remains
in the append-only log and must not be mistaken for a failure of the completed run.

## Results collected from LocalState/minibrowser.log

| Check | Evidence | Result |
| --- | --- | --- |
| Downloaded web font selected | custom=1, interstitial=0, glyph=1 | Pass |
| Redirect final document | fixture-suite-redirect=1 | Pass |
| Engine Back history | back-count=0, history-back=0 | **Fail** |
| Engine Forward history | Not reached after Back failed | Not tested |
| HTML HTTP-error document | fixture-suite-http-error-document=1 | Pass |
| ISO-8859-1 decoding | fixture-suite-latin1=1 (`café` in DOM text) | Pass |
| CSP inline-script block | fixture-suite-csp-script=1 | Pass |
| CSP image block | fixture-suite-csp-image=1 | Pass |
| Cross-origin CORS allow | fixture-suite-cors-allowed=1 | Pass |
| Cross-origin CORS deny | fixture-suite-cors-blocked=1; FAIL exposed absent | Pass |
| Replace page with pending image | replacement-pending=1, replacement-stable=1 | Pass for this case |
| Stop with pending image | stop-pending=1, stop-stable=1 | Pass for this case |

Both cancellation cases start `/delay?ms=3000`. There are no data/complete
callbacks for these delayed URLs in the captured log; the runner checks page
stability 3500 ms later. This verifies delayed image cancellation and replacement
on a live page, not every teardown, main-document cancellation or terminal-event
ordering scenario.

`fixture-suite-completed=1` records completion of the runner, **not** an aggregate
PASS. It returns to index.html afterward. CSS/script/fetch/XHR/cookie settle
markers remain successful on that fixture. The owner previously confirmed the
green PNG is visible in 1.0.0.62; PNG requests continue completing in this build.

## History defect attribution

`WKCreatePageUWP` uses `pageConfigurationWithEmptyClients`. Its
`EmptyBackForwardClient` (`Source/WebCore/loader/EmptyClients.cpp:122`) discards
HistoryItem additions, returns null for every lookup, and reports zero back and
forward counts. No browser-capable history client has replaced it yet.
MiniBrowser's existing toolbar still uses a separate shell URL vector, which
does not satisfy engine-history acceptance. Next implementation work must retain
WebCore HistoryItems through a proper BackForwardClient and wire toolbar actions
to the engine, then re-run Back/Forward/Reload and same-document cases.

## Other coverage limits

HTTPS/mixed-content and invalid-certificate acceptance remain outstanding;
the separate OpenSSL ARM32 crash is still open. Resource budgets, page/control
destruction, event/navigation IDs, history state/scroll restoration and lifecycle
acceptance are not covered by this run.

## Host checks

`python -m unittest discover -s Tools/uwp/NavigationTests -p test_server.py -v`
passed all six fixture-response tests before deployment. PageUWP's updated
diagnostics compiled for ARM and WebCore relinked successfully. The existing
engine build cache was restored following an RC-compiler-triggered reset, but
normal Ninja still plans a large rebuild; see handoff for build reconciliation.
