// Minimal native TLS probe: packaged libcurl + OpenSSL only.
// No XAML, no WebCore, no worker threads. Phases isolate plain-TCP,
// handshake-core and cert-store behavior with synchronous calls.
#include <windows.h>
#include <windows.applicationmodel.core.h>
#include <windows.storage.h>
#include <windows.ui.core.h>
#include <windows.system.display.h>
#include <winsock2.h>
#include <curl/curl.h>
#include <bcrypt.h>
#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/provider.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <stdio.h>
#include <string>

using namespace Microsoft::WRL;
using Microsoft::WRL::Wrappers::HString;
using Microsoft::WRL::Wrappers::HStringReference;
namespace Core = ABI::Windows::ApplicationModel::Core;
namespace UICore = ABI::Windows::UI::Core;
namespace Storage = ABI::Windows::Storage;

static HANDLE logHandle = INVALID_HANDLE_VALUE;
static wchar_t logFilename[1024];
static SRWLOCK logLock = SRWLOCK_INIT;

static void __stdcall logResult(const char* stage, unsigned long result)
{
    char text[160];
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

static size_t writeCallback(char* ptr, size_t size, size_t nmemb, void*)
{
    size_t count = size * nmemb;
    if (responseBody.size() + count > 256 * 1024)
        return 0; // abort: over probe cap
    responseBody.append(ptr, count);
    return count;
}

struct PhaseOutcome {
    int curlCode { -1 };
    long httpCode { 0 };
    unsigned bytes { 0 };
};

static PhaseOutcome runPhase(const char* url, bool verify, const char* caPath)
{
    PhaseOutcome outcome;
    logResult("tls-easy-init-enter", 0);
    CURL* handle = curl_easy_init();
    logResult("tls-easy-init-exit", handle ? 0u : 1u);
    if (!handle) {
        outcome.curlCode = -100;
        return outcome;
    }
    responseBody.clear();
    curl_easy_setopt(handle, CURLOPT_URL, url);
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 3L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 8L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(handle, CURLOPT_USERAGENT, "WebKitTlsSmoke/1.0");
    curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");
    if (verify) {
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
        curl_easy_setopt(handle, CURLOPT_CAINFO, caPath);
    } else {
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 0L);
    }
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, writeCallback);
    logResult("tls-perform-enter", 0);
    CURLcode code = curl_easy_perform(handle);
    logResult("tls-perform-exit", static_cast<unsigned>(code));
    outcome.curlCode = static_cast<int>(code);
    if (code == CURLE_OK)
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &outcome.httpCode);
    outcome.bytes = static_cast<unsigned>(responseBody.size());
    curl_easy_cleanup(handle);
    return outcome;
}

static void runTlsProbes(const char* caPath)
{
    // Entry/exit markers around every native call: a crash between a -enter
    // and its matching marker (with no matching marker) names the faulting
    // call exactly, without needing dump archaeology.
    unsigned char bcryptBytes[32];
    logResult("tls-bcrypt-enter", 0);
    NTSTATUS bcryptStatus = BCryptGenRandom(nullptr, bcryptBytes, sizeof(bcryptBytes), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    logResult("tls-bcrypt-exit", static_cast<unsigned>(bcryptStatus));
    logResult("tls-openssl-init-enter", 0);
    int minInit = OPENSSL_init_crypto(0, nullptr);
    logResult("tls-openssl-init-exit", static_cast<unsigned>(minInit));
    logResult("tls-libctx-new-enter", 0);
    OSSL_LIB_CTX* freshCtx = OSSL_LIB_CTX_new();
    logResult("tls-libctx-new-exit", freshCtx ? 1u : 0u);
    if (freshCtx)
        OSSL_LIB_CTX_free(freshCtx);
    logResult("tls-provider-enter", 0);
    OSSL_PROVIDER* defaultProvider = OSSL_PROVIDER_load(nullptr, "default");
    logResult("tls-provider-exit", defaultProvider ? 1u : 0u);
    logResult("tls-cipher-fetch-enter", 0);
    EVP_CIPHER* fetchedCipher = EVP_CIPHER_fetch(nullptr, "AES-256-CBC", nullptr);
    logResult("tls-cipher-fetch-exit", fetchedCipher ? 1u : 0u);
    if (fetchedCipher)
        EVP_CIPHER_free(fetchedCipher);
    logResult("tls-init-ciphers-enter", 0);
    int ciphersInit = OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_CIPHERS, nullptr);
    logResult("tls-init-ciphers-exit", static_cast<unsigned>(ciphersInit));
    logResult("tls-init-digests-enter", 0);
    int digestsInit = OPENSSL_init_crypto(OPENSSL_INIT_ADD_ALL_DIGESTS, nullptr);
    logResult("tls-init-digests-exit", static_cast<unsigned>(digestsInit));
    logResult("tls-init-strings-enter", 0);
    int stringsInit = OPENSSL_init_crypto(OPENSSL_INIT_LOAD_CRYPTO_STRINGS, nullptr);
    logResult("tls-init-strings-exit", static_cast<unsigned>(stringsInit));
    logResult("tls-err-enter", 0);
    ERR_clear_error();
    unsigned long errCode = ERR_get_error();
    logResult("tls-err-exit", errCode);
    logResult("tls-osslmalloc-enter", 0);
    void* heapProbe = OPENSSL_malloc(64);
    logResult("tls-osslmalloc-exit", heapProbe ? 1u : 0u);
    if (heapProbe)
        OPENSSL_free(heapProbe);
    // Split the RAND path: poll (OS entropy gathering) vs generate (DRBG
    // logic). Whichever enter lacks its exit is the faulting half.
    logResult("tls-poll-enter", 0);
    int pollStatus = RAND_poll();
    logResult("tls-poll-exit", static_cast<unsigned>(pollStatus));
    unsigned char randomBytes[16];
    logResult("tls-rand-enter", 0);
    int randStatus = RAND_bytes(randomBytes, sizeof(randomBytes));
    logResult("tls-rand-exit", static_cast<unsigned>(randStatus));
    logResult("tls-global-init-enter", 0);
    CURLcode globalCode = curl_global_init(CURL_GLOBAL_DEFAULT);
    logResult("tls-global-init-exit", static_cast<unsigned>(globalCode));
    // Phase A: plain HTTP, no crypto. Failure here means sockets/threads/CRT,
    // not TLS.
    PhaseOutcome plain = runPhase("http://neverssl.com/", false, nullptr);
    logResult("tls-plain-curl-code", static_cast<unsigned>(plain.curlCode));
    logResult("tls-plain-http-code", static_cast<unsigned>(plain.httpCode));
    logResult("tls-plain-bytes", plain.bytes);
    // Phase B: HTTPS without verification. Failure here with A passing means
    // handshake core (RAND/locks/codegen), not the cert store.
    PhaseOutcome insecure = runPhase("https://example.com/", false, nullptr);
    logResult("tls-insecure-curl-code", static_cast<unsigned>(insecure.curlCode));
    logResult("tls-insecure-http-code", static_cast<unsigned>(insecure.httpCode));
    logResult("tls-insecure-bytes", insecure.bytes);
    // Phase C: HTTPS with verification against the packaged Mozilla bundle.
    // Failure here with B passing means the X509_STORE/cacert path.
    PhaseOutcome verified = runPhase("https://example.com/", true, caPath);
    logResult("tls-verified-curl-code", static_cast<unsigned>(verified.curlCode));
    logResult("tls-verified-http-code", static_cast<unsigned>(verified.httpCode));
    logResult("tls-verified-bytes", verified.bytes);
    logResult("tls-done", 0);
}

class TlsView final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, Core::IFrameworkView> {
    InspectableClass(L"WebKitTlsSmoke.View", BaseTrust);
public:
    HRESULT STDMETHODCALLTYPE Initialize(Core::ICoreApplicationView* view) override
    {
        auto activated = Callback<ABI::Windows::Foundation::ITypedEventHandler<Core::CoreApplicationView*, ABI::Windows::ApplicationModel::Activation::IActivatedEventArgs*>>(
            [this](Core::ICoreApplicationView*, ABI::Windows::ApplicationModel::Activation::IActivatedEventArgs*) -> HRESULT {
                return m_window->Activate();
            });
        EventRegistrationToken token;
        return view->add_Activated(activated.Get(), &token);
    }
    HRESULT STDMETHODCALLTYPE SetWindow(UICore::ICoreWindow* window) override { m_window = window; return S_OK; }
    HRESULT STDMETHODCALLTYPE Load(HSTRING) override { return S_OK; }
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
        // Installed location for cacert.pem.
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
            caPathW += L"\\cacert.pem";
            if (GetFileAttributesW(caPathW.c_str()) == INVALID_FILE_ATTRIBUTES) {
                logResult("tls-ca-present", 0);
            } else {
                logResult("tls-ca-present", 1);
            }
            int caLength = WideCharToMultiByte(CP_UTF8, 0, caPathW.c_str(), -1, nullptr, 0, nullptr, nullptr);
            if (caLength > 1) {
                caPath.assign(caLength - 1, 0);
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
    HRESULT STDMETHODCALLTYPE Uninitialize() override
    {
        if (m_displayActive)
            m_displayRequest->RequestRelease();
        return S_OK;
    }
private:
    ComPtr<UICore::ICoreWindow> m_window;
    ComPtr<ABI::Windows::System::Display::IDisplayRequest> m_displayRequest;
    bool m_displayActive { false };
};

class TlsViewSource final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, Core::IFrameworkViewSource> {
    InspectableClass(L"WebKitTlsSmoke.ViewSource", BaseTrust);
public:
    HRESULT STDMETHODCALLTYPE CreateView(Core::IFrameworkView** view) override { return Make<TlsView>().CopyTo(view); }
};

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
