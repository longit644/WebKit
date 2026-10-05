#include "Diagnostics.h"
#include "ProbePage.g.hpp"
#include "pch.h"
#include <cstdio>
#include <windows.ui.xaml.controls.h>
#include <windows.ui.xaml.h>
#include <wrl.h>

namespace CXCompileProbe {

// Binding is asynchronous relative to page construction, so validate at Loaded.
ProbePage::ProbePage()
{
    InitializeComponent();
    m_loadedToken = Loaded += ref new Windows::UI::Xaml::RoutedEventHandler(
        [this](Platform::Object ^, Windows::UI::Xaml::RoutedEventArgs ^) {
            Loaded -= m_loadedToken;
            try {
                RunChecks();
            } catch (Platform::Exception ^ exception) {
                Status->Text = L"COMPILED XAML FAIL: check exception";
                RecordFailure(exception->HResult, exception->Message);
            }
        });
}

// Measure the actual compiled control and observe final release with weak references.
void ProbePage::RunChecks()
{
    using Microsoft::WRL::ComPtr;
    auto object = reinterpret_cast<IInspectable*>(Probe);
    ComPtr<ABI::Windows::UI::Xaml::Controls::IControl> control;
    ComPtr<ABI::Windows::UI::Xaml::IFrameworkElement> framework;
    ComPtr<ABI::Windows::UI::Xaml::IUIElement> element;
    bool inherited = SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&control)))
        && SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&framework)))
        && SUCCEEDED(object->QueryInterface(IID_PPV_ARGS(&element)));
    bool identity = Probe->ProbeId == 1;
    bool property = Probe->ProbeText == L"Compiled binding value";
    bool binding = BoundText->Text == L"Compiled binding value";
    bool layout = false;
    if (inherited) {
        ABI::Windows::Foundation::Size available { 320.0f, 240.0f };
        ABI::Windows::Foundation::Rect bounds { 0.0f, 0.0f, 320.0f, 240.0f };
        ABI::Windows::Foundation::Size desired {};
        double width = 0, height = 0;
        layout = SUCCEEDED(element->Measure(available))
            && SUCCEEDED(element->get_DesiredSize(&desired))
            && SUCCEEDED(element->Arrange(bounds))
            && SUCCEEDED(framework->get_ActualWidth(&width))
            && SUCCEEDED(framework->get_ActualHeight(&height))
            && desired.Width >= 240.0f && desired.Height >= 160.0f
            && width >= 240.0 && height >= 160.0;
    }
    auto original = ProbeHost->Content;
    ProbeHost->Content = nullptr;
    bool removed = ProbeHost->Content == nullptr;
    ProbeHost->Content = original;
    bool reinsert = removed && ProbeHost->Content == original;

    // Unattached controls must disappear once the last caller reference is dropped.
    constexpr int lifetimeIterations = 32;
    int destroyed = 0;
    for (int index = 0; index < lifetimeIterations; ++index) {
        Platform::WeakReference weak;
        {
            auto temporary = ref new WebKitProbe::XamlProbe();
            weak = temporary;
        }
        if (!weak.Resolve<Platform::Object>())
            ++destroyed;
    }
    bool lifetime = destroyed == lifetimeIterations;
    bool passed = inherited && identity && property && binding && layout && reinsert && lifetime;
    char text[1024];
    sprintf_s(text, "version=1.0.0.6\r\nresult=%s\r\nidentity=%d\r\nproperty=%d\r\nbinding=%d\r\ninherited=%d\r\nlayout=%d\r\nreinsert=%d\r\ndestroyed=%d/%d\r\n",
        passed ? "PASS" : "FAIL", identity, property, binding, inherited, layout, reinsert, destroyed, lifetimeIterations);
    RecordResult(text);
    Status->Text = passed ? L"COMPILED XAML PASS (v6)" : L"COMPILED XAML FAIL (v6)";
    wchar_t details[512];
    swprintf_s(details, L"Identity/property: %ls\nBinding: %ls\nInherited QI: %ls\nLayout: %ls\nReinsert: %ls\nFinal release: %d/%d",
        identity && property ? L"PASS" : L"FAIL", binding ? L"PASS" : L"FAIL", inherited ? L"PASS" : L"FAIL",
        layout ? L"PASS" : L"FAIL", reinsert ? L"PASS" : L"FAIL", destroyed, lifetimeIterations);
    Details->Text = ref new Platform::String(details);
}

}
