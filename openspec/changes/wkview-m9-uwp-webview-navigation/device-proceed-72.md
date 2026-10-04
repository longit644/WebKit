# MiniBrowser 1.0.0.72 consented proceed acceptance run

Device: Windows 10 Mobile ARM32. Package
`WebKitWebView.MiniBrowser_1.0.0.72_arm__2932e6rj4m41p`
(install verified in the package list; app terminated before launch for a
fresh log). LocalState/minibrowser.log.

## Flow verified (owner-operated, log-confirmed)

```
navigation-policy: https://expired.badssl.com/: 0x00000001
uwp-load-start: https://expired.badssl.com/: 0x0000001b
uwp-load-error: https://expired.badssl.com/: 0x0000003c
navigation-provisional-error-url: https://expired.badssl.com/: 0x0000003c
minibrowser-interstitial-shown: 0x0000003c
navigation-policy: minibrowser:proceed: 0x00000000
history-proceed-exception: expired.badssl.com: 0x00000001
navigation-policy: https://expired.badssl.com/: 0x00000001
uwp-load-start: https://expired.badssl.com/: 0x0000001b
uwp-load-response: https://expired.badssl.com/: 0x000000c8
uwp-load-mime: https://expired.badssl.com/: 0x00000009
uwp-load-data: https://expired.badssl.com/: 0x000001ee
uwp-load-start: https://expired.badssl.com/style.css: 0x00000024
uwp-load-complete: https://expired.badssl.com/: 0x0000001ee
uwp-load-response: https://expired.badssl.com/style.css: 0x00000008
uwp-load-data: https://expired.badssl.com/style.css: 0x000005e2
uwp-load-complete: https://expired.badssl.com/style.css: 0x000005e2
```

| Step | Evidence | Result |
| --- | --- | --- |
| Fail closed by default | cert error 0x3c, interstitial shown, owner confirms warning page | Pass |
| Proceed tap reaches policy, link itself never loads | `navigation-policy: minibrowser:proceed: 0` (Ignore) | Pass |
| Host exception armed for that host only | `history-proceed-exception: expired.badssl.com` | Pass |
| Proceeded page loads (main document HTTP 200) | response/data/complete for `/` | Pass |
| Proceeded page subresources load (same host) | style.css 200 + complete | Pass — per-request enforcement covers subresources |
| No global flag mutated | `setIgnoreSSLErrors` stays false; enforcement is per-request via the exception store | By construction + code review |

Owner confirms the warning page, the Advanced expander, and the proceeded red
site page all render; "Ok good" on the full flow.

## Design as amended in 2.4

- Fail-closed default; no silent or automatic bypass anywhere in the stack.
- Bypass requires the explicit tap on the warning page's proceed control.
- Scope: that host, current page session only; cleared on page teardown;
  only the recorded failing URL can arm it, and only while no successful
  commit has superseded it.
- The interstitial page itself is application-layer HTML (MiniBrowser default);
  any host app may supply its own design using the same failure data
  (failing URL + error code) and `loadHTML`. No engine change is needed for a
  custom page. A formal navigation-failed event belongs to the WinRT component
  work (M9 5.4).

## Limits

- Cross-host isolation (`self-signed.badssl.com` still fails after proceeding
  on another host) not yet exercised on device.
- Back-from-proceeded-page traversal not log-confirmed in this run.
- Deterministic offline TLS fixture (local expired-cert server) remains a
  follow-up; badssl.com verification above is manual.
