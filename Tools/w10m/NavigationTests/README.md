# W10M navigation/resource fixtures

These deterministic HTTP endpoints verify the M9 resource-loader integration. They do not themselves implement a browser. Response checks validate the fixture server; native/device checks must establish resource execution, security and cancellation.

## Layout (M10 task 9.1)

- `host/fixtures/server.py`: canonical fixture host (moved from this directory).
- `host/fixtures/test_server.py`: canonical host fixture tests (15 tests).
- `protocol/run_report.py`, `protocol/aggregate.py`: canonical 9.2 report protocol.
- `protocol/test_reports.py`: canonical protocol tests (14 tests).
- `tls/`: fixture TLS assets (kept here; build scripts copy `tls/fixture-ca.pem`).
- Root `server.py`, `test_server.py`, `run_report.py`, `aggregate.py`,
  `test_reports.py` are thin forwarders so every previously documented
  command and import path keeps working.

## Host verification

From the repository root:

```powershell
python -m unittest discover -s Tools/w10m/NavigationTests/host/fixtures -p test_server.py -v
python -m unittest discover -s Tools/w10m/NavigationTests/protocol -p test_reports.py -v
python Tools/w10m/NavigationTests/host/fixtures/server.py --host 0.0.0.0 --port 8000
```

Legacy equivalents through the root forwarders run the same suites:

```powershell
python -m unittest discover -s Tools/w10m/NavigationTests -p test_server.py -v
python -m unittest discover -s Tools/w10m/NavigationTests -p test_reports.py -v
python Tools/w10m/NavigationTests/server.py --host 0.0.0.0 --port 8000
```

For cross-origin tests run a second instance on port 8001. Use this PC's LAN IPv4 address from the phone, not localhost. Both ports must be reachable. This is a test server, not an Internet-facing service. HTTPS validation remains a separate valid/invalid-certificate test; HTTP fixtures do not bypass TLS checks.

## Device cases

- `/index.html`: heading becomes green, script text says `first,second`, green image and external font render; fetch/XHR/cookie markers say PASS. `/requests` lists requested paths without cookie/header contents.
- `/redirect`: commits `/index.html` and resolves its relative assets. `/second.html`: Back/Forward and title/address changes.
- `/latin1.html`: displays cafe with its accented final character. `/gzip.html`: normal fixture HTML after decoding.
- `/missing.html`: displays the 404 document with observable HTTP status. `/download`: explicit download/unsupported-content host policy, not successful HTML navigation.
- `/cross-origin.html?origin=http://PC-IP:8001`: permitted CORS response readable, unapproved response blocked. Same hostname/different port is a different origin.
- `/policy.html`: inline script and image are blocked by response CSP. Inspect `window.fixtureBlocked` through the host script API: it must be undefined.
- `/cookies/set` then `/cookies/echo`: same-session cookie persistence; a separate ephemeral session returns an empty cookie field. Clearing a persistent profile removes the cookie.
- `/slow.html`: input remains responsive; Stop or navigate away before its delayed image completes. No late image/event updates on the replacement page.
- `/large?bytes=1048576`: response above the old 512 KB shell cap; configured budget failures must be explicit. Server responses are capped at 4 MB.
- `/redirect-loop`: bounded failure without indefinitely active loading.

Mixed-content checks require serving a fixture through HTTPS with an HTTP resource URL. Invalid-certificate checks require a deliberately untrusted HTTPS endpoint. Neither is simulated by this HTTP server. Record the chosen device-accessible HTTPS endpoints and certificate setup in test evidence when those gates are executed.

Do not mark resource/security scenarios complete from server tests: the browser must visibly apply CSS, execute ordered scripts, decode the image/font and enforce origin policy on the phone.
