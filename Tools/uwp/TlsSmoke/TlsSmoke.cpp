// Minimal native TLS probe: packaged libcurl + OpenSSL only.
// No XAML, no WebCore, no worker threads. Phases isolate plain-TCP,
// handshake-core and cert-store behavior with synchronous calls.
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
#include <windows.applicationmodel.core.h>
#include <windows.storage.h>
#include <windows.system.display.h>
#include <windows.ui.core.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>

using namespace Microsoft::WRL;
using Microsoft::WRL::Wrappers::HString;
using Microsoft::WRL::Wrappers::HStringReference;
namespace Core = ABI::Windows::ApplicationModel::Core;
namespace UICore = ABI::Windows::UI::Core;
namespace Storage = ABI::Windows::Storage;

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

constexpr size_t kTlsLogPathCapacity = 1024;
constexpr size_t kTlsLogLineCapacity = 160;
constexpr size_t kTlsResponseBodyCapBytes = 256 * 1024;
constexpr long kProbeMaxRedirects = 3L;
constexpr long kProbeConnectTimeoutSeconds = 8L;
constexpr long kProbeTotalTimeoutSeconds = 15L;
constexpr long kCurlFollowRedirects = 1L;
constexpr long kCurlVerifyPeer = 1L;
constexpr long kCurlVerifyHost = 2L;
constexpr long kCurlDisableVerification = 0L;
constexpr int kUninitializedCurlCode = -1;
constexpr int kCurlHandleCreationFailed = -100;
constexpr int kNoOpenSSLInitFlags = 0;
constexpr int kWindowsConversionNoFlags = 0;
constexpr size_t kNulTerminatorCharacters = 1;
constexpr size_t kEmptyOutputLength = 0;
constexpr size_t kAbortCurlWriteCallback = 0;
constexpr size_t kBcryptProbeBufferBytes = 32;
constexpr size_t kOpenSSLHeapProbeBytes = 64;
constexpr size_t kOpenSSLRandomProbeBytes = 16;
constexpr char kTlsProbeUserAgent[] = "WebKitTlsSmoke/1.0";
constexpr char kOpenSSLDefaultProviderName[] = "default";
constexpr char kAesProbeCipherName[] = "AES-256-CBC";
constexpr char kPlainHttpProbeURL[] = "http://neverssl.com/";
constexpr char kHttpsProbeURL[] = "https://example.com/";
constexpr wchar_t kCaBundleFilename[] = L"\\cacert.pem";

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

// Minimal XAML view hosting the TLS smoke run.
class TlsView final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, Core::IFrameworkView> {
    InspectableClass(L"WebKitTlsSmoke.View", BaseTrust);

public:
    // Subscribes to activation so the CoreWindow exists before the TLS probe.
    HRESULT STDMETHODCALLTYPE Initialize(Core::ICoreApplicationView* view) override
    {
        auto activated = Callback<ABI::Windows::Foundation::ITypedEventHandler<Core::CoreApplicationView*, ABI::Windows::ApplicationModel::Activation::IActivatedEventArgs*>>(
            [this](Core::ICoreApplicationView*, ABI::Windows::ApplicationModel::Activation::IActivatedEventArgs*) -> HRESULT {
                return m_window->Activate();
            });
        EventRegistrationToken token;
        return view->add_Activated(activated.Get(), &token);
    }

    // Stores the CoreWindow used for activation and event pumping.
    HRESULT STDMETHODCALLTYPE SetWindow(UICore::ICoreWindow* window) override
    {
        m_window = window;
        return S_OK;
    }

    // No packaged XAML page needs to load before Run.
    HRESULT STDMETHODCALLTYPE Load(HSTRING) override { return S_OK; }

    // Holds the display awake, locates cacert.pem, runs TLS phases and pumps events.
    HRESULT STDMETHODCALLTYPE Run() override
    {
        HRESULT result = m_window->Activate();
        if (FAILED(result))
            return result;
        ComPtr<UICore::ICoreDispatcher> dispatcher;
        result = m_window->get_Dispatcher(&dispatcher);
        if (FAILED(result))
            return result;
        ComPtr<IInspectable> display;
        if (SUCCEEDED(RoActivateInstance(HStringReference(RuntimeClass_Windows_System_Display_DisplayRequest).Get(), &display))
            && SUCCEEDED(display.As(&m_displayRequest)))
            m_displayActive = SUCCEEDED(m_displayRequest->RequestActive());
        // Resolve the package-installed CA bundle used by the verified phase.
        ComPtr<ABI::Windows::ApplicationModel::IPackageStatics> packages;
        ComPtr<ABI::Windows::ApplicationModel::IPackage> package;
        ComPtr<Storage::IStorageFolder> installed;
        ComPtr<Storage::IStorageItem> installedItem;
        HString path;
        result = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_ApplicationModel_Package).Get(), IID_PPV_ARGS(&packages));
        std::string caPath;
        if (SUCCEEDED(result) && SUCCEEDED(result = packages->get_Current(&package))
            && SUCCEEDED(result = package->get_InstalledLocation(&installed))
            && SUCCEEDED(result = installed.As(&installedItem)) && SUCCEEDED(result = installedItem->get_Path(path.GetAddressOf()))) {
            UINT32 packageLength = 0;
            const wchar_t* packageRaw = path.GetRawBuffer(&packageLength);
            std::wstring caPathW(packageRaw ? packageRaw : L"", packageLength);
            caPathW += kCaBundleFilename;
            if (GetFileAttributesW(caPathW.c_str()) == INVALID_FILE_ATTRIBUTES) {
                logResult("tls-ca-present", TlsProbeLogFalse);
            } else {
                logResult("tls-ca-present", TlsProbeLogTrue);
            }
            int caLength = WideCharToMultiByte(CP_UTF8, kWindowsConversionNoFlags, caPathW.c_str(), -1, nullptr, kEmptyOutputLength, nullptr, nullptr);
            if (caLength > static_cast<int>(kNulTerminatorCharacters)) {
                caPath.assign(caLength - kNulTerminatorCharacters, '\0');
                WideCharToMultiByte(CP_UTF8, 0, caPathW.c_str(), -1, caPath.data(), caLength, nullptr, nullptr);
            }
        } else {
            logResult("tls-package-path", result);
        }
        runTlsProbes(caPath.c_str());
        ComPtr<UICore::ICoreDispatcher> events;
        if (SUCCEEDED(m_window->get_Dispatcher(&events)))
            return events->ProcessEvents(UICore::CoreProcessEventsOption_ProcessUntilQuit);
        return S_OK;
    }

    // Releases the display keep-awake request when the window closes.
    HRESULT STDMETHODCALLTYPE Uninitialize() override
    {
        if (m_displayActive)
            m_displayRequest->RequestRelease();
        return S_OK;
    }

private:
    // CoreWindow used for activation and event pumping.
    ComPtr<UICore::ICoreWindow> m_window;

    // Display request held during synchronous TLS phases.
    ComPtr<ABI::Windows::System::Display::IDisplayRequest> m_displayRequest;

    // True only when RequestActive succeeded and release is required.
    bool m_displayActive { false };
};

// View factory named by the smoke manifest's EntryPoint.
class TlsViewSource final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, Core::IFrameworkViewSource> {
    InspectableClass(L"WebKitTlsSmoke.ViewSource", BaseTrust);

public:
    // Creates the TLS smoke view requested by the manifest entry point.
    HRESULT STDMETHODCALLTYPE CreateView(Core::IFrameworkView** view) override { return Make<TlsView>().CopyTo(view); }
};

// Package entry point: initializes WinRT and runs the TLS smoke view.
extern "C" void __stdcall WKTlsStartup()
{
    HRESULT result = RoInitialize(RO_INIT_MULTITHREADED);
    logResult("tls-startup", result);
    if (SUCCEEDED(result)) {
        ComPtr<Storage::IApplicationDataStatics> statics;
        ComPtr<Storage::IApplicationData> data;
        ComPtr<Storage::IStorageFolder> folder;
        HString path;
        if (SUCCEEDED(RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Storage_ApplicationData).Get(), IID_PPV_ARGS(&statics)))
            && SUCCEEDED(statics->get_Current(&data)) && SUCCEEDED(data->get_LocalFolder(&folder))) {
            ComPtr<Storage::IStorageItem> item;
            HString folderPath;
            if (SUCCEEDED(folder.As(&item)) && SUCCEEDED(item->get_Path(folderPath.GetAddressOf()))) {
                UINT32 folderLength = 0;
                const wchar_t* folderRaw = folderPath.GetRawBuffer(&folderLength);
                std::wstring filename(folderRaw ? folderRaw : L"", folderLength);
                filename += L"\\tls-smoke.txt";
                wcscpy_s(logFilename, filename.c_str());
                HANDLE file = CreateFile2(logFilename, GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, nullptr);
                if (file != INVALID_HANDLE_VALUE)
                    CloseHandle(file);
            }
        }
        ComPtr<Core::ICoreApplication> application;
        result = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_ApplicationModel_Core_CoreApplication).Get(), IID_PPV_ARGS(&application));
        if (SUCCEEDED(result))
            result = application->Run(Make<TlsViewSource>().Get());
        RoUninitialize();
    }
    ExitProcess(static_cast<UINT>(result));
}
