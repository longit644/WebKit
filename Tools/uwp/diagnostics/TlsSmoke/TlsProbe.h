// TlsSmoke native transport probe payload contract.
//
// The C# application host (App.xaml.cs) in this directory owns activation,
// window, LocalState/package path resolution and the display keep-awake, and
// reaches the probe only through the three WK* exports below. The probe
// phases, marker names, ordering, constants and the "%s: 0x%08lx\r\n" report
// shape are behavior-identical to the former native host; only file/location
// plumbing moved across the language boundary.
//
// Report protocol: every line is "<stage>: 0x<8 lowercase hex digits>\r\n",
// appended under an exclusive SRW lock and flushed per line, so a truncated
// report names the faulting call by its missing exit marker. Phase order is
// crypto checks (tls-bcrypt through tls-rand), curl global init, plain-HTTP
// phase A, unverified-HTTPS phase B, verified-HTTPS phase C, then tls-done.
// Host-owned lines precede the phases: tls-startup, then tls-ca-present (or
// tls-package-path when the package path itself is unreachable).
//
// Fail-closed rule: the verified phase always sets peer verification,
// strict host verification and the packaged bundle path; a missing bundle
// fails the curl call with zero bytes and no retry. The insecure diagnostic
// phase uses a separate handle and separate outcome variables and never feeds
// the verified verdict, so warning/consent policy stays independent of the
// transport isolation diagnosis.
#ifndef WEBKIT_TLS_PROBE_H
#define WEBKIT_TLS_PROBE_H

// Truncates/creates the LocalState report file at the host-resolved UTF-16 path.
extern "C" __declspec(dllexport) void __stdcall WKSetTlsSmokeLogPath(const wchar_t* logPath);

// Appends one report line; shared by probe phases and host-owned markers.
extern "C" __declspec(dllexport) void __stdcall WKLogTlsSmokeResult(const char* stage, unsigned long result);

// Logs tls-ca-present for the host-joined UTF-8 bundle path, then runs the
// crypto/curl phase sequence ending in tls-done.
extern "C" __declspec(dllexport) void __stdcall WKRunTlsSmokeProbes(const char* caBundleUtf8);

#endif // WEBKIT_TLS_PROBE_H
