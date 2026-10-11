# M9 2.4 TLS acceptance: MiniBrowser 1.0.0.68 on-device evidence

Device: Windows 10 Mobile ARM32. Package
`WebKitWebView.MiniBrowser_1.0.0.68_arm__2932e6rj4m41p`.
LocalState/minibrowser.log (append-only; suite lines omitted here).

## Valid certificate succeeds (owner-observed)

`https://example.com/` loads on the phone. Verification path in source:
`WKNavigatePageUWP` sets `setIgnoreSSLErrors(false)` and the packaged CA path;
`CurlHandle::enableSSL` enables peer verification with strict hostname checking
(`CurlContext.cpp:328`). On Windows the build additionally passes
`CURLSSLOPT_NATIVE_CA`, so trust anchors in the phone's system root store.
No fallback or insecure retry exists in the engine navigation path; the legacy
fetch worker's `kFetchInsecureDiagnostic` is `false` in source (peer + host
verification on). The 1.0.0.29 no-verify build was a reverted one-off diagnostic.

## Invalid certificate fails cleanly (log-verified)

Owner entered `https://expired.badssl.com/` in the Go box (plus a typo'd
`https://expiered.badssl.com/`, which fails DNS/TLS the same way). Log:

```
navigation-policy: https://expired.badssl.com/: 0x00000001
uwp-load-start: https://expired.badssl.com/: 0x0000001b
uwp-load-error: https://expired.badssl.com/: 0x0000003c
navigation-provisional-error: 0x0000003c
navigation-provisional-error-url: https://expired.badssl.com/: 0x0000003c
```

- `0x3c` = 60 = `CURLE_PEER_FAILED_VERIFICATION` (expired certificate).
- Failure is terminal at the provisional stage; no `navigation-completed`,
  no page commit, no retry, no bypass. Owner confirms nothing loaded.
- Repeated Go presses while a previous attempt was in flight produce
  `navigation-provisional-error: 0x00000001` — our own `cancelledError`
  ("Load cancelled") for the abandoned provisional load. Also correct.

## Limits

- Unknown-issuer rejection (`self-signed.badssl.com`) not yet exercised.
- The separate OpenSSL `TlsSmoke` crash (`OSSL_LIB_CTX_new` lhash) remains
  open but is outside the verification path (the 1.0.0.29 experiment faulted
  identically with verification off).
