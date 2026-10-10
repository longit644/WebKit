// Host-only proof glue for the TlsSmoke payload: never packaged, never shipped.
//
// Builds against TlsProbe.cpp on the build machine (MinGW x64 in
// verify-tls-smoke-host.ps1) and replays the exact host-owned line sequence
// the C# host emits on the phone (tls-startup, then the payload entry, which
// logs tls-ca-present before the phases), so marker names, phase ordering,
// report shape and fail-closed outcomes can be compared against the inventoried
// expectations without a device. Network behavior is the real payload linked
// against real libcurl/OpenSSL; only the compiler/host differs.
#include <stdio.h>
#include <string>
#include <windows.h>

// Payload entry points under test; linked from TlsProbe.cpp in this build.
// Set the LocalState log path consumed by the report writer.
extern "C" void __stdcall WKSetTlsSmokeLogPath(const wchar_t* logPath);
// Emit one report field using the native stage/value serialization.
extern "C" void __stdcall WKLogTlsSmokeResult(const char* stage, unsigned long result);
// Run the unchanged native TLS phases with the supplied optional CA bundle.
extern "C" void __stdcall WKRunTlsSmokeProbes(const char* caBundleUtf8);

// Host-proof entry: mirrors the C# OnLaunched order (log path, tls-startup,
// bundle path, phases) with caller-supplied paths.
int main(int argc, char** argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: TlsHostProof <log-path> <ca-bundle-path-or-empty>\n");
        return 2;
    }
    wchar_t logPath[1024];
    int converted = MultiByteToWideChar(CP_UTF8, 0, argv[1], -1, logPath, 1024);
    if (converted <= 0) {
        fprintf(stderr, "log path conversion failed\n");
        return 2;
    }
    WKSetTlsSmokeLogPath(logPath);
    WKLogTlsSmokeResult("tls-startup", 0);
    std::string caBundle(argv[2]);
    if (caBundle == "-")
        caBundle.clear();
    WKRunTlsSmokeProbes(caBundle.c_str());
    return 0;
}
