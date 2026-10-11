# M10 task 9.6 — M9 fixture phase/marker replacement ledger

Scope: analysis + this artifact ONLY (M10 task 9.6, G5). No code, no `tasks.md`
edits, no commit. Sibling workers own 9.1–9.5 implementation.

## 0. Sources and status definitions

- M10 scope: `openspec/changes/wkview-m10-w10m-cppcx-restructure/tasks.md`
  lines 230–240 (§9, tasks 9.1–9.8). Task 9.6 text: *"Expand every M9 fixture
  phase/marker into the replacement ledger; run old/new suites on matching
  fixtures and verify covered/internal/approved-gap status for all rows."*
- M9 inventory: the archived M9 change was removed from the tree by commit
  `150d48a54d` ("remove historical M4-M9 OpenSpec archives"). Both M9 inputs
  below were recovered read-only from git history at `150d48a54d^` and are
  quoted, not restored:
  - `openspec/changes/archive/wkview-m9-uwp-webview-navigation/tasks.md`
    (M9 checkboxes 1.1–7.6, device evidence notes v52/v63/v64/v66/v68,
    TLS 2.4 / proceed-72).
  - `.../wkview-m9-uwp-webview-navigation/fixture-extraction-plan.md`
    (19-row assertion-replacement matrix, `FixtureSuitePhase` 0–28 coverage
    check, 6 fixture/test corrections, safe-removal order). This plan is the
    canonical phase/marker inventory for this ledger.
- Current fixture endpoints: `Tools/w10m/NavigationTests/server.py` (+
  `README.md`, `test_server.py`), verified on 2026-10-11 by running the host
  suite: `Ran 9 tests ... OK` (9/9 PASS).
- Structural facts established for this ledger (verified, not assumed):
  - There is **no** same-origin `/report` result endpoint with run-ID
    isolation in `server.py` (plan correction #1, owned by 9.2).
  - There is **no** `window.fixtureBlocked` oracle in `/policy.html`
    (README references it, `server.py` does not define it; correction #2,
    owned by 9.3).
  - There is **no** large `text/html` main-document budget route; `/large`
    serves `application/octet-stream` only (correction #3, owned by 9.3).
  - There is **no** `NavigationTestsW10M` CX device harness yet (owned by 9.4)
    and no run/scenario/assertion aggregation protocol (owned by 9.2).

- Addendum 2026-10-11 (working-tree observation, read-only): sibling workers
  have uncommitted 9.2/9.3 work in `Tools/w10m/NavigationTests/` (modified
  `server.py`/`test_server.py`; new `aggregate.py`, `run_report.py`,
  `test_reports.py`). Observed deltas directly target this ledger's
  PENDING-9.2/9.3 gaps: `POST /report` + `GET /reports` with report-kind
  allowlist and sensitive-key redaction; new self-reporting fixtures
  (`/visual.html`, `/scroll.html`, `/history-report.html`,
  `/fetch-report.html`, `/xhr-report.html`, `/cookie-report.html`,
  `/security-report.html`, `/font-report.html`); a `/large-page` capped
  `text/html` budget route; and a `/policy.html` oracle fix (blocked script
  demonstrably not run). Affected rows: R2, R3, R4, R5, R7, R13 (option b
  now implementable), R17, R19, R26, R29/R30 (visual oracle), R31. Row
  statuses above are CONSERVATIVELY UNCHANGED (sibling code is uncommitted
  and was not host-verified by this worker). 9.6 sign-off and 9.7 MUST
  re-run §3a plus the new report/aggregation tests against the committed
  state and flip each affected gap row to `covered` only on green runs —
  this ledger pins the pre-change baseline, not the outcome.

Status semantics used in §1 (old-vs-new suite status):

- `covered` — the M9 behavior maps to a replacement assertion whose fixture
  endpoint(s) exist in `server.py` TODAY (host-verified) and whose observation
  path uses only the public control API, screenshots, or server logs. Device
  proof may still be pending (stated per row, owned by 9.4/9.5/11.2) — status
  records specification completeness, NOT a passed device run.
- `internal-only` — renderer-private assertion that must NOT move into
  NavigationTests; verified by an internal WebKit test target.
- `approved-gap` — no complete replacement specification today. Each names
  the missing endpoint/route, undecided surface, or owner-deferred scope, with
  the owning task. Prefix `PENDING-9.x` = planned replacement work that 9.7
  must wait for (not owner-deferred). Prefix `DECISION`/`DEFERRED`/`DEVICE`
  = needs explicit owner decision or device-only setup at sign-off.

## 1. Replacement ledger (row per M9 phase/marker)

| # | M9 phase / marker (plan + task source) | Fixture endpoint(s) | Status | Rationale | Exact verification (command or device evidence) |
|---|---|---|---|---|---|
| R1 | `fixture-css-heading`, phase 0 `IndexProbes` (CSS part); M9 4.2 | `GET /index.html`, `GET /assets/style.css` | covered | Endpoint + host test exist; replacement proof is screenshot pixel/color comparison (heading green `rgb(0,128,0)`), since text presence does not prove CSS applied. | Host: `python -m unittest discover -s Tools/w10m/NavigationTests -p test_server.py -v` (`test_external_resource_response_types_and_font`). Device: 9.5 screenshot color assertion on phone aggregate. |
| R2 | External script order `first,second`, phase 0 (M9 4.2; plan: fixture reports order to `/report`) | `GET /assets/first.js`, `GET /assets/second.js` (+ missing `POST /report`) | approved-gap (PENDING-9.2) | Routes/MIME host-verified, but the replacement assertion requires fixture self-report to a same-origin run-ID endpoint that does not exist. Old suite stays authoritative. | Host: same suite as R1 (route/MIME only). New: 9.2 `/report` unit test (structured payloads, aggregate-failure behavior) + 9.5 device order assertion in phone aggregate. |
| R3 | Fetch result marker (`PASS fetch:`), phase 0 (M9 4.3) | `GET /api/data` (+ missing `POST /report`) | approved-gap (PENDING-9.2) | `{"value":"fixture-data"}` host-verified; browser-outcome assertion (JS-visible success posted per run ID + `/requests` shows the fetch) needs 9.2 reporting. | Host: `test_cookie_request_echo_and_data` (data part). Device: 9.5 fetch-report assertion + `/requests` log check, no cookie/credential values in logs. |
| R4 | XHR result marker (`PASS XHR:`), phase 0 (M9 4.3) | `GET /api/data` (+ missing `POST /report`) | approved-gap (PENDING-9.2) | Same as R3 for the XHR path; both must be asserted separately (regression in one must be distinguishable). | Host: same as R3. Device: 9.5 XHR-report assertion; server report + expected-request cross-check. |
| R5 | Cookie pass/fail markers (`PASS/FAIL cookie`), phases 0–1 (M9 2.2; plan: booleans only, pass+fail together) | `GET /cookies/set`, `GET /cookies/echo` (+ missing `POST /report`) | approved-gap (PENDING-9.2) | Set-Cookie value and echo host-verified; replacement must report booleans only (never cookie content), both markers together, plus per-instance isolation via the 9.4 two-instance harness. | Host: `test_cookie_request_echo_and_data`. Device: 9.5 boolean cookie report; 9.4 same-session persistence vs ephemeral-empty check; persistent-profile clearing is R22 (deferred v2). |
| R6 | Web-font `customPlatformData`, `isInterstitial`, glyph lookup, phase 0 (M9 4.2, v63 renderer probe) | `GET /assets/font.ttf` (for serving only) | internal-only | Renderer-private backend assertions; plan correction #6 keeps them in an internal WebKit test target. Device screenshot validates visible fallback/appearance only. Prior M9 device evidence (`device-v63-acceptance.md`: interstitial=0, glyph=1) stays historical, not re-proven via fixtures. | Internal WebKit font/platform test target (sibling-owned). Fixture side: host route test only. Device: optional 9.5 appearance screenshot, not a backend assertion. |
| R7 | Delayed settle probes at 300/1200/3000 ms, per-load/settle probes (M9 4.4; plan: harness bounded-timeout wait) | None — harness mechanism, no fixture route | approved-gap (PENDING-9.2) | Replacement is a harness wait for an explicit fixture report with bounded timeout (9.2 protocol), not timed re-probing. Probe-only load-generation state must be removed at 9.7 while generic navigation IDs/generations needed for engine cancellation are retained. | New: 9.2 malformed/missing/timed-out report tests (single failed assertion fails the run). Device: timeout-failure drill recorded in 9.8 aggregate. |
| R8 | Redirect phase + final query-free index URL, phase 1 (M9 3.1/3.4) | `GET /redirect` (302 → `/index.html`) | covered | Redirect + relative-asset resolution host-verified; replacement observes `SourceChanged` + exactly-one terminal event for the same stable navigation ID, every hop using that ID; `/requests` log confirms hop order. | Host: `test_redirect_error_encoding_compression_and_download` (redirect part). Device: 9.4 typed URI/terminal sequence asserting final query-free URL. |
| R9 | Index → second navigation, Back/Forward availability + document, phases 2–3 (M9 3.3, v64 evidence) | `GET /index.html`, `GET /second.html` | covered | Endpoints host-verified; replacement drives navigation + Back/Forward through the public control and checks `Source`, typed event sequence, history properties. Shell URL-vector history must not be used (already removed, M9 3.3 evidence). | Host: route/content checks in `test_external_resource_response_types_and_font`. Device: 9.4 history sequence incl. title/address changes; Back/Forward availability flags. |
| R10 | Reload request + reloaded page, phase 4 `HistoryReload` and phase 14 `ReloadDocument` (M9 3.3) | `GET /index.html` | covered | Replacement calls public Reload and matches a fresh document completion/report for the same effective URL; Reload must not create a spurious history entry. | Host: R9 command. Device: 9.4 Reload scenario with document-completion + history-entry-count asserts. |
| R11 | `HistoryBackFallback` (phase 5) + `HttpError` (phase 17 tail) (M9 3.3/3.1) | `GET /missing.html` (404 document) | covered | Plan maps fallback to the missing-page checks: 404 HTML is a completed HTTP document (typed completion carries HTTP status), distinct from transport failure. | Host: `test_redirect_error_encoding_compression_and_download` (404 part). Device: 9.4 Back-to-missing + typed terminal-status assert. |
| R12 | Same-document fragment navigation + Back, phases 14–17 (`SameFragment`, `SameBackDocument`) (M9 3.3 anchor evidence) | `GET /index.html` (`#bottom` anchor) | covered | Engine-classified same-document navigation observed via `SourceChanged`/history properties (no URL-fragment-stripping inference). `scrollY`-value reporting belongs to R13. | Host: R9 command. Device: 9.4 in-page anchor activation + Back traversal asserting same-document classification. |
| R13 | Scroll position: `ScrollBefore`/`ScrollBack`/`ScrollRestored`, phases 18–20 (M9 3.3 scroll restoration) | `GET /index.html` (1600px scroll fixture) (+ undecided observation surface) | approved-gap (DECISION, PENDING-9.5) | Plan requires an explicit decision: (a) justified generic observable scroll property, (b) fixture-reported `scrollY` on hash/pageshow (needs 9.2 `/report`), or (c) defer with explicit gap statement. A fixture-only framework probe must NOT be kept to preserve it. | Owner decision recorded at 9.6 sign-off. Device (if (a)/(b)): 9.5 scrollY/fixture-report assert on phone aggregate; native restoration needs visible/UI evidence, never WebCore-internal inspection. |
| R14 | 404 document display + observable HTTP status (M9 3.1; README device case) | `GET /missing.html` | covered | 404 body + status host-verified; device asserts typed completion HTTP status + rendered 404 document. | Host: `test_redirect_error_encoding_compression_and_download`. Device: 9.4 404 scenario (status + document + history state). |
| R15 | Forward-history truncation on new navigation (M9 3.3 v64 evidence) | `GET /second.html`, `GET /index.html` | covered | Replacement checks `Source`/history state after navigating from a Back position; forward entry must be gone. | Host: R9 command. Device: 9.4 truncation scenario (Back, then new nav, then Forward-unavailable assert). |
| R16 | Latin-1 `café` decoding, phase 6 (M9 3.2) | `GET /latin1.html` (`iso-8859-1`) | covered | Byte payload + charset host-verified; replacement observes decoded marker via fixture report (9.2) or visible-text screenshot — either path specified, endpoint ready. | Host: `test_redirect_error_encoding_compression_and_download` (latin-1 part). Device: 9.5 decoded-marker assert or screenshot. |
| R17 | CSP inline-script non-execution, phase 7 (M9 4.3; 9.3 oracle scope) | `GET /policy.html` (CSP: `default-src 'self'; script-src 'none'; img-src 'none'`) | approved-gap (PENDING-9.3) | Header host-verified, but the current oracle is broken by construction: a blocked inline script cannot write its own marker, and `window.fixtureBlocked` does not exist in the fixture. Plan correction #2: prove non-execution via endpoint non-arrival or a separate allowed harness signal, with an intentional-failure variant that the fixture demonstrably detects (9.3 must verify the variant FAILS). | Host: `test_cors_and_policy_headers` (CSP header part). New: 9.3 corrected oracle + negative-variant test. Device: 9.5 CSP scenario on phone aggregate only after the 9.3 fix. |
| R18 | CSP image non-load, phase 7 (M9 4.3) | `GET /policy.html` + `GET /requests` (server request log) | covered | Image non-arrival is provable TODAY: `/requests` lists requested paths (no cookie/header contents), so the harness asserts the image path was never requested while the document marker stays at its blocked value. | Host: `test_cors_and_policy_headers` + request-log behavior. Device: 9.5 assert (document marker + `/requests` absence); server log is the evidence artifact. |
| R19 | CORS allow readable / deny blocked, phase 8 (M9 4.3; needs 2nd origin) | `GET /cross-origin.html?origin=…`, `GET /cors/allow`, `GET /cors/deny`, `OPTIONS /cors/allow|/deny` | approved-gap (PENDING-9.2) | Headers/preflight host-verified, but the plan requires asserting response-*exposure* behavior (allow readable, deny rejected from script), not server response generation — which needs fixture self-report to its own origin (9.2). Second server on `:8001` (same host, different port = different origin) is the documented setup. | Host: `test_cors_and_policy_headers`. Device: two-instance fixture (`--port 8000` + `--port 8001`, phone uses PC LAN IPv4) + 9.5 exposure asserts; second server logs requests, never sensitive headers. |
| R20 | Slow-page replacement, phases 9–11 (`ReplacementPending/Stabilize/Stable`) (M9 3.4/4.4, v63/v64 evidence) | `GET /slow.html`, `GET /delay?ms=3000` | covered | Endpoints + cancel-tolerance host-verified; replacement: harness starts replacement after the slow main document, asserts the old navigation gets exactly one `Cancelled` terminal outcome, the new document stays current, the delayed response never mutates the new page (server record confirms arrival-or-not). | Host: `test_delayed_request_does_not_block_other_clients`, `test_cancelled_socket_and_bounded_large_response`. Device: 9.4 replacement scenario with stable-document check ≥3500 ms after delayed arrival. |
| R21 | Stop while slow resource pending, phases 12–13 (`StopPending`, `StopStable`) (M9 3.4/4.4) | `GET /slow.html`, `GET /delay?ms=3000` | covered | Same endpoints; replacement distinguishes main-navigation cancellation from subresource cancellation: one `Cancelled` outcome, current document unchanged, server records delayed-request completion. | Host: same as R20. Device: 9.4 Stop scenario (Stop after start marker, Cancelled assert, unchanged-document assert). |
| R22 | Persistent profiles + clearing, explicit sharing (M9 2.3 — **deferred v2**) | `GET /cookies/set`, `GET /cookies/echo` (mechanism only) | approved-gap (DEFERRED v2) | Owner-deferred in M9: application-scoped persistent profiles, restart/clear behavior, supported-storage documentation. Ephemeral per-control isolation (M9 2.2) is R35, not this row. | Owner acceptance at 9.6 sign-off confirms v2 deferral stands. Future: on-device restart/clear run (post-v1), documented storage mechanisms + errors. |
| R23 | `pushState` entry + Back/Forward traversal, phases 21–23 (M9 3.3, v66 evidence) | `GET /pushstate.html` | covered | Fixture script + resulting-URL observation host-verified; replacement traverses with native history APIs checking `SourceChanged`/`HistoryChanged`. History *state-object identity* is a separate gap (R24). | Host: `test_pushstate_fixture_page`. Device: 9.4 pushState scenario (pushed-URL assert + Back/Forward traversal). |
| R24 | History state-object identity (M9 3.3 residual gap, v66 note) | `GET /pushstate.html` (no readable surface) | approved-gap (DECISION — no v1 bridge) | M9 v66 evidence leaves this as the only history gap: no JS evaluation bridge (`ExecuteScriptAsync` deferred from v1) to read state back. Keep explicit rather than adding a fixture-only WebCore API. | Owner decision at 9.6 sign-off (defer to post-v1 script-result API). No verification possible in v1; must not gate 9.7 beyond the recorded statement. |
| R25 | `replaceState` mutates without pushing, phases 24–27 (M9 3.3, v68 evidence) | `GET /replacestate.html` | covered | Push-then-replace script host-verified; replacement asserts Back from `?v=2` lands on the no-query URL (intermediate `?v=1` absent) + Forward traversal — exercising engine history, not a JS-only expectation. | Host: `test_replacestate_fixture_page`. Device: 9.4 replaceState scenario. |
| R26 | Aggregate `suite-completed` / phase 28 `Done` (plan: failure if any check fails or times out) | None — 9.2 aggregation protocol | approved-gap (PENDING-9.2) | No run/scenario/assertion reporting or aggregation exists; 9.2 must deliver it (malformed/missing/timed-out handling, single-failed-assertion fails run, no credential/cookie values in logs) and 9.8 documents package/run identity. Old suite remains the record until then. | New: 9.2 host tests for aggregation + failure semantics. Device: 9.8 phone aggregate result with exact package/run identity; G5 closes only on that evidence. |
| R27 | `#next` DOM hit-test traces in ordinary load completion (plan: removal-only) | None — explicit non-requirement | covered | Decision recorded in the plan: remove from the framework load path; retain only in a dedicated interaction test if needed. Nothing to prove per navigation; 9.7 verifies absence from ordinary paths via source scan. | 9.7 production-source scan (no hit-test traces in load path) + public-runner regression (ordinary loads still complete). |
| R28 | Diagnostic-only counts (`history-back-count`, `branch-forward-count`, `history-*-document`) (M9 diagnostics) | None — public API observation | covered | Replaced by public `CanGoBack`/`CanGoForward`/`Source` observation in the 9.4 harness; not privileged assertions; no fixture route needed. | Device: 9.4 asserts availability flags against `Source` after each traversal (R9/R15 scenarios). |
| R29 | Scheduler resource oracle: CSS + delayed JS + fetch composition, distinct page identities (9.3 HTML main-response scope; `test_scheduler_resource_oracle_routes`) | `GET /scheduler.html?page=A|B`, `GET /scheduler.css`, `GET /scheduler.js`, `GET /api/data` | covered | MIME/closure/identity + timing composition host-verified (script checks computed style `101px`, timer 350 ms, JSON fetch → `3000+identity` vs `17px`). Device height oracle via screenshot/DOM-visible outcome (9.5); README-equivalent device case to be added by 9.8 docs. | Host: `test_scheduler_resource_oracle_routes`. Device: 9.5 two-identity runs (A→~3001px, B→~3002px oracle) with CSS/JS/fetch each independently breakable (9.3 intentional-failure variants). |
| R30 | CSS/image visual assertions with intentional-failure detection (9.3 scope; M9 4.2) | `GET /assets/style.css`, `GET /assets/image.png` (deterministic 2×2 RGBA PNG), `GET /assets/font.ttf` | covered | Bytes/MIME host-verified (`png()` equality); replacement must use visual assertions (screenshot) that 9.3 proves against intentional-failure variants rather than accepting static text/server output. | Host: `test_external_resource_response_types_and_font` (incl. `png()` equality). Device: 9.5 visual asserts (green heading pixel, 96×96 green image, external-font render) + 9.3 negative-variant proof. |
| R31 | Main-document resource budget, large `text/html` (plan correction #3; 9.3 scope; M9 4.5) | NO ROUTE — `/large` serves `application/octet-stream` only (verified absent) | approved-gap (PENDING-9.3) | Document-budget vs download-policy are distinct outcomes; the existing octet-stream route covers only the latter (R32). 9.3 must add the `text/html` budget route; configured budget failures must be explicit on device. | New: 9.3 route + host size/content-type test. Device: 9.4 over-budget main-document scenario asserting explicit categorized failure (not silent truncation). |
| R32 | Octet-stream budget cap + download/unsupported-content policy (M9 4.5; README device case) | `GET /large?bytes=N` (capped 4 MiB), `GET /download` (attachment) | covered | Bounds (`0…4 MiB`, bad-request on invalid) host-verified; replacement asserts explicit configured-budget failure above the old 512 KB shell cap and explicit download policy (not HTML navigation) on device. | Host: `test_cancelled_socket_and_bounded_large_response`. Device: 9.4 `/large?bytes=1048576` budget-failure assert + `/download` policy assert (explicit unsupported-content handling). |
| R33 | Concurrent delayed resources + input responsiveness (M9 4.1) | `GET /delay?ms=N` (0…5000 ms, 400 on invalid) | covered | Threaded-service host-verified (second client served within 0.4 s during a 500 ms delay); device asserts keyboard/focus/tap responsiveness during load (9.4 harness, M9 7.2 scope). | Host: `test_delayed_request_does_not_block_other_clients`. Device: 9.4 delayed-load input scenario (tap/focus works; no input stall). |
| R34 | gzip transfer decoding (M9 3.2 encoding variants) | `GET /gzip.html` (`Content-Encoding: gzip`) | covered | Compressed bytes + `gzip.decompress == INDEX` host-verified; device asserts normal fixture HTML after decoding. | Host: `test_redirect_error_encoding_compression_and_download` (gzip part). Device: 9.4 gzip scenario (heading/script markers live post-decode). |
| R35 | Redirect-loop bounded failure (M9 3.4 terminal outcomes) | `GET /redirect-loop` (self-302) | covered | Self-redirect host-verified; device asserts bounded terminal failure without indefinitely active loading. | Host: `test_redirect_error_encoding_compression_and_download` (loop part). Device: 9.4 loop scenario (terminal outcome + loading-state idle asserts). |
| R36 | Ephemeral per-control session + two-control isolation (M9 2.2; M9 2.4 TLS session note excluded) | `GET /cookies/set`, `GET /cookies/echo` | covered | Set/echo mechanics host-verified; replacement proves isolation by navigating two public-control instances independently (9.4 two-instance case, M10 5.5-origin requirement via 9.4). | Host: `test_cookie_request_echo_and_data`. Device: 9.4 two-instance scenario (instance A cookie invisible to B; one closes mid-load without affecting the other). |
| R37 | TLS valid-HTTPS success / invalid-cert fail-closed + mixed content (M9 2.4 v2.4-evidence; M9 4.3 mixed-content) | NO HTTP-SIMULATED ROUTE — device-accessible HTTPS required (README: "Neither is simulated by this HTTP server") | approved-gap (DEVICE, PENDING-11.2) | The HTTP fixture cannot bypass TLS checks by design; 11.2 must record the chosen HTTPS endpoints + CA setup (valid cert, deliberately untrusted cert, HTTP-subresource-under-HTTPS page) in test evidence. M9 fail-closed + consented-override (`device-tls-2.4.md`, `device-proceed-72.md`) behavior is re-proven, not assumed. | Device: 11.2 TLS matrix (valid loads; `expired`-style invalid fails terminally, no commit/retry; mixed-content blocked) with exact package/run identity + certificate-setup record. Host: none (out of scope by design). |
| R38 | Warning→proceed single-host session exception (M9 2.4 amended 2026-10-04: `history-proceed-exception`) | Device-only flow, no fixture route | approved-gap (DEVICE, PENDING-11.2) | M9 evidence verifies arming + scoped load, but cross-host isolation and Back-from-proceeded-page traversal are NOT log-confirmed and the unknown-issuer variant is untested — all three must be device-proven before the replacement claims this behavior. | Device: 11.2 proceed matrix (proceed loads only the armed host; sibling host still blocked; Back traversal; teardown clears exception; unknown-issuer variant). |
| R39 | Suspend/resume (and resize/DPI) while resources loading; cached/local-demo scrolling (M9 7.3) | `GET /slow.html`, `GET /delay?ms=3000` (loading state); shell contract in M10 8.8 | covered | Loading-state endpoints host-verified; shell suspend/resume contract specified (M10 8.8: `MainPage` Resuming reapplies margins, no `Close()` on suspend, owner power-button cycles). Device classification vs the accepted v33 reference belongs to 11.3. | Host: R20/R21 commands. Device: 11.3 suspend-mid-load + resume scenario (rendering continues, no stale mutation, margins correct) + resize/DPI + scroll cases, regressions classified vs v33 or owner-approved. |
| R40 | Single failed image as distinct categorized outcome (M9 4.5 diagnostics) | Unknown asset path → 404 (e.g. `GET /assets/no-such.png`); `GET /requests` | covered | Unknown-path 404 + request logging host-verified (`else → 404` branch); device asserts a failed image is a distinct image-outcome (not document/transport failure) with the document otherwise complete. | Host: 404 branch (covered by `test_redirect_error_encoding_compression_and_download` pattern). Device: 9.4 missing-image scenario (document PASS markers live, image-specific diagnostic recorded). |
| R41 | DNS failure as distinct categorized outcome (M9 4.5 diagnostics) | NO ROUTE — requires an unresolvable hostname on the device network | approved-gap (DEVICE, PENDING-11.2) | No fixture server route can simulate DNS failure; needs a reserved-unresolvable hostname (e.g. `.invalid`) accessed from the phone, asserting a categorized DNS outcome distinct from HTTP-error/budget/timeout. | Device: 11.2 DNS scenario with exact hostname + categorized-diagnostic assert. Defaults/timeout values documented in 9.8. |
| R42 | Unsupported inline MIME outcome (M9 3.2/3.5: non-HTML handling) | NO DEDICATED ROUTE (`/download` is attachment-disposition, `/large` is budget) | approved-gap (PENDING-9.3) | Inline unsupported-MIME handling (explicit policy, not HTML navigation, not download) has no fixture route; 9.3 decides whether to add one or specify the outcome against an existing route. | Owner decision at 9.6 sign-off → 9.3 route-or-policy spec. Device: 9.4 unsupported-MIME scenario after the decision. |
| R43 | App-supplied HTML: isolated opaque-origin/`about:blank` substitute-data, no base-URI option (M9 3.5) | None — public-API contract (`NavigateToString` path, M10 5.7) | covered | No fixture route needed: replacement asserts source/cancellation/history/origin-isolation through the public API (opaque local origin when no `<base>`, remote never falls through this API). Fixture-independent by design. | Device: 9.4 `NavigateToString` isolation scenario (origin opaque, cancel/history behavior, remote-navigation separation). |
| R44 | Navigation IDs, single terminal outcome, replacement ordering, engine generations (M9 3.4; plan: retain generic IDs at probe removal) | Exercised via R8/R20/R21 routes | covered | Every redirect hop / replacement / Stop asserts the same stable navigation ID with exactly one terminal outcome; generic navigation IDs/generations required for correct engine cancellation MUST survive 9.7 (only probe-only load-generation state is removed). | Device: 9.4 ID/ordering asserts across R8+R20+R21 scenarios (duplicate-terminal scan = fail). 9.7 scan must still find the generic ID/generation machinery. |
| R45 | Page/control teardown: no delayed completion updates a released instance (M9 4.4 residual: "teardown acceptance remains outstanding") | `GET /delay?ms=N` (pending-response mechanics) | approved-gap (DEVICE, PENDING-11.x) | Stop/replacement cancellation is device-proven in M9 (v63/v64), but page/control-teardown acceptance was explicitly left outstanding; the replacement harness (9.4 Close/reentrant cases) + 11.x lifecycle runs must prove it. | Device: 9.4 Close-during-load + reentrant scenarios; 11.x teardown run (deferred diagnostic callbacks expire with page lifetime; zero post-teardown mutations). |

Internal-consistency check (performed for this ledger):

- Every `FixtureSuitePhase` 0–28 maps: 0–1 → R1–R6 (+R7 settle, R8 redirect);
  2–5 → R9–R11; 6–8 → R14/R16/R17–R19; 9–11 → R20; 12–13 → R21; 14–17 →
  R10/R12/R14; 18–20 → R13 (+R12); 21–23 → R23 (+R24); 24–27 → R25 (+R24);
  28 → R26.
- Every `probeFixtureMarkers` field maps: heading/order/fetch/XHR/cookie →
  R1–R5; font trio → R6; redirect marker/target → R8; `café` → R16;
  CSP/CORS → R17–R19; push/replace `location` → R23/R25;
  `history-back-count`/`branch-forward-count`/`history-*-document` → R28;
  `#next` hit-test → R27; `fixture-cookie-fail` paired with `-pass` in R5.
- Every ledger row names at least one `GET` endpoint or states
  `NO ROUTE`/mechanism-only with an explicit gap (§1, R7/R22/R24/R26/R31/
  R37/R38/R41/R42/R45) — no row is endpoint-less without a gap rationale.

## 2. M9 checkbox → ledger-row map (completeness: all M9 1.1–7.6)

Scaffolding/contract checkboxes with no fixture phase map to their owning
M10 work, not to §1 rows; behavior checkboxes map to rows.

| M9 checkbox | Maps to |
|---|---|
| 1.1 loader/dependency inventory (`loader-map.md`) | Scaffolding — M10 `artifact/migration-map.md` + engine-module pinning; no fixture row. |
| 1.2 navigation IDs/terminal outcomes/origin rules (`control-contract.md`) | R44 (IDs/terminals) + R43 (origin rules); contract text → M10 specs. |
| 1.3 deterministic fixture (CSS/scripts/image/font/redirect/MIME/delay) | R1–R2, R8, R16, R29–R30, R33–R35. |
| 1.4 fetch/XHR/cookie/isolation/cancel/budget fixtures | R3–R5, R19–R21, R31–R33, R36, R40–R41. |
| 1.5 IDL/ABI review (script-result deferred) | Scaffolding — M10 §5/CX contract; R24 records the deferred script-result consequence. |
| 2.1 engine/thread/font init strategies | Scaffolding — M10 §4; font fixture serving → R6/R30. |
| 2.2 ephemeral per-control session + isolation | R5 (cookie mechanics) + R36 (two-instance isolation). |
| 2.3 persistent profiles/clearing (deferred v2) | R22 (approved-gap, deferred). |
| 2.4 TLS valid/invalid + proceed exception | R37 + R38 (approved-gaps, device). |
| 3.1 URL navigation/redirects/HTTP-error docs | R8–R9, R11, R14, R35. |
| 3.2 MIME/encoding/base/origin, unsupported outcomes | R14, R16, R34 (covered) + R42 (gap). |
| 3.3 links/same-document/Back/Forward/Reload, no shell history | R9–R10, R12–R13, R15, R23, R25 (+R24 gap). |
| 3.4 state/events, one terminal, Stop/replacement ordering | R8, R20–R21, R35, R44. |
| 3.5 app-supplied HTML opaque-origin contract | R43. |
| 4.1 scheduler backend, concurrency, input responsiveness | R29 (composition) + R33 (concurrency/input). |
| 4.2 external CSS/script/image/font w/ metadata | R1–R2, R6, R29–R30. |
| 4.3 script fetch/XHR same-session; CORS/CSP/mixed-content blocks | R3–R4, R17–R19 (gaps noted) + R37 (mixed content). |
| 4.4 lifetime-safe cancellation, Stop/replacement/teardown | R20–R21 (covered) + R45 (teardown gap). |
| 4.5 concurrency/buffering/redirect/timeout config + diagnostics | R31–R33, R35, R40–R41 (+R42 gap). |
| 4.6 eight-job build + phone regression log | Process — M10 §10/§11 builds + 9.8 evidence layout; no fixture row. |
| 5.1 IDL placement/metadata hygiene | Scaffolding — M10 §5; no fixture row. |
| 5.2 XAML-identity gate | Scaffolding — proven pre-integration; no fixture row. |
| 5.3 compiled-XAML/markup resolution | Scaffolding — M10 §8; no fixture row. |
| 5.4 Source/state properties, typed events, no recursive nav | Observed through R8–R10, R20–R21, R23, R25 device asserts; implementation → 9.4. |
| 5.5 two-instance dispose/unload/recreation, close-during-load | R36 (isolation) + R45 (teardown); implementation → 9.4. |
| 5.6 new-window/download/permission (deferred v2, fail-closed) | Deferred — `/download` policy row R32 asserts fail-closed handling only; contracts stay v2. |
| 5.7 lazy init/`NavigateToString`/idempotent Close | R43 (NavigateToString) + R45 (Close); implementation → 9.4/M10 5.7. |
| 6.1 declarative embed, toolbar/state from engine events | Scaffolding — M10 §8 (`csharp-app-hosts.md`); no fixture row. |
| 6.2 remove curl worker/polling/URL-vector history | Removal verified at 9.7 via R27-pattern source scan; behavior rows R9/R44 prove no regression. |
| 6.3 local demo/capture/scroll diagnostics, Skia staging guard | R39 scrolling part (11.3); staging guard → M10 §6/§10; no fixture row. |
| 7.1 matching package, exports/WinMD/closure/CA, version identity | Process — M10 §10/11.1 + 9.8 exact package/run identity; no fixture row. |
| 7.2 device harness incl. keyboard/focus/tap, Stop/redirect/history/security/cancel | Harness itself — 9.4/9.5 implementation; scenarios enumerated in §1 device column; aggregate → R26/9.8. |
| 7.3 resize/DPI + suspend/resume during load + scrolling vs v33 | R39 (+11.3 classification). |
| 7.4 Wikipedia Main_Page device load | Real-site acceptance — 11.4 owns; no fixture row (by design, fixtures never simulate it). |
| 7.5 GitHub repo device run | Same as 7.4 — 11.4 owns. |
| 7.6 capability/evidence reconciliation + owner acceptance | Milestone process — 9.8 + 11.5/11.6; this ledger is an input. |

## 3. Verification matrix

### 3a. Host commands (fixture-readiness; all must pass before any device claim)

```powershell
# Full host fixture contract (9/9 PASS on 2026-10-11; re-run after any 9.1–9.3 change):
python -m unittest discover -s Tools/w10m/NavigationTests -p test_server.py -v
# Per-area mapping:
#  R1/R2/R30  <- test_external_resource_response_types_and_font
#  R23        <- test_pushstate_fixture_page
#  R25        <- test_replacestate_fixture_page
#  R29        <- test_scheduler_resource_oracle_routes
#  R8/R11/R14/R34/R35 <- test_redirect_error_encoding_compression_and_download
#  R17/R18/R19        <- test_cors_and_policy_headers
#  R3/R4/R5/R36       <- test_cookie_request_echo_and_data
#  R20/R21/R33        <- test_delayed_request_does_not_block_other_clients
#  R20/R21/R31/R32    <- test_cancelled_socket_and_bounded_large_response
```

```powershell
# Fixture servers for device runs (phone uses this PC's LAN IPv4, not localhost):
python Tools/w10m/NavigationTests/server.py --host 0.0.0.0 --port 8000
python Tools/w10m/NavigationTests/server.py --host 0.0.0.0 --port 8001  # 2nd origin for R19
```

### 3b. Device evidence needed (per row group; 9.4 harness + 9.5 reports + 9.8 aggregate)

| Group | Rows | Device evidence (phone aggregate, exact package/run identity per 9.8) |
|---|---|---|
| CSS/image/scheduler visuals | R1, R29, R30 | 9.5 screenshot asserts (green heading pixel, 96×96 image, font render, scheduler heights A/B); 9.3 intentional-failure variants demonstrably detected. |
| JS-marker reports (order/fetch/XHR/cookie/CORS/fixture-location) | R2–R5, R19 (PENDING-9.2) | 9.2 `/report` + run-ID protocol first; then 9.5 per-marker asserts + `/requests` cross-check; logs carry no cookie/credential values. |
| CSP | R17 (PENDING-9.3), R18 | R18 now: marker + `/requests` non-arrival. R17 only after 9.3 oracle fix + negative-variant proof. |
| Navigation/history | R8–R12, R14–R15, R23, R25 | 9.4 typed URI/history/terminal sequences; no shell-URL-history; reload-no-spurious-entry; truncation; push/replace traversal. |
| Scroll/state identity | R13, R24 (DECISION) | Owner decision at 9.6 sign-off; device asserts only for the chosen option. |
| Cancel/replace/stop/teardown | R20–R21, R44–R45 | 9.4 Cancelled-outcome + stable-document (≥3500 ms) + ID-uniqueness asserts; 11.x teardown run for R45. |
| Budgets/policy/diagnostics | R31–R32, R34–R35, R40–R41 (+R42 after decision) | 9.4 over-budget/download/loop/DNS/missing-image scenarios with distinct categorized outcomes; explicit (never silent) failures. |
| Sessions/TLS/security | R22 (v2), R36–R38 | R36 in 9.4 two-instance run. R37/R38 in 11.2 TLS matrix with recorded HTTPS/CA setup (valid, untrusted, mixed-content, proceed arming/isolation/traversal/clearing). R22 stays deferred. |
| Lifecycle/resize | R39 | 11.3 suspend-mid-load/resume + resize/DPI + scroll runs classified vs v33 reference (or owner-approved). |
| Contracts | R43 (+R6 internal, R26 aggregate, R27–R28) | R43 in 9.4 `NavigateToString` run. R6 in internal font target. R26/R9.8 aggregate closes G5. R27–R28 via 9.4/9.7 scans + flag asserts. |

## 4. Ledger summary counts

- Total ledger rows: **45** (R1–R45; R10 covers both the phase-4 and
  phase-14 reload markers in one row).
- `covered`: **27** — R1, R8, R9, R10, R11, R12, R14, R15, R16, R18, R20, R21,
  R23, R25, R27, R28, R29, R30, R32, R33, R34, R35, R36, R39, R40, R43, R44.
- `internal-only`: **1** — R6 (renderer-private font backend assertions).
- `approved-gap`: **17** — R2, R3, R4, R5, R7, R13, R17, R19, R22, R24, R26,
  R31, R37, R38, R41, R42, R45. (27 + 1 + 17 = 45 ✓.)

Gap breakdown (for 9.7 gating): PENDING-9.2 → R2, R3, R4, R5, R7, R19, R26
(7 rows: reporting/aggregation infrastructure); PENDING-9.3 → R17, R31, R42
(3 rows: oracle fix + budget/MIME routes); DECISION at 9.6 sign-off → R13,
R24 (2 rows); DEFERRED-v2 → R22 (+5.6 contracts, §2); DEVICE 11.x → R37, R38,
R41, R45 (4 rows: TLS/proceed/DNS/teardown). No gap is unowned.

Required-scope checklist: CSP → R17/R18 ✓; budgets → R31/R32 ✓; CSS/image →
R1/R29/R30 ✓; scroll/history/fetch/XHR/cookie/security → R2–R5, R9–R15, R19,
R23–R25, R36–R38 ✓; suspend/resume (present in M9 7.3) → R39 ✓.

## 5. What 9.7 removal needs from this ledger

9.7 ("Remove fixture fields/DOM probes/phase timers/uwp-suite=1 trigger and
ordinary shell test sequences only after 9.6") is gated as follows:

1. **All 17 approved-gap rows resolved or owner-approved.** Concretely:
   9.2 lands `/report` + run-ID isolation + aggregation (clears R2, R3, R4,
   R5, R7, R19, R26); 9.3 lands the CSP oracle fix, the large-`text/html`
   route, and the R42 route-or-policy decision (clears R17, R31, R42);
   owner records the R13 scroll-surface and R24 state-identity decisions;
   R22 stays deferred-v2 by explicit re-confirmation; R37/R38/R41/R45 move
   to scheduled 11.x runs (removal may proceed while device runs are pending
   ONLY with owner sign-off naming the run).
2. **Every `covered` row device-proven on matching fixtures.** For each of the
   27 covered rows, a 9.4-harness + 9.5-report run against the same
   package/server showing the §1 verification assert passing, collected into
   the 9.8 aggregate with exact package/run identity. Covered ≠ proven until
   then; the host 9/9 suite proves fixture readiness only.
3. **Old/new suite comparison on matching fixtures.** Run the M9-derived
   expectations (this ledger's markers) and the replacement asserts against
   the same server revision; investigate every discrepancy before removal
   (plan §Safe-removal-order step 3). This ledger's endpoint column pins the
   "matching fixtures" revision: `server.py` + `test_server.py` as
   host-verified in §3a.
4. **Removal checklist executed verbatim** (plan §Safe-removal-order step 5):
   remove `fixtureBodyText`, `probeFixtureMarkers`, settle timers,
   `FixtureSuitePhase`/`scheduleFixtureSuite`, suite fields, `uwp-suite=1`
   startup/log gating, fixture-specific completion/hit-test logs — and
   NOTHING else. 9.7 must verify via production-source scans that ordinary
   navigation, history, rendering, failure behavior, generic navigation
   IDs/generations (R44), and the R27-removed-path absence remain intact,
   and that the public runner still reproduces the required outcomes.
5. **Retain-list confirmation.** The following must still exist after 9.7:
   generic navigation IDs/generations for engine cancellation (R7/R44),
   `/requests` logging without sensitive values (R18/R19/R40), the R6
   internal font target, and the R22-v2 / R24-deferral statements.
