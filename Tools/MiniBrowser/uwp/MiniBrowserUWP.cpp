#include "WebKitWebViewUWP.h"
#include <windows.applicationmodel.h>
#include <windows.applicationmodel.activation.h>
#include <windows.foundation.h>
#include <windows.storage.h>
#include <windows.ui.core.h>
#include <windows.ui.xaml.markup.h>
#include <windows.ui.xaml.media.h>
#include <windows.system.display.h>
#include <winsock2.h>
#include <curl/curl.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <stdio.h>
#include <string>
#include <vector>

using namespace Microsoft::WRL;
using Microsoft::WRL::Wrappers::HString;
using Microsoft::WRL::Wrappers::HStringReference;
namespace Xaml = ABI::Windows::UI::Xaml;
namespace Media = ABI::Windows::UI::Xaml::Media;
namespace Controls = ABI::Windows::UI::Xaml::Controls;
namespace Foundation = ABI::Windows::Foundation;
namespace Activation = ABI::Windows::ApplicationModel::Activation;
namespace Storage = ABI::Windows::Storage;

static wchar_t logPath[1024];
static void __stdcall logResult(const char* stage, unsigned long result)
{
    char message[1024];
    int length = sprintf_s(message, "%s: 0x%08lx\r\n", stage, result);
    OutputDebugStringA(message);
    HANDLE file = CreateFile2(logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, OPEN_ALWAYS, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(file, message, length, &written, nullptr);
        CloseHandle(file);
    }
}

static HRESULT folderPath(Storage::IStorageFolder* folder, HString& path)
{
    ComPtr<Storage::IStorageItem> item;
    HRESULT hr = folder->QueryInterface(IID_PPV_ARGS(&item));
    return FAILED(hr) ? hr : item->get_Path(path.GetAddressOf());
}

static std::string plainHTML()
{
    std::string html = "<!doctype html><html><head><meta charset='utf-8'><style>body{font:18px 'DejaVu Sans';background:white;color:black;margin:24px}h1{font-size:28px}div{margin:18px 0}</style></head><body>";
    html += "<h1>Plain baseline</h1><p>No sticky, fixed, gradient, radius or shadow. Scroll only.</p>";
    for (unsigned i = 1; i <= 30; ++i)
        html += "<div>Plain section " + std::to_string(i) + ". Swipe up and down.</div>";
    html += "</body></html>";
    return html;
}

static std::string heavyHTML()
{
    std::string html = "<!doctype html><html><head><meta charset='utf-8'><style>body{font:18px 'DejaVu Sans';background:#101820;color:#f2f5f9;margin:24px}h1{font-size:28px}.heavy{margin:18px 0;padding:20px;border-radius:16px;background:linear-gradient(135deg,#1766a8,#20b878 60%,#f2b705);box-shadow:0 8px 24px rgba(0,0,0,.45);border:2px solid rgba(255,255,255,.35)}svg{display:block;width:100%;height:120px;margin:12px 0}img{width:48px;height:48px;margin:4px;vertical-align:middle}.spin{width:48px;height:48px;margin:12px auto;border-radius:50%;border:6px solid rgba(255,255,255,.25);border-top-color:#20b878;animation:spin 2s linear infinite}@keyframes spin{to{transform:rotate(360deg)}}</style></head><body>";
    html += "<h1>Heavy baseline</h1><p>Gradients, radius, shadow, inline images and SVG. No external loads. Spinner moved to about:motion.</p>";
    html += "<div><img src='data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAADAAAAAwCAIAAADYYG7QAAAAU0lEQVR4nO3QsQkAMQwEQRnsvl368xVs6mAGofjYNXPPzDu3//eSbVBQqChUFCoKFYWKQkWholBRqChUFCoKFYWKQkWholBRqChUFCoKFYWKQhM+9sMDFLSjbq4AAAAASUVORK5CYII=' alt='png'/>";
    html += "<img src='data:image/webp;base64,UklGRuAAAABXRUJQVlA4INQAAAAQBwCdASowADAAPmkokEW0IqGhMcgCgA0JaD0cofHCJqj9T2TpWOF4Dky38AVj/kkFwpmKL0Q8KCqM4X3KWBL9MAD+/aR2H/VrlCS0G5f/mUnm5mm4mPb79uFbJv4JPQJL/EA2FvajpCXV26jrJwhP6HzviWA+9lfQW3pDD/8oG9//0yQsWULOXTqJz2ukNEmXlGfEhFlstmnhz0rTr4DkBQgOayGbXkspRulEH2qf/VX3/9MjbeRgl9F+OifI2MGP3iL5zk6nyQIprbf3/R9WERgAAA==' alt='webp'/>";
    html += "<img src='data:image/jpeg;base64,/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAYEBQYFBAYGBQYHBwYIChAKCgkJChQODwwQFxQYGBcUFhYaHSUfGhsjHBYWICwgIyYnKSopGR8tMC0oMCUoKSj/2wBDAQcHBwoIChMKChMoGhYaKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCgoKCj/wAARCAAwADADASIAAhEBAxEB/8QAHwAAAQUBAQEBAQEAAAAAAAAAAAECAwQFBgcICQoL/8QAtRAAAgEDAwIEAwUFBAQAAAF9AQIDAAQRBRIhMUEGE1FhByJxFDKBkaEII0KxwRVS0fAkM2JyggkKFhcYGRolJicoKSo0NTY3ODk6Q0RFRkdISUpTVFVWV1hZWmNkZWZnaGlqc3R1dnd4eXqDhIWGh4iJipKTlJWWl5iZmqKjpKWmp6ipqrKztLW2t7i5usLDxMXGx8jJytLT1NXW19jZ2uHi4+Tl5ufo6erx8vP09fb3+Pn6/8QAHwEAAwEBAQEBAQEBAQAAAAAAAAECAwQFBgcICQoL/8QAtREAAgECBAQDBAcFBAQAAQJ3AAECAxEEBSExBhJBUQdhcRMiMoEIFEKRobHBCSMzUvAVYnLRChYkNOEl8RcYGRomJygpKjU2Nzg5OkNERUZHSElKU1RVVldYWVpjZGVmZ2hpanN0dXZ3eHl6goOEhYaHiImKkpOUlZaXmJmaoqOkpaanqKmqsrO0tba3uLm6wsPExcbHyMnK0tPU1dbX2Nna4uPk5ebn6Onq8vP09fb3+Pn6/9oADAMBAAIRAxEAPwD58gtOnFaEFp04rQgtPatCC09q+0qYk58Fi9jPgtOnFaEFp04rQgtPatCC09q8+piT63BYvYz4LTpxWhBadOK0ILT2q/Bae1cFTEn12Cxexx8Fp04rQgtOnFX4LT2rQgtPaipiT+fcFi9ihBadOK0ILTpxV+C09q0ILT2rgqYk+uwWL2KEFp04rQgtPar8Fp7VoQWntXBUxJ9dgsXscfBadOK0ILTpxV+C09q0ILT2oqYk/n3BYvYoQWnTitCC06cVfgtPatCC09q4KmJPrsFi9jPgtPatCC06cVoQWntWhBae1efUxJ9bgsXsf//Z' alt='jpg'/></div>";
    for (unsigned i = 1; i <= 20; ++i) {
        html += "<div class='heavy'>Heavy section " + std::to_string(i) + ". Swipe to test GPU raster.</div>";
        html += "<svg viewBox='0 0 300 120'><defs><linearGradient id='g' x1='0' y1='0' x2='1' y2='1'><stop offset='0' stop-color='#1766a8'/><stop offset='1' stop-color='#20b878'/></linearGradient></defs><rect x='4' y='4' width='292' height='112' rx='18' fill='url(#g)'/><circle cx='70' cy='60' r='34' fill='white' opacity='.85'/><rect x='130' y='24' width='140' height='72' rx='12' fill='black' opacity='.35'/></svg>";
    }
    html += "</body></html>";
    return html;
}

static std::string demoHTML(bool second)
{
    std::string html = "<!doctype html><html><head><meta charset='utf-8'><style>body{font:18px 'DejaVu Sans';background:#edf3fa;color:#14243a;margin:24px}h1{color:#1766a8;font-size:28px}button{font:20px 'DejaVu Sans';padding:16px;background:#20b878;color:#102d21;border:0;border-radius:8px}.card{padding:20px;background:white;margin:18px 0;border-radius:12px}footer{margin:30px 0}</style></head><body>";
    html += second ? "<h1>Second local page</h1><p>Use Back to return to the first page.</p>" : "<h1>WebKitWebViewUWP</h1><p>Persistent WebCore page inside a XAML SwapChainPanel.</p>";
    html += "<div style='position:sticky;top:0;background:#1766a8;color:white;padding:8px;z-index:2'>Sticky layer: stays at the top while scrolling</div><div style='position:fixed;right:6px;bottom:6px;background:#14243a;color:white;padding:6px;font-size:12px;z-index:3'>Fixed layer</div>";
    html += "<button onclick=\"this.textContent='Clicked '+(++window.clickCount)+' times'\">Tap this button</button><script>window.clickCount=0;</script>";
    for (unsigned i = 1; i <= 18; ++i)
        html += "<div class='card'>Scroll section " + std::to_string(i) + ". Swipe up and down to test persistent-page scrolling.</div>";
    html += "<footer>End of page. Try about:second in the address box, then Back and Reload.</footer></body></html>";
    return html;
}

class MiniBrowserApp final : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, Xaml::IApplicationOverrides, ComposableBase<>> {
    InspectableClass(L"WebKitWebView.MiniBrowser.App", BaseTrust);
public:
    ~MiniBrowserApp() { if (m_displayActive) m_displayRequest->RequestRelease(); }
    HRESULT RuntimeClassInitialize()
    {
        InitializeSRWLock(&m_fetchLock);
        m_fetchInFlight = false;
        m_fetchReady = false;
        m_fetchGeneration = 0;
        ComPtr<Xaml::IApplicationFactory> factory;
        ComPtr<IInspectable> inner;
        ComPtr<Xaml::IApplication> application;
        HRESULT hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_UI_Xaml_Application).Get(), IID_PPV_ARGS(&factory));
        if (FAILED(hr) || FAILED(hr = factory->CreateInstance(this, &inner, &application))) return hr;
        return SetComposableBasePointers(inner.Get());
    }
    HRESULT STDMETHODCALLTYPE OnLaunched(Activation::ILaunchActivatedEventArgs*) override { return openWindow(); }
    HRESULT STDMETHODCALLTYPE OnActivated(Activation::IActivatedEventArgs*) override { return openWindow(); }
    HRESULT STDMETHODCALLTYPE OnFileActivated(Activation::IFileActivatedEventArgs*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnSearchActivated(Activation::ISearchActivatedEventArgs*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnShareTargetActivated(Activation::IShareTargetActivatedEventArgs*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnFileOpenPickerActivated(Activation::IFileOpenPickerActivatedEventArgs*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnFileSavePickerActivated(Activation::IFileSavePickerActivatedEventArgs*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnCachedFileUpdaterActivated(Activation::ICachedFileUpdaterActivatedEventArgs*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnWindowCreated(Xaml::IWindowCreatedEventArgs*) override { return S_OK; }
private:
    HRESULT find(const wchar_t* name, IInspectable** object) { return m_root->FindName(HStringReference(name).Get(), object); }
    void status(const wchar_t* text, HRESULT result = S_OK)
    {
        if (m_status) m_status->put_Text(HStringReference(text).Get());
        logResult("minibrowser-status", result);
    }
    // Remote fetch probe via packaged libcurl/OpenSSL on a Win32 worker thread.
    // No COM or XAML in the worker; results are consumed on the UI Rendering
    // tick. Windows.Web.Http is deliberately not used: it crashes inside the
    // system HTTP/twinapi stack on this device (dumps 7956/5976/2520).
    struct FetchRequest {
        MiniBrowserApp* app;
        std::string url;
        std::string caPath;
        std::wstring uriW;
        bool addHistory;
        unsigned generation;
    };
    struct FetchResult {
        bool transportOk { false };
        long httpCode { 0 };
        int curlCode { 0 };
        bool tooLarge { false };
        std::string body;
        std::wstring uriW;
        bool addHistory { false };
        unsigned generation { 0 };
    };
    static size_t fetchWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata)
    {
        auto* body = static_cast<std::string*>(userdata);
        size_t count = size * nmemb;
        if (body->size() + count > 512 * 1024) return 0; // abort: over probe cap
        body->append(ptr, count);
        return count;
    }
    static INIT_ONCE s_curlInitOnce;
    static BOOL CALLBACK curlInitOnceCallback(PINIT_ONCE, PVOID, PVOID*) { curl_global_init(CURL_GLOBAL_DEFAULT); return TRUE; }
    // Secure default. A one-off insecure diagnostic (1.0.0.29) proved the
    // libcrypto LH_insert crash is NOT in the X509_STORE path (it faults
    // identically with verification off), so verification stays ON.
    static constexpr bool kFetchInsecureDiagnostic = false;
    static DWORD WINAPI fetchThreadProc(LPVOID lpParam)
    {
        auto* request = static_cast<FetchRequest*>(lpParam);
        FetchResult result;
        result.uriW = request->uriW;
        result.addHistory = request->addHistory;
        result.generation = request->generation;
        InitOnceExecuteOnce(&s_curlInitOnce, curlInitOnceCallback, nullptr, nullptr);
        if (CURL* handle = curl_easy_init()) {
            curl_easy_setopt(handle, CURLOPT_URL, request->url.c_str());
            curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 5L);
            curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 10L);
            curl_easy_setopt(handle, CURLOPT_TIMEOUT, 25L);
            curl_easy_setopt(handle, CURLOPT_USERAGENT, "WebKitWebViewUWP MiniBrowser");
            curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");
            if (kFetchInsecureDiagnostic) {
                curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);
                curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 0L);
                logResult("minibrowser-fetch-insecure-diagnostic", 1);
            } else {
                curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
                curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
                curl_easy_setopt(handle, CURLOPT_CAINFO, request->caPath.c_str());
            }
            curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, fetchWriteCallback);
            curl_easy_setopt(handle, CURLOPT_WRITEDATA, &result.body);
            CURLcode code = curl_easy_perform(handle);
            result.curlCode = static_cast<int>(code);
            if (code == CURLE_OK) {
                result.transportOk = true;
                curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &result.httpCode);
            } else if (code == CURLE_WRITE_ERROR) {
                result.tooLarge = true;
            }
            logResult("minibrowser-fetch-curl-code", static_cast<unsigned>(code));
            logResult("minibrowser-fetch-bytes", static_cast<unsigned>(result.body.size()));
            logResult("minibrowser-fetch-http-code", static_cast<unsigned>(result.httpCode > 0 ? result.httpCode : 0));
            curl_easy_cleanup(handle);
        } else {
            logResult("minibrowser-fetch-easy-init", E_FAIL);
        }
        AcquireSRWLockExclusive(&request->app->m_fetchLock);
        request->app->m_fetchResult = std::move(result);
        request->app->m_fetchReady = true;
        ReleaseSRWLockExclusive(&request->app->m_fetchLock);
        delete request;
        return 0;
    }
    void consumeFetchResult()
    {
        // UI thread only, from the Rendering tick.
        FetchResult result;
        AcquireSRWLockExclusive(&m_fetchLock);
        if (!m_fetchReady) { ReleaseSRWLockExclusive(&m_fetchLock); return; }
        result = std::move(m_fetchResult);
        m_fetchResult = FetchResult();
        m_fetchReady = false;
        m_fetchInFlight = false;
        ReleaseSRWLockExclusive(&m_fetchLock);
        if (result.generation != m_fetchGeneration) return; // superseded
        if (!result.transportOk) { status(L"Fetch failed; see minibrowser.log.", E_FAIL); return; }
        if (result.tooLarge) { status(L"Remote page too large for v0 probe (512KB cap).", E_FAIL); return; }
        if (result.httpCode < 200 || result.httpCode >= 300 || result.body.empty()) {
            wchar_t message[128];
            swprintf_s(message, L"Remote fetch HTTP %ld.", result.httpCode);
            status(message, E_FAIL);
            return;
        }
        HRESULT hr = m_view->loadHTML(result.body.c_str());
        if (FAILED(hr)) { status(L"Remote HTML load failed; see log.", hr); return; }
        if (result.addHistory) m_history.push_back(result.uriW);
        m_address->put_Text(HStringReference(result.uriW.c_str()).Get());
        status(L"Remote main HTML loaded. Subresources (CSS/JS/images) still pending.");
        WKPageStateUWP state;
        if (SUCCEEDED(m_view->pageState(state))) {
            logResult("minibrowser-content-height", state.contentHeight);
            logResult("minibrowser-scroll-y", state.scrollY);
        }
    }
    HRESULT fetchRemote(const std::wstring& uri, bool addHistory)
    {
        AcquireSRWLockExclusive(&m_fetchLock);
        if (m_fetchInFlight) { ReleaseSRWLockExclusive(&m_fetchLock); status(L"Fetch already in progress.", HRESULT_FROM_WIN32(ERROR_BUSY)); return S_OK; }
        m_fetchInFlight = true;
        ++m_fetchGeneration;
        unsigned generation = m_fetchGeneration;
        ReleaseSRWLockExclusive(&m_fetchLock);
        status(L"Fetching remote page (main HTML only; subresources pending)...", S_OK);
        logResult("minibrowser-fetch-start", 0);
        int urlLength = WideCharToMultiByte(CP_UTF8, 0, uri.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::wstring caPathW = m_packagePath + L"\\cacert.pem";
        int caLength = WideCharToMultiByte(CP_UTF8, 0, caPathW.c_str(), -1, nullptr, 0, nullptr, nullptr);
        if (urlLength <= 1 || caLength <= 1) {
            AcquireSRWLockExclusive(&m_fetchLock);
            m_fetchInFlight = false;
            ReleaseSRWLockExclusive(&m_fetchLock);
            status(L"Fetch failed: bad URL or install path.", E_INVALIDARG);
            return S_OK;
        }
        auto* request = new (std::nothrow) FetchRequest();
        if (!request) {
            AcquireSRWLockExclusive(&m_fetchLock);
            m_fetchInFlight = false;
            ReleaseSRWLockExclusive(&m_fetchLock);
            return E_OUTOFMEMORY;
        }
        request->app = this;
        request->url.assign(urlLength - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, uri.c_str(), -1, request->url.data(), urlLength, nullptr, nullptr);
        request->caPath.assign(caLength - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, caPathW.c_str(), -1, request->caPath.data(), caLength, nullptr, nullptr);
        request->uriW = uri;
        request->addHistory = addHistory;
        request->generation = generation;
        if (GetFileAttributesW(caPathW.c_str()) == INVALID_FILE_ATTRIBUTES) {
            logResult("minibrowser-fetch-no-ca", E_FAIL);
            delete request;
            AcquireSRWLockExclusive(&m_fetchLock);
            m_fetchInFlight = false;
            ReleaseSRWLockExclusive(&m_fetchLock);
            status(L"Fetch failed: CA bundle missing from package.", E_FAIL);
            return S_OK;
        }
        HANDLE thread = CreateThread(nullptr, 0, fetchThreadProc, request, 0, nullptr);
        if (!thread) {
            HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
            logResult("minibrowser-fetch-thread", hr);
            delete request;
            AcquireSRWLockExclusive(&m_fetchLock);
            m_fetchInFlight = false;
            ReleaseSRWLockExclusive(&m_fetchLock);
            status(L"Fetch failed to start worker.", hr);
            return S_OK;
        }
        CloseHandle(thread);
        return S_OK;
    }
    HRESULT navigate(const std::wstring& uri, bool addHistory)
    {
        if (uri.rfind(L"http://", 0) == 0 || uri.rfind(L"https://", 0) == 0)
            return fetchRemote(uri, addHistory);
        if (uri != L"about:demo" && uri != L"about:second" && uri != L"about:plain" && uri != L"about:heavy") {
            status(L"Unknown local page. Try about:demo, about:plain, about:heavy, or an http(s) URL.", E_INVALIDARG);
            return S_OK;
        }
        AcquireSRWLockExclusive(&m_fetchLock);
        ++m_fetchGeneration;
        m_fetchReady = false;
        m_fetchInFlight = false;
        ReleaseSRWLockExclusive(&m_fetchLock);
        auto html = uri == L"about:plain" ? plainHTML() : uri == L"about:heavy" ? heavyHTML() : demoHTML(uri == L"about:second");
        HRESULT hr = m_view->loadHTML(html.c_str());
        if (FAILED(hr)) { status(L"Page load failed; see minibrowser.log", hr); return hr; }
        if (addHistory) m_history.push_back(uri);
        m_address->put_Text(HStringReference(uri.c_str()).Get());
        status(L"Local page loaded. Tap button, swipe to scroll.");
        WKPageStateUWP state;
        if (SUCCEEDED(m_view->pageState(state))) {
            logResult("minibrowser-content-height", state.contentHeight);
            logResult("minibrowser-scroll-y", state.scrollY);
        }
        return S_OK;
    }
    HRESULT openWindow()
    {
        logResult("minibrowser-launched", 0);
        if (m_window) return m_window->Activate();
        ComPtr<Xaml::IWindowStatics> windows;
        HRESULT hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_UI_Xaml_Window).Get(), IID_PPV_ARGS(&windows));
        if (FAILED(hr) || FAILED(hr = windows->get_Current(&m_window))) return hr;
        ComPtr<ABI::Windows::UI::Xaml::Markup::IXamlReaderStatics> reader;
        ComPtr<IInspectable> root;
        hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_UI_Xaml_Markup_XamlReader).Get(), IID_PPV_ARGS(&reader));
        const wchar_t* markup = LR"(<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Background="#EDF3FA" RequestedTheme="Light">
<Grid.RowDefinitions><RowDefinition Height="Auto"/><RowDefinition Height="*"/><RowDefinition Height="Auto"/></Grid.RowDefinitions>
<StackPanel Orientation="Horizontal"><Button x:Name="Back" Content="Back"/><Button x:Name="Reload" Content="Reload"/><TextBox x:Name="Address" Text="about:demo" Width="140"/><Button x:Name="Go" Content="Go"/><Button x:Name="Capture" Content="Capture"/></StackPanel>
<ContentControl x:Name="Browser" Grid.Row="1" HorizontalContentAlignment="Stretch" VerticalContentAlignment="Stretch"/>
<TextBlock x:Name="Status" Grid.Row="2" TextWrapping="Wrap" Margin="8" Text="Initializing WebKitWebViewUWP..."/>
</Grid>)";
        if (FAILED(hr) || FAILED(hr = reader->Load(HStringReference(markup).Get(), &root))
            || FAILED(hr = root.As(&m_root))) { logResult("minibrowser-xaml-root", hr); return hr; }
        ComPtr<IInspectable> control;
        if (FAILED(hr = find(L"Address", &control)) || FAILED(hr = control.As(&m_address))) return hr;
        control.Reset();
        if (FAILED(hr = find(L"Status", &control)) || FAILED(hr = control.As(&m_status))) return hr;
        ComPtr<ABI::Windows::ApplicationModel::IPackageStatics> packages;
        ComPtr<ABI::Windows::ApplicationModel::IPackage> package;
        ComPtr<Storage::IStorageFolder> folder;
        HString packagePath;
        hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_ApplicationModel_Package).Get(), IID_PPV_ARGS(&packages));
        if (FAILED(hr) || FAILED(hr = packages->get_Current(&package)) || FAILED(hr = package->get_InstalledLocation(&folder))
            || FAILED(hr = folderPath(folder.Get(), packagePath))) return hr;
        wchar_t fonts[1024];
        swprintf_s(fonts, L"%ls\\Fonts", packagePath.GetRawBuffer(nullptr));
        {
            UINT32 packageLength = 0;
            const wchar_t* packageRaw = packagePath.GetRawBuffer(&packageLength);
            m_packagePath.assign(packageRaw ? packageRaw : L"", packageLength);
        }
        m_view = std::make_unique<WebKit::WebKitWebViewUWP>();
        hr = m_view->initialize(fonts, logResult);
        logResult("minibrowser-control-initialize", hr);
        if (FAILED(hr)) { status(L"Control initialization failed; see minibrowser.log", hr); return hr; }
        control.Reset();
        ComPtr<Controls::IContentControl> browser;
        ComPtr<IInspectable> view;
        if (FAILED(hr = find(L"Browser", &control)) || FAILED(hr = control.As(&browser))
            || FAILED(hr = m_view->xamlElement()->QueryInterface(IID_PPV_ARGS(&view)))
            || FAILED(hr = browser->put_Content(view.Get()))) return hr;
        auto go = Callback<Xaml::IRoutedEventHandler>([this](IInspectable*, Xaml::IRoutedEventArgs*) -> HRESULT {
            HString uri;
            HRESULT hr = m_address->get_Text(uri.GetAddressOf());
            return FAILED(hr) ? hr : navigate(uri.GetRawBuffer(nullptr), true);
        });
        auto reload = Callback<Xaml::IRoutedEventHandler>([this](IInspectable*, Xaml::IRoutedEventArgs*) -> HRESULT {
            return m_history.empty() ? S_OK : navigate(m_history.back(), false);
        });
        auto back = Callback<Xaml::IRoutedEventHandler>([this](IInspectable*, Xaml::IRoutedEventArgs*) -> HRESULT {
            if (m_history.size() < 2) return S_OK;
            m_history.pop_back();
            return navigate(m_history.back(), false);
        });
        auto capture = Callback<Xaml::IRoutedEventHandler>([this](IInspectable*, Xaml::IRoutedEventArgs*) -> HRESULT {
            std::wstring filename(logPath);
            filename.resize(filename.find_last_of(L"\\/") + 1);
            filename += L"page.png";
            HRESULT hr = m_view->savePNG(filename.c_str());
            status(SUCCEEDED(hr) ? L"Page image saved to LocalState/page.png" : L"Capture failed; see minibrowser.log", hr);
            return S_OK;
        });
        if (FAILED(hr = hookButton(L"Go", go.Get())) || FAILED(hr = hookButton(L"Reload", reload.Get()))
            || FAILED(hr = hookButton(L"Back", back.Get())) || FAILED(hr = hookButton(L"Capture", capture.Get()))) return hr;
        // Poll completed remote fetches on the UI thread each frame.
        ComPtr<Media::ICompositionTargetStatics> composition;
        if (FAILED(hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_UI_Xaml_Media_CompositionTarget).Get(), IID_PPV_ARGS(&composition)))) { logResult("minibrowser-fetch-poll-target", hr); return hr; }
        auto fetchTick = Callback<Foundation::IEventHandler<IInspectable*>>([this](IInspectable*, IInspectable*) -> HRESULT { consumeFetchResult(); return S_OK; });
        if (FAILED(hr = composition->add_Rendering(fetchTick.Get(), &m_fetchRenderToken))) { logResult("minibrowser-fetch-poll-tick", hr); return hr; }
        ComPtr<Xaml::IUIElement> element;
        if (FAILED(hr = root.As(&element)) || FAILED(hr = m_window->put_Content(element.Get()))) return hr;
        // Hide the system status bar (clock/battery/signal) so it never covers
        // the app's top toolbar. Best effort; layout works even if hidden fails.
        {
            namespace ViewManagement = ABI::Windows::UI::ViewManagement;
            ComPtr<ViewManagement::IStatusBarStatics> statusStatics;
            ComPtr<ViewManagement::IStatusBar> statusBar;
            ComPtr<ABI::Windows::Foundation::IAsyncAction> hideOp;
            if (SUCCEEDED(RoGetActivationFactory(HStringReference(RuntimeClass_Windows_UI_ViewManagement_StatusBar).Get(), IID_PPV_ARGS(&statusStatics)))
                && SUCCEEDED(statusStatics->GetForCurrentView(&statusBar)) && statusBar
                && SUCCEEDED(statusBar->HideAsync(&hideOp)) && hideOp)
                logResult("minibrowser-statusbar-hide", 0);
            else
                logResult("minibrowser-statusbar-hide", E_FAIL);
        }
        hr = m_window->Activate();
        if (SUCCEEDED(hr)) {
            ComPtr<IInspectable> display;
            if (SUCCEEDED(RoActivateInstance(HStringReference(RuntimeClass_Windows_System_Display_DisplayRequest).Get(), &display))
                && SUCCEEDED(display.As(&m_displayRequest)))
                m_displayActive = SUCCEEDED(m_displayRequest->RequestActive());
        }
        return FAILED(hr) ? hr : navigate(L"about:demo", true);
    }
    HRESULT hookButton(const wchar_t* name, Xaml::IRoutedEventHandler* handler)
    {
        ComPtr<IInspectable> object;
        ComPtr<Controls::Primitives::IButtonBase> button;
        HRESULT hr = find(name, &object);
        if (FAILED(hr) || FAILED(hr = object.As(&button))) return hr;
        EventRegistrationToken token;
        return button->add_Click(handler, &token);
    }
    ComPtr<Xaml::IWindow> m_window;
    ComPtr<Xaml::IFrameworkElement> m_root;
    ComPtr<Controls::ITextBox> m_address;
    ComPtr<Controls::ITextBlock> m_status;
    std::unique_ptr<WebKit::WebKitWebViewUWP> m_view;
    std::vector<std::wstring> m_history;
    std::wstring m_packagePath;
    SRWLOCK m_fetchLock;
    bool m_fetchInFlight { false };
    bool m_fetchReady { false };
    unsigned m_fetchGeneration { 0 };
    FetchResult m_fetchResult;
    EventRegistrationToken m_fetchRenderToken { };
    ComPtr<ABI::Windows::System::Display::IDisplayRequest> m_displayRequest;
    bool m_displayActive { false };
};

INIT_ONCE MiniBrowserApp::s_curlInitOnce = INIT_ONCE_STATIC_INIT;

extern "C" void __stdcall WKMiniBrowserStartup()
{
    HRESULT hr = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(hr)) ExitProcess(hr);
    ComPtr<Storage::IApplicationDataStatics> statics;
    ComPtr<Storage::IApplicationData> data;
    ComPtr<Storage::IStorageFolder> folder;
    HString path;
    hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Storage_ApplicationData).Get(), IID_PPV_ARGS(&statics));
    if (SUCCEEDED(hr) && SUCCEEDED(hr = statics->get_Current(&data)) && SUCCEEDED(hr = data->get_LocalFolder(&folder))
        && SUCCEEDED(hr = folderPath(folder.Get(), path))) {
        swprintf_s(logPath, L"%ls\\minibrowser.log", path.GetRawBuffer(nullptr));
        HANDLE file = CreateFile2(logPath, GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, nullptr);
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
    }
    logResult("minibrowser-startup", hr);
    ComPtr<Xaml::IApplicationStatics> application;
    hr = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_UI_Xaml_Application).Get(), IID_PPV_ARGS(&application));
    if (SUCCEEDED(hr)) {
        auto initialize = Callback<Xaml::IApplicationInitializationCallback>([](Xaml::IApplicationInitializationCallbackParams*) -> HRESULT {
            ComPtr<MiniBrowserApp> app;
            HRESULT hr = MakeAndInitialize<MiniBrowserApp>(&app);
            logResult("minibrowser-application-initialize", hr);
            if (SUCCEEDED(hr)) app.Detach(); // Application lifetime, released at process teardown.
            return hr;
        });
        hr = application->Start(initialize.Get());
    }
    logResult("minibrowser-exit", hr);
    RoUninitialize();
    ExitProcess(hr);
}
