// Native TLS transport probe payload for the TlsSmoke diagnostic.
// Packaged libcurl + OpenSSL only. No XAML, no WebCore, no worker threads.
// Phases isolate plain-TCP, handshake-core and cert-store behavior with
// synchronous calls.
//
// Application hosting (Application/activation/window/launch, LocalState and
// package-path resolution, display keep-awake) lives in the C# host
// (App.xaml.cs) in this directory and reaches this payload only through the
// three WK* exports below. Every probe constant, enter/exit marker name,
// phase ordering and report line is behavior-identical to the former native
// host: the C# migration moved file/location plumbing, never probe logic.
#include <winsock2.h>
#include <windows.h>
#include <bcrypt.h>
#include <curl/curl.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/provider.h>
#include <openssl/rand.h>
#include <stdio.h>
#include <string>

// Boolean values and status codes written by TLS probe log fields.
enum TlsProbeLogBoolean {
    // Log field describes a false condition.
    TlsProbeLogFalse = 0,

    // Log field describes a true condition.
    TlsProbeLogTrue = 1
};

// Native-call status values used by the TLS log protocol.
enum TlsProbeStatus {
    // Native call completed without an error.
    TlsProbeStatusSuccess = 0,

    // Native call or probe setup failed.
    TlsProbeStatusFailure = 1
};

// Maximum UTF-16 characters accepted for the LocalState log path.
constexpr size_t kTlsLogPathCapacity = 1024;
// Maximum formatted bytes in one serialized report line.
constexpr size_t kTlsLogLineCapacity = 160;
// Maximum response bytes retained by the probe's curl callback.
constexpr size_t kTlsResponseBodyCapBytes = 256 * 1024;
// Maximum redirects followed in either HTTP transport phase.
constexpr long kProbeMaxRedirects = 3L;
// Connection timeout for a probe request, in seconds.
constexpr long kProbeConnectTimeoutSeconds = 8L;
// Whole-request deadline for a probe request, in seconds.
constexpr long kProbeTotalTimeoutSeconds = 15L;
// Enable libcurl redirect following.
constexpr long kCurlFollowRedirects = 1L;
// Enable certificate-chain peer verification.
constexpr long kCurlVerifyPeer = 1L;
// Enable strict certificate-hostname verification.
constexpr long kCurlVerifyHost = 2L;
// Explicit zero value used only by the deliberately unverified comparison phase.
constexpr long kCurlDisableVerification = 0L;
// Sentinel curl result for a phase that has not run.
constexpr int kUninitializedCurlCode = -1;
// Distinct synthetic result when curl handle allocation fails.
constexpr int kCurlHandleCreationFailed = -100;
// No optional OpenSSL initialization flags are enabled.
constexpr int kNoOpenSSLInitFlags = 0;
// No optional UTF conversion flags are enabled by this native probe.
constexpr int kWindowsConversionNoFlags = 0;
// One terminating UTF-16 NUL is included in path conversion buffers.
constexpr size_t kNulTerminatorCharacters = 1;
// Empty callback result has no output bytes.
constexpr size_t kEmptyOutputLength = 0;
// Returning zero from libcurl's write callback aborts an oversized response.
constexpr size_t kAbortCurlWriteCallback = 0;
// Size of the BCrypt random-data probe buffer.
constexpr size_t kBcryptProbeBufferBytes = 32;
// Size of the OpenSSL secure-heap allocation probe.
constexpr size_t kOpenSSLHeapProbeBytes = 64;
// Size of the OpenSSL random-data probe buffer.
constexpr size_t kOpenSSLRandomProbeBytes = 16;
// User-Agent sent by each diagnostic curl phase.
constexpr char kTlsProbeUserAgent[] = "WebKitTlsSmoke/1.0";
// OpenSSL provider selected for the provider-load probe.
constexpr char kOpenSSLDefaultProviderName[] = "default";
// Cipher selected for the OpenSSL cipher-fetch probe.
constexpr char kAesProbeCipherName[] = "AES-256-CBC";
// Plain HTTP endpoint isolates TCP/curl behavior from TLS policy.
constexpr char kPlainHttpProbeURL[] = "http://neverssl.com/";
// HTTPS endpoint exercises verification and comparison policy phases.
constexpr char kHttpsProbeURL[] = "https://example.com/";

static HANDLE logHandle = INVALID_HANDLE_VALUE;
static wchar_t logFilename[kTlsLogPathCapacity];
static SRWLOCK logLock = SRWLOCK_INIT;

// Appends one serialized diagnostic line to the LocalState log.
static void __stdcall logResult(const char* stage, unsigned long result)
{
    char text[kTlsLogLineCapacity];
    int length = sprintf_s(text, "%s: 0x%08lx\r\n", stage, result);
    OutputDebugStringA(text);
    AcquireSRWLockExclusive(&logLock);
    if (logHandle == INVALID_HANDLE_VALUE && *logFilename)
        logHandle = CreateFile2(logFilename, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_ALWAYS, nullptr);
    if (logHandle != INVALID_HANDLE_VALUE && length > 0) {
        DWORD written;
        WriteFile(logHandle, text, static_cast<DWORD>(length), &written, nullptr);
        FlushFileBuffers(logHandle);
    }
    if (logHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(logHandle);
        logHandle = INVALID_HANDLE_VALUE;
    }
    ReleaseSRWLockExclusive(&logLock);
}

static std::string responseBody;

// Appends a curl data chunk; returns short to abort past the probe cap.
static size_t writeCallback(char* ptr, size_t size, size_t nmemb, void*)
{
    size_t count = size * nmemb;
    // Probe downloads are diagnostic only; never buffer a full page here.
    if (responseBody.size() + count > kTlsResponseBodyCapBytes)
        return kAbortCurlWriteCallback; // abort: over probe cap
    responseBody.append(ptr, count);
    return count;
}

// One HTTPS probe run: curl result, HTTP status and body size.
struct PhaseOutcome {
    // Raw curl result; -1 means the phase never ran.
    int curlCode { kUninitializedCurlCode };

    // HTTP status when the transport completed.
    long httpCode { 0 };

    // Body bytes received.
    unsigned bytes { 0 };
};

// Runs one fetch with the given verification mode; never throws.
static PhaseOutcome runPhase(const char* url, bool verify, const char* caPath)
{
    PhaseOutcome outcome;
    logResult("tls-easy-init-enter", TlsProbeStatusSuccess);
    CURL* handle = curl_easy_init();
    logResult("tls-easy-init-exit", handle ? TlsProbeStatusSuccess : TlsProbeStatusFailure);
    if (!handle) {
        // Distinct from any CURLcode: the handle itself failed.
        outcome.curlCode = kCurlHandleCreationFailed;
        return outcome;
    }
    responseBody.clear();
    curl_easy_setopt(handle, CURLOPT_URL, url);
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, kCurlFollowRedirects);
    // Probe redirects stay few and fast; the shell suite covers real chains.
    curl_easy_setopt(handle, CURLOPT_MAXREDIRS, kProbeMaxRedirects);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, kProbeConnectTimeoutSeconds);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, kProbeTotalTimeoutSeconds);
    curl_easy_setopt(handle, CURLOPT_USERAGENT, kTlsProbeUserAgent);
    curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");
    if (verify) {
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, kCurlVerifyPeer);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, kCurlVerifyHost);
        curl_easy_setopt(handle, CURLOPT_CAINFO, caPath);
    } else {
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, kCurlDisableVerification);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, kCurlDisableVerification);
    }
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, writeCallback);
    logResult("tls-perform-enter", TlsProbeStatusSuccess);
    CURLcode code = curl_easy_perform(handle);
    logResult("tls-perform-exit", static_cast<unsigned>(code));
    outcome.curlCode = static_cast<int>(code);
    if (code == CURLE_OK)
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &outcome.httpCode);
    outcome.bytes = static_cast<unsigned>(responseBody.size());
    curl_easy_cleanup(handle);
    return outcome;
}

// Runs certificate-verifying and diagnostic insecure curl phases after crypto checks.
static void runTlsProbes(const char* caPath)
{
    // Entry/exit markers around every native call: a crash between a -enter
    // and its matching marker (with no matching marker) names the faulting
    // call exactly, without needing dump archaeology.
    unsigned char bcryptBytes[kBcryptProbeBufferBytes];
    logResult("tls-bcrypt-enter", TlsProbeStatusSuccess);
    NTSTATUS bcryptStatus = BCryptGenRandom(nullptr, bcryptBytes, sizeof(bcryptBytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    logResult("tls-bcrypt-exit", static_cast<unsigned>(bcryptStatus));
    logResult("tls-openssl-init-enter", TlsProbeStatusSuccess);
    int minInit = OPENSSL_init_crypto(kNoOpenSSLInitFlags, nullptr);
    logResult("tls-openssl-init-exit", static_cast<unsigned>(minInit));
    logResult("tls-libctx-new-enter", TlsProbeStatusSuccess);
    OSSL_LIB_CTX* freshCtx = OSSL_LIB_CTX_new();
    logResult("tls-libctx-new-exit", freshCtx ? TlsProbeLogTrue : TlsProbeLogFalse);
    if (freshCtx)
        OSSL_LIB_CTX_free(freshCtx);
    logResult("tls-provider-enter", TlsProbeStatusSuccess);
    OSSL_PROVIDER* defaultProvider = OSSL_PROVIDER_load(nullptr, kOpenSSLDefaultProviderName);
    logResult("tls-provider-exit", defaultProvider ? TlsProbeLogTrue : TlsProbeLogFalse);
    logResult("tls-cipher-fetch-enter", TlsProbeStatusSuccess);
    EVP_CIPHER* fetchedCipher = EVP_CIPHER_fetch(nullptr, kAesProbeCipherName, nullptr);
    logResult("tls-cipher-fetch-exit", fetchedCipher ? TlsProbeLogTrue : TlsProbeLogFalse);
    if (fetchedCipher)
        EVP_CIPHER_free(fetchedCipher);
    logResult("tls-init-ciphers-enter", TlsProbeStatusSuccess);
    int ciphersInit = OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_CIPHERS, nullptr);
    logResult("tls-init-ciphers-exit", static_cast<unsigned>(ciphersInit));
    logResult("tls-init-digests-enter", TlsProbeStatusSuccess);
    int digestsInit = OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_DIGESTS, nullptr);
    logResult("tls-init-digests-exit", static_cast<unsigned>(digestsInit));
    logResult("tls-init-strings-enter", TlsProbeStatusSuccess);
    int stringsInit = OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr);
    logResult("tls-init-strings-exit", static_cast<unsigned>(stringsInit));
    logResult("tls-err-enter", TlsProbeStatusSuccess);
    ERR_clear_error();
    unsigned long errCode = ERR_get_error();
    logResult("tls-err-exit", errCode);
    logResult("tls-osslmalloc-enter", TlsProbeStatusSuccess);
    void* heapProbe = OPENSSL_malloc(kOpenSSLHeapProbeBytes);
    logResult("tls-osslmalloc-exit", heapProbe ? TlsProbeLogTrue : TlsProbeLogFalse);
    if (heapProbe)
        OPENSSL_free(heapProbe);
    // Split the RAND path: poll (OS entropy gathering) vs generate (DRBG
    // logic). Whichever enter lacks its exit is the faulting half.
    logResult("tls-poll-enter", TlsProbeStatusSuccess);
    int pollStatus = RAND_poll();
    logResult("tls-poll-exit", static_cast<unsigned>(pollStatus));
    unsigned char randomBytes[kOpenSSLRandomProbeBytes];
    logResult("tls-rand-enter", TlsProbeStatusSuccess);
    int randStatus = RAND_bytes(randomBytes, sizeof(randomBytes));
    logResult("tls-rand-exit", static_cast<unsigned>(randStatus));
    logResult("tls-global-init-enter", TlsProbeStatusSuccess);
    CURLcode globalCode = curl_global_init(CURL_GLOBAL_DEFAULT);
    logResult("tls-global-init-exit", static_cast<unsigned>(globalCode));
    // Phase A: plain HTTP, no crypto. Failure here means sockets/threads/CRT,
    // not TLS.
    PhaseOutcome plain = runPhase(kPlainHttpProbeURL, false, nullptr);
    logResult("tls-plain-curl-code", static_cast<unsigned>(plain.curlCode));
    logResult("tls-plain-http-code", static_cast<unsigned>(plain.httpCode));
    logResult("tls-plain-bytes", plain.bytes);
    // Phase B: HTTPS without verification. Failure here with A passing means
    // handshake core (RAND/locks/codegen), not the cert store.
    PhaseOutcome insecure = runPhase(kHttpsProbeURL, false, nullptr);
    logResult("tls-insecure-curl-code", static_cast<unsigned>(insecure.curlCode));
    logResult("tls-insecure-http-code", static_cast<unsigned>(insecure.httpCode));
    logResult("tls-insecure-bytes", insecure.bytes);
    // Phase C: HTTPS with verification against the packaged Mozilla bundle.
    // Failure here with B passing means the X509_STORE/cacert path.
    PhaseOutcome verified = runPhase(kHttpsProbeURL, true, caPath);
    logResult("tls-verified-curl-code", static_cast<unsigned>(verified.curlCode));
    logResult("tls-verified-http-code", static_cast<unsigned>(verified.httpCode));
    logResult("tls-verified-bytes", verified.bytes);
    logResult("tls-done", TlsProbeStatusSuccess);
}

// Truncates/creates the LocalState report file at the host-resolved UTF-16
// path, mirroring the CREATE_ALWAYS setup the former WKTlsStartup entry ran
// after resolving LocalState through WinRT. The C# host resolves the path;
// this payload only stores and truncates it.
extern "C" __declspec(dllexport) void __stdcall WKSetTlsSmokeLogPath(const wchar_t* logPath)
{
    if (!logPath || !*logPath)
        return;
    wcscpy_s(logFilename, logPath);
    HANDLE file = CreateFile2(logFilename, GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, nullptr);
    if (file != INVALID_HANDLE_VALUE)
        CloseHandle(file);
}

// Single report writer shared by the probe phases and the C# host. The
// host-owned lines (tls-startup, tls-ca-present when the host cannot even
// reach the package path, tls-package-path) travel through this export so
// every report line keeps the identical "%s: 0x%08lx\r\n" shape, locking and
// flush behavior no matter which side emits it.
extern "C" __declspec(dllexport) void __stdcall WKLogTlsSmokeResult(const char* stage, unsigned long result)
{
    logResult(stage, result);
}

// Checks the host-joined UTF-8 CA bundle path, logs tls-ca-present exactly
// as the former view Run did (present flag, never a hard failure), then runs
// the unchanged phase sequence. A null, empty or missing bundle flows into
// the verified phase as an empty CAINFO, which curl fails closed: nonzero
// curl code, zero bytes, no retry and no fallback to the insecure diagnostic
// result, which stays a separate attribution variable throughout.
extern "C" __declspec(dllexport) void __stdcall WKRunTlsSmokeProbes(const char* caBundleUtf8)
{
    if (!caBundleUtf8)
        caBundleUtf8 = "";
    int wideLength = MultiByteToWideChar(CP_UTF8, kWindowsConversionNoFlags, caBundleUtf8, -1, nullptr, kEmptyOutputLength);
    std::wstring caPathW;
    if (wideLength > static_cast<int>(kNulTerminatorCharacters)) {
        caPathW.assign(wideLength - kNulTerminatorCharacters, L'\0');
        MultiByteToWideChar(CP_UTF8, kWindowsConversionNoFlags, caBundleUtf8, -1, caPathW.data(), wideLength);
    }
    if (!caPathW.empty() && GetFileAttributesW(caPathW.c_str()) != INVALID_FILE_ATTRIBUTES) {
        logResult("tls-ca-present", TlsProbeLogTrue);
    } else {
        logResult("tls-ca-present", TlsProbeLogFalse);
    }
    runTlsProbes(caBundleUtf8);
}
