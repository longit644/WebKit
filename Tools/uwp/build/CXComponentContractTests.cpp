// External consumer: compile exclusively against the generated WebKit.winmd.
#include <activation.h>
#include <cstdio>
#include <cwchar>
#include <initializer_list>
#include <winstring.h>
#include <wrl.h>

using Microsoft::WRL::ComPtr;
using namespace WebKit;

// This is compiled but never invoked by the browser-free host. It checks every
// projected operation/overload/event name through generated metadata alone.
void CheckPublicMethodProjection(WebKitWebView^ view, Windows::Foundation::Uri^ uri, Platform::String^ html, WebKitCertificateFailureToken^ token)
{
    view->Source = uri;
    (void)view->Source;
    (void)view->CanGoBack;
    (void)view->CanGoForward;
    (void)view->DocumentTitle;
    (void)WebKitWebView::SourceProperty;
    (void)view->EnsureInitializedAsync();
    view->GoBack();
    view->GoForward();
    view->Reload();
    view->Stop();
    view->NavigateToString(html);
    view->NavigateToString(html, token);
    view->ProceedWithCertificateFailure(token);
    auto starting = view->NavigationStarting += ref new Windows::Foundation::TypedEventHandler<WebKitWebView^, WebKitNavigationStartingEventArgs^>(
        [](WebKitWebView^, WebKitNavigationStartingEventArgs^ args) { args->Cancel = true; });
    view->NavigationStarting -= starting;
    auto source = view->SourceChanged += ref new Windows::Foundation::TypedEventHandler<WebKitWebView^, WebKitSourceChangedEventArgs^>(
        [](WebKitWebView^, WebKitSourceChangedEventArgs^) { });
    view->SourceChanged -= source;
    auto history = view->HistoryChanged += ref new Windows::Foundation::TypedEventHandler<WebKitWebView^, WebKitHistoryChangedEventArgs^>(
        [](WebKitWebView^, WebKitHistoryChangedEventArgs^) { });
    view->HistoryChanged -= history;
    auto title = view->DocumentTitleChanged += ref new Windows::Foundation::TypedEventHandler<WebKitWebView^, WebKitDocumentTitleChangedEventArgs^>(
        [](WebKitWebView^, WebKitDocumentTitleChangedEventArgs^) { });
    view->DocumentTitleChanged -= title;
    auto completed = view->NavigationCompleted += ref new Windows::Foundation::TypedEventHandler<WebKitWebView^, WebKitNavigationCompletedEventArgs^>(
        [](WebKitWebView^, WebKitNavigationCompletedEventArgs^) { });
    view->NavigationCompleted -= completed;
    auto property = view->PropertyChanged += ref new Windows::UI::Xaml::Data::PropertyChangedEventHandler(
        [](Platform::Object^, Windows::UI::Xaml::Data::PropertyChangedEventArgs^) { });
    view->PropertyChanged -= property;
    view->Close();
}

// Negative compilation modes prove the consumer cannot create/modify a decision
// capability or write copied state. This source never includes authored API headers.
void CheckConsumerVisibility(WebKitWebView^ view, WebKitCertificateFailureToken^ token, WebKitNavigationCompletedEventArgs^ completed)
{
#if defined(TEST_TOKEN_CONSTRUCTION)
    token = ref new WebKitCertificateFailureToken();
#elif defined(TEST_TOKEN_MUTATION)
    token->FailedNavigationId = 1;
#elif defined(TEST_STATE_MUTATION)
    view->IsLoading = true;
#elif defined(TEST_EVENT_MUTATION)
    completed->ErrorCode = 0;
#else
    (void)view;
    (void)token;
    (void)completed;
    auto handler = ref new Windows::Foundation::TypedEventHandler<WebKitWebView^, WebKitNavigationCompletedEventArgs^>(
        [](WebKitWebView^, WebKitNavigationCompletedEventArgs^ args) {
            (void)args->ContentKind;
            (void)args->ErrorDomain;
            (void)args->HasHttpStatus;
            (void)args->CertificateFailureToken;
        });
    (void)handler;
#endif
}

// Native factory-export signature resolved dynamically from the component image.
using FactoryGetter = HRESULT(STDAPICALLTYPE*)(HSTRING, ::IActivationFactory**);
// Resolved component factory; the consumer never links against private framework code.
static FactoryGetter s_getFactory;
// Number of failed runtime contract assertions.
static unsigned s_failures;
// Number of runtime contract assertions evaluated.
static unsigned s_checks;

// Report and aggregate an assertion without allowing a later PASS to erase failure.
static void check(bool condition, const char* description)
{
    ++s_checks;
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition)
        ++s_failures;
}

// Request the supplied runtime class with balanced temporary HSTRING ownership.
static HRESULT getFactory(const wchar_t* name, ::IActivationFactory** factory)
{
    HSTRING id = nullptr;
    auto result = WindowsCreateString(name, static_cast<UINT32>(wcslen(name)), &id);
    if (FAILED(result))
        return result;
    result = s_getFactory(id, factory);
    WindowsDeleteString(id);
    return result;
}

#pragma warning(push)
#pragma warning(disable : 4447)
// Exercise exported factories and a generated-WinMD-only consumer on the x64 host.
int wmain(int argc, wchar_t** argv)
{
    if (argc != 2)
        return 2;
    auto initialized = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(initialized))
        return 3;
    auto module = LoadLibraryExW(argv[1], nullptr, 0);
    if (!module)
        return 4;
    s_getFactory = reinterpret_cast<FactoryGetter>(GetProcAddress(module, "DllGetActivationFactory"));
    auto canUnload = reinterpret_cast<HRESULT(STDAPICALLTYPE*)()>(GetProcAddress(module, "DllCanUnloadNow"));
    if (!s_getFactory || !canUnload)
        return 5;
    check(canUnload() == S_FALSE, "component remains resident");
    check(s_getFactory(nullptr, nullptr) == E_POINTER, "null factory output rejected");
    ComPtr<::IActivationFactory> factory;
    check(s_getFactory(nullptr, &factory) == CLASS_E_CLASSNOTAVAILABLE && !factory, "empty class rejected with null output");
    check(getFactory(L"WebKit.Unknown", &factory) == CLASS_E_CLASSNOTAVAILABLE && !factory, "unknown class rejected");
    // Spoofed class name that must not truncate to a valid activation identity.
    constexpr wchar_t truncated[] = L"WebKit.WebKitWebView\0hidden";
    HSTRING malformed = nullptr;
    WindowsCreateString(truncated, static_cast<UINT32>(sizeof(truncated) / sizeof(wchar_t) - 1), &malformed);
    check(s_getFactory(malformed, &factory) == CLASS_E_CLASSNOTAVAILABLE && !factory, "embedded-NUL class name cannot truncate activation matching");
    WindowsDeleteString(malformed);
    for (auto name : { L"WebKit.WebKitCertificateFailureToken", L"WebKit.WebKitNavigationStartingEventArgs", L"WebKit.WebKitNavigationCompletedEventArgs",
             L"WebKit.WebKitSourceChangedEventArgs", L"WebKit.WebKitHistoryChangedEventArgs", L"WebKit.WebKitDocumentTitleChangedEventArgs" }) {
        check(getFactory(name, &factory) == CLASS_E_CLASSNOTAVAILABLE && !factory, "internal-only class has no activation factory");
    }
    check(SUCCEEDED(getFactory(L"WebKit.WebKitWebView", &factory)) && factory, "public control factory resolves");
    if (factory) {
        ComPtr<IInspectable> queried;
        // Compiler-generated parameterized constructor identity frozen by metadata checks.
        const IID optionsFactory = { 0xfad2720e, 0x40fc, 0x342f, { 0xaa, 0x47, 0x60, 0xa6, 0xf7, 0xe9, 0xc7, 0x98 } };
        // Compiler-generated static SourceProperty identity frozen by metadata checks.
        const IID statics = { 0x3e0e3cac, 0xc5f0, 0x3bcf, { 0x9c, 0x82, 0x4e, 0xf3, 0x9c, 0x6b, 0x26, 0x95 } };
        check(SUCCEEDED(factory->QueryInterface(optionsFactory, &queried)), "generated parameterized factory implemented");
        queried.Reset();
        check(SUCCEEDED(factory->QueryInterface(statics, &queried)), "generated SourceProperty statics implemented");
        ComPtr<IInspectable> instance;
        check(factory->ActivateInstance(&instance) == RPC_E_WRONG_THREAD && !instance, "non-XAML construction returns HRESULT without partial object");
    }
    factory.Reset();
    check(SUCCEEDED(getFactory(L"WebKit.WebKitWebViewOptions", &factory)) && factory, "public options factory resolves");
    if (factory) {
        ComPtr<IInspectable> instance;
        check(SUCCEEDED(factory->ActivateInstance(&instance)) && instance, "options activate without engine or XAML");
        if (instance) {
            try {
                auto options = safe_cast<WebKitWebViewOptions^>(reinterpret_cast<Platform::Object^>(instance.Get()));
                check(options->CertificateAuthorityPath->IsEmpty() && options->FontDirectoryPath->IsEmpty(), "empty paths select package defaults");
                check(options->MaximumConcurrentRequests == 8 && options->MaximumResourceBytes == 32ULL * 1024 * 1024
                        && options->MaximumBufferedBytes == 16ULL * 1024 * 1024 && options->RequestTimeoutMilliseconds == 30000,
                    "projected option defaults agree with contract");
            } catch (Platform::Exception^ exception) {
                std::printf("Options projection HRESULT: 0x%08x\n", exception->HResult);
                check(false, "projected options contract");
            }
        }
    }
    check(canUnload() == S_FALSE, "resident contract after factory release");
    std::printf("M10 CX contract: %u checks, %u failures\n", s_checks, s_failures);
    RoUninitialize();
    return s_failures ? 1 : 0;
}
#pragma warning(pop)
