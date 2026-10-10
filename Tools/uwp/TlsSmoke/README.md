# TlsSmoke migration pointer

The native TLS transport payload and C# application host now live under
`Tools/uwp/diagnostics/TlsSmoke/`. Build with
`diagnostics/TlsSmoke/build-arm32-tls-smoke.ps1`; verify retained fail-closed
semantics with `diagnostics/TlsSmoke/verify-tls-smoke-host.ps1`.

This legacy directory no longer contains a standalone host project. The
diagnostics README records the current payload, report protocol, package
identity, and device runbook.
