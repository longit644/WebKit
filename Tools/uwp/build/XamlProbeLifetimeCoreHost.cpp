// Native UWP XAML Application host for the XAML probe lifetime gate.
//
// The console harness deliberately proves the failure path, but a UserControl
// must be constructed by the XAML application runtime on its owning thread. This
// host follows the composable Application::Start path used by MiniBrowserUWP.cpp
// and keeps a visible status window open after the checks finish.

#include <windows.applicationmodel.activation.h>
#include <windows.storage.h>
#include <windows.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <windows.ui.xaml.controls.h>
#include <windows.ui.xaml.h>
#include <windows.ui.xaml.markup.h>
#include <windows.ui.xaml.data.h>
#include <windows.ui.core.h>
#include <XamlProbe.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>

#include <cstdio>
#include <cstring>
#include <cwchar>

using namespace Microsoft::WRL;
using Microsoft::WRL::Wrappers::HString;
using Microsoft::WRL::Wrappers::HStringReference;

namespace Activation = ABI::Windows::ApplicationModel::Activation;
namespace Storage = ABI::Windows::Storage;
namespace Xaml = ABI::Windows::UI::Xaml;
namespace Controls = ABI::Windows::UI::Xaml::Controls;
namespace Markup = ABI::Windows::UI::Xaml::Markup;

using ProbeInterface = __x_ABI_CWebKitProbe_CIXamlProbe;
using GetActivationFactoryFn = HRESULT(STDMETHODCALLTYPE*)(HSTRING, IActivationFactory**);

extern int RunXamlProbeLifetimeChecks(HMODULE, const char*);

namespace {

LONG gExitCode = 1;
char gDiagnostics[4096] = "version=1.0.0.27\r\n";

void SetExitCode(int result)
{
    InterlockedExchange(&gExitCode, result);
}

void WriteResultToLocalState(LONG result, HRESULT detail = S_OK)
{
    ComPtr<Storage::IApplicationDataStatics> statics;
    HRESULT status = RoGetActivationFactory(
        HStringReference(RuntimeClass_Windows_Storage_ApplicationData).Get(),
        IID_PPV_ARGS(&statics));
    if (FAILED(status))
        return;

    ComPtr<Storage::IApplicationData> data;
    if (FAILED(status = statics->get_Current(&data)))
        return;

    ComPtr<Storage::IStorageFolder> folder;
    if (FAILED(status = data->get_LocalFolder(&folder)))
        return;

    ComPtr<Storage::IStorageItem> item;
    HString path;
    if (FAILED(status = folder.As(&item)) || FAILED(status = item->get_Path(path.GetAddressOf())))
        return;

    wchar_t filename[MAX_PATH];
    if (swprintf_s(filename, L"%ls\\xaml-probe-lifetime.txt", path.GetRawBuffer(nullptr)) < 0)
        return;

    HANDLE handle = CreateFile2(filename, GENERIC_WRITE, FILE_SHARE_READ, CREATE_ALWAYS, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return;

    char text[128];
    int length = sprintf_s(text, "exit=%ld hr=0x%08lX\r\n", result, static_cast<unsigned long>(detail));
    if (length > 0) {
        DWORD written = 0;
        WriteFile(handle, text, static_cast<DWORD>(length), &written, nullptr);
        WriteFile(handle, gDiagnostics, static_cast<DWORD>(strlen(gDiagnostics)), &written, nullptr);
        FlushFileBuffers(handle);
    }
    CloseHandle(handle);
}

extern "C" void XamlProbeRecordStage(int stage)
{
    WriteResultToLocalState(stage);
}

extern "C" void XamlProbeRecordHResult(int stage, HRESULT detail)
{
    WriteResultToLocalState(stage, detail);
}

bool HStringEquals(HSTRING actual, const wchar_t* expected)
{
    UINT32 length = 0;
    const wchar_t* value = WindowsGetStringRawBuffer(actual, &length);
    return value && length == static_cast<UINT32>(wcslen(expected))
        && wcsncmp(value, expected, length) == 0;
}

HRESULT ActivateProbe(HMODULE component, IInspectable** instance)
{
    if (!instance)
        return E_POINTER;
    *instance = nullptr;

    auto entry = reinterpret_cast<GetActivationFactoryFn>(
        GetProcAddress(component, "DllGetActivationFactory"));
    if (!entry)
        return HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);

    HStringReference className(L"WebKitProbe.XamlProbe");
    ComPtr<IActivationFactory> factory;
    HRESULT result = entry(className.Get(), &factory);
    if (FAILED(result))
        return result;
    return factory->ActivateInstance(instance);
}

class ProbeApplication final
    : public RuntimeClass<RuntimeClassFlags<WinRtClassicComMix>, Xaml::IApplicationOverrides, ComposableBase<>> {
    InspectableClass(L"WebKitProbe.XamlLifetimeHost.App", BaseTrust);

public:
    HRESULT RuntimeClassInitialize()
    {
        ComPtr<Xaml::IApplicationFactory> factory;
        ComPtr<IInspectable> inner;
        ComPtr<Xaml::IApplication> application;
        HRESULT result = RoGetActivationFactory(
            HStringReference(RuntimeClass_Windows_UI_Xaml_Application).Get(),
            IID_PPV_ARGS(&factory));
        if (FAILED(result))
            return result;
        result = factory->CreateInstance(this, &inner, &application);
        if (FAILED(result))
            return result;
        return SetComposableBasePointers(inner.Get());
    }

    HRESULT STDMETHODCALLTYPE OnLaunched(Activation::ILaunchActivatedEventArgs*) override
    {
        HRESULT result = OpenWindow();
        if (FAILED(result))
            SetExitCode(1);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnActivated(Activation::IActivatedEventArgs*) override
    {
        if (m_window)
            return m_window->Activate();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnFileActivated(Activation::IFileActivatedEventArgs*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnSearchActivated(Activation::ISearchActivatedEventArgs*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnShareTargetActivated(Activation::IShareTargetActivatedEventArgs*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnFileOpenPickerActivated(Activation::IFileOpenPickerActivatedEventArgs*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnFileSavePickerActivated(Activation::IFileSavePickerActivatedEventArgs*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnCachedFileUpdaterActivated(Activation::ICachedFileUpdaterActivatedEventArgs*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnWindowCreated(Xaml::IWindowCreatedEventArgs*) override { return S_OK; }

private:
    void RecordBindingSnapshot(const char* phase)
    {
        ComPtr<ProbeInterface> probe;
        HString direct, bound, known;
        HRESULT directHr = m_probe.As(&probe);
        if (SUCCEEDED(directHr))
            directHr = probe->get_ProbeText(direct.GetAddressOf());
        HRESULT boundHr = m_bound->get_Text(bound.GetAddressOf());
        HRESULT knownHr = m_knownBound->get_Text(known.GetAddressOf());
        ComPtr<Xaml::Data::ICustomPropertyProvider> provider;
        HRESULT providerHr = m_probe.As(&provider);
        ComPtr<Xaml::Data::ICustomProperty> property;
        HRESULT propertyHr = SUCCEEDED(providerHr)
            ? provider->GetCustomProperty(HStringReference(L"ProbeText").Get(), &property)
            : providerHr;
        size_t used = strlen(gDiagnostics);
        sprintf_s(gDiagnostics + used, sizeof(gDiagnostics) - used,
            "%s: direct_hr=0x%08lX direct_match=%d bound_hr=0x%08lX bound_match=%d bound_length=%u known_hr=0x%08lX known_match=%d provider_hr=0x%08lX property_hr=0x%08lX property_present=%d\r\n",
            phase, static_cast<unsigned long>(directHr), HStringEquals(direct.Get(), L"Markup binding value"),
            static_cast<unsigned long>(boundHr), HStringEquals(bound.Get(), L"Markup binding value"),
            WindowsGetStringLen(bound.Get()), static_cast<unsigned long>(knownHr),
            HStringEquals(known.Get(), L"Known binding value"), static_cast<unsigned long>(providerHr),
            static_cast<unsigned long>(propertyHr), property != nullptr);
        XamlProbeRecordStage(-112);
    }

    HRESULT FinishChecks()
    {
        RecordBindingSnapshot("deferred-final");
        const bool markupChecks = RunMarkupChecks(m_probe.Get(), m_probeHost.Get(), m_bound.Get());
        const bool passed = m_lifetimeResult == 0 && markupChecks;
        SetExitCode(passed ? 0 : 1);
        SetStatus(passed ? L"PASS: XAML and lifetime checks completed (v27)" : L"FAIL: XAML or lifetime checks failed (v27)");
        if (markupChecks)
            XamlProbeRecordStage(m_lifetimeResult == 0 ? 0 : 1200 + m_lifetimeResult);
        return S_OK;
    }

    HRESULT SetStatus(const wchar_t* status)
    {
        return m_status ? m_status->put_Text(HStringReference(status).Get()) : E_UNEXPECTED;
    }

    HRESULT SetDetails(const wchar_t* details)
    {
        return m_details ? m_details->put_Text(HStringReference(details).Get()) : E_UNEXPECTED;
    }

    bool RunMarkupChecks(IInspectable* probeObject, Controls::IContentControl* probeHost,
        Controls::ITextBlock* boundText)
    {
        ComPtr<Controls::IControl> control;
        ComPtr<Xaml::IFrameworkElement> framework;
        ComPtr<Xaml::IUIElement> element;
        const bool hasControl = SUCCEEDED(probeObject->QueryInterface(IID_PPV_ARGS(&control)));
        const bool hasFramework = SUCCEEDED(probeObject->QueryInterface(IID_PPV_ARGS(&framework)));
        const bool hasElement = SUCCEEDED(probeObject->QueryInterface(IID_PPV_ARGS(&element)));
        const bool inherited = hasControl && hasFramework && hasElement;

        ComPtr<ProbeInterface> probe;
        HString probeText;
        const bool probeValueMatches = SUCCEEDED(probeObject->QueryInterface(IID_PPV_ARGS(&probe)))
            && SUCCEEDED(probe->get_ProbeText(probeText.GetAddressOf()))
            && HStringEquals(probeText.Get(), L"Markup binding value");
        HString boundValue;
        const bool boundValueMatches = SUCCEEDED(boundText->get_Text(boundValue.GetAddressOf()))
            && HStringEquals(boundValue.Get(), L"Markup binding value");
        const bool binding = probeValueMatches && boundValueMatches;
        const int bindingMask = (probeValueMatches ? 1 : 0) | (boundValueMatches ? 2 : 0);

        bool layout = false;
        if (inherited) {
            ABI::Windows::Foundation::Size available { 320.0f, 240.0f };
            ABI::Windows::Foundation::Rect bounds { 0.0f, 0.0f, 320.0f, 240.0f };
            ABI::Windows::Foundation::Size desired { 0.0f, 0.0f };
            double width = 0.0;
            double height = 0.0;
            layout = SUCCEEDED(element->Measure(available))
                && SUCCEEDED(element->get_DesiredSize(&desired))
                && SUCCEEDED(element->Arrange(bounds))
                && SUCCEEDED(framework->get_Width(&width))
                && SUCCEEDED(framework->get_Height(&height))
                && desired.Width >= 239.0f && desired.Height >= 159.0f
                && width >= 239.0 && height >= 159.0;
        }

        ComPtr<IInspectable> originalContent;
        HRESULT getContentResult = probeHost->get_Content(&originalContent);
        const bool gotContent = SUCCEEDED(getContentResult) && originalContent;
        HRESULT clearContentResult = gotContent ? probeHost->put_Content(nullptr) : E_UNEXPECTED;
        const bool clearedContent = SUCCEEDED(clearContentResult);
        HRESULT restoreContentResult = clearedContent ? probeHost->put_Content(originalContent.Get()) : E_UNEXPECTED;
        const bool reinserted = SUCCEEDED(restoreContentResult);
        const int interfaceMask = (hasControl ? 1 : 0) | (hasFramework ? 2 : 0)
            | (hasElement ? 4 : 0);
        const int markupStage = !inherited ? -400 - interfaceMask
            : bindingMask != 3 ? -500 - bindingMask
            : !layout ? -303
            : !gotContent ? -305
            : !clearedContent ? -306
            : !reinserted ? -304
            : -204;
        XamlProbeRecordHResult(markupStage, restoreContentResult);

        const bool passed = inherited && binding && layout && reinserted;
        size_t used = strlen(gDiagnostics);
        sprintf_s(gDiagnostics + used, sizeof(gDiagnostics) - used,
            "checks: lifetime=%d inherited=%d binding=%d layout=%d reinsert=%d\r\n",
            m_lifetimeResult, inherited, binding, layout, reinserted);
        XamlProbeRecordHResult(markupStage, restoreContentResult);
        wchar_t details[512];
        swprintf_s(details, L"Inherited QI: %ls\nBinding: %ls\nLayout: %ls\nReinsert: %ls\nLifetime: %ls",
            inherited ? L"PASS" : L"FAIL", binding ? L"PASS" : L"FAIL",
            layout ? L"PASS" : L"FAIL", reinserted ? L"PASS" : L"FAIL",
            m_lifetimeResult == 0 ? L"PASS" : L"FAIL");
        SetDetails(details);
        return passed;
    }

    HRESULT OpenWindow()
    {
        XamlProbeRecordStage(-101);
        if (m_window)
            return m_window->Activate();

        HMODULE component = LoadPackagedLibrary(L"XamlProbeComponent.dll", 0);
        if (!component)
            return HRESULT_FROM_WIN32(GetLastError());
        XamlProbeRecordStage(-104);

        ComPtr<Xaml::IWindowStatics> windows;
        HRESULT result = RoGetActivationFactory(
            HStringReference(RuntimeClass_Windows_UI_Xaml_Window).Get(), IID_PPV_ARGS(&windows));
        if (FAILED(result) || FAILED(result = windows->get_Current(&m_window)))
            return result;
        XamlProbeRecordStage(-105);

        ComPtr<Markup::IXamlReaderStatics> reader;
        ComPtr<IInspectable> root;
        result = RoGetActivationFactory(
            HStringReference(RuntimeClass_Windows_UI_Xaml_Markup_XamlReader).Get(), IID_PPV_ARGS(&reader));
        XamlProbeRecordStage(-106);
        const wchar_t* markup = LR"(<Grid xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation" xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml" Background="#EDF3FA" RequestedTheme="Light">
 <StackPanel HorizontalAlignment="Center" VerticalAlignment="Center" Margin="24">
  <TextBlock Text="M10 XAML Probe Lifetime" FontSize="28" HorizontalAlignment="Center" Margin="0,0,0,16"/>
  <ContentControl x:Name="ProbeHost" HorizontalAlignment="Center" HorizontalContentAlignment="Center" VerticalContentAlignment="Center">
  </ContentControl>
  <TextBlock x:Name="BoundText" Text="{Binding Content.ProbeText, ElementName=ProbeHost}" FontSize="20" Margin="0,16,0,0" HorizontalAlignment="Center"/>
  <TextBlock x:Name="KnownSource" Text="Known binding value" Visibility="Collapsed"/>
  <TextBlock x:Name="KnownBound" Text="{Binding Text, ElementName=KnownSource}" Visibility="Collapsed"/>
  <TextBlock x:Name="Status" Text="Running lifetime checks..." FontSize="20" TextWrapping="Wrap" HorizontalAlignment="Center" Margin="0,16,0,0"/>
  <TextBlock x:Name="Details" Text="Preparing XAML checks..." Margin="0,16,0,0" TextWrapping="Wrap" HorizontalAlignment="Center"/>
 </StackPanel>
</Grid>)";
        if (FAILED(result) || FAILED(result = reader->Load(HStringReference(markup).Get(), &root))
            || FAILED(result = root.As(&m_root)))
            return result;
        XamlProbeRecordStage(-107);

        ComPtr<IInspectable> status;
        if (FAILED(result = m_root->FindName(HStringReference(L"Status").Get(), &status))
            || FAILED(result = status.As(&m_status)))
            return result;

        ComPtr<IInspectable> details;
        if (FAILED(result = m_root->FindName(HStringReference(L"Details").Get(), &details))
            || FAILED(result = details.As(&m_details)))
            return result;

        ComPtr<IInspectable> probeHostObject;
        ComPtr<Controls::IContentControl> probeHost;
        if (FAILED(result = m_root->FindName(HStringReference(L"ProbeHost").Get(), &probeHostObject))
            || FAILED(result = probeHostObject.As(&probeHost)))
            return result;

        ComPtr<IInspectable> boundTextObject;
        ComPtr<Controls::ITextBlock> boundText;
        if (FAILED(result = m_root->FindName(HStringReference(L"BoundText").Get(), &boundTextObject))
            || FAILED(result = boundTextObject.As(&boundText)))
            return result;
        XamlProbeRecordStage(-108);

        ComPtr<IInspectable> probeObject;
        if (FAILED(result = ActivateProbe(component, &probeObject)))
            return result;
        HStringReference probeText(L"Markup binding value");
        ComPtr<ProbeInterface> probeInterface;
        if (FAILED(result = probeObject.As(&probeInterface))
            || FAILED(result = probeInterface->put_ProbeText(probeText.Get()))
            || FAILED(result = probeHost->put_Content(probeObject.Get())))
            return result;
        XamlProbeRecordStage(-109);

        m_probe = probeObject;
        m_probeHost = probeHost;
        m_bound = boundText;
        ComPtr<IInspectable> knownBound;
        if (FAILED(result = m_root->FindName(HStringReference(L"KnownBound").Get(), &knownBound))
            || FAILED(result = knownBound.As(&m_knownBound)))
            return result;
        RecordBindingSnapshot("before-activate");
        ComPtr<ProbeApplication> self(this);
        auto loaded = Callback<Xaml::IRoutedEventHandler>(
            [self](IInspectable*, Xaml::IRoutedEventArgs*) -> HRESULT {
                self->m_root->remove_Loaded(self->m_loadedToken);
                self->RecordBindingSnapshot("loaded");
                ComPtr<ABI::Windows::UI::Core::ICoreDispatcher> dispatcher;
                HRESULT hr = self->m_window->get_Dispatcher(&dispatcher);
                if (FAILED(hr))
                    return hr;
                auto callback = Callback<ABI::Windows::UI::Core::IDispatchedHandler>(
                    [self]() -> HRESULT { return self->FinishChecks(); });
                ComPtr<ABI::Windows::Foundation::IAsyncAction> action;
                hr = dispatcher->RunAsync(ABI::Windows::UI::Core::CoreDispatcherPriority_Low,
                    callback.Get(), &action);
                if (FAILED(hr)) {
                    size_t used = strlen(gDiagnostics);
                    sprintf_s(gDiagnostics + used, sizeof(gDiagnostics) - used,
                        "dispatcher_hr=0x%08lX; validating at Loaded instead\r\n", static_cast<unsigned long>(hr));
                    return self->FinishChecks();
                }
                return hr;
            });
        if (FAILED(result = m_root->add_Loaded(loaded.Get(), &m_loadedToken)))
            return result;

        ComPtr<Xaml::IUIElement> rootElement;
        if (FAILED(result = m_root.As(&rootElement))
            || FAILED(result = m_window->put_Content(rootElement.Get()))
            || FAILED(result = m_window->Activate()))
            return result;
        XamlProbeRecordStage(-110);

        XamlProbeRecordStage(-102);
        m_lifetimeResult = RunXamlProbeLifetimeChecks(component, "XamlProbeComponent.dll");
        XamlProbeRecordStage(-111);
        SetStatus(L"Waiting for loaded binding checks (v27)...");
        return S_OK;
    }

    ComPtr<Xaml::IWindow> m_window;
    ComPtr<Xaml::IFrameworkElement> m_root;
    ComPtr<Controls::ITextBlock> m_status;
    ComPtr<Controls::ITextBlock> m_details;
    ComPtr<IInspectable> m_probe;
    ComPtr<Controls::IContentControl> m_probeHost;
    ComPtr<Controls::ITextBlock> m_bound;
    ComPtr<Controls::ITextBlock> m_knownBound;
    EventRegistrationToken m_loadedToken {};
    int m_lifetimeResult = 1;
};

} // namespace

extern "C" void __stdcall WKProbeLifetimeStartup()
{
    const HRESULT apartment = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(apartment)) {
        ExitProcess(1);
        return;
    }

    ComPtr<Xaml::IApplicationStatics> application;
    HRESULT result = RoGetActivationFactory(
        HStringReference(RuntimeClass_Windows_UI_Xaml_Application).Get(),
        IID_PPV_ARGS(&application));
    if (SUCCEEDED(result))
        WriteResultToLocalState(-100);
    if (SUCCEEDED(result)) {
        auto initialize = Callback<Xaml::IApplicationInitializationCallback>(
            [](Xaml::IApplicationInitializationCallbackParams*) -> HRESULT {
                ComPtr<ProbeApplication> app;
                HRESULT callbackResult = MakeAndInitialize<ProbeApplication>(&app);
                if (SUCCEEDED(callbackResult))
                    app.Detach();
                return callbackResult;
            });
        result = application->Start(initialize.Get());
    }

    if (FAILED(result))
        SetExitCode(1);
    WriteResultToLocalState(gExitCode);
    RoUninitialize();
    ExitProcess(static_cast<UINT>(gExitCode));
}
