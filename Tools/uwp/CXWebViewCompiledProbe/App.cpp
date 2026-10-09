#include "App.g.hpp"
#include "Diagnostics.h"
#include "pch.h"

namespace WebKitCompiledProbe {

// Install the generated XAML provider/resources before page construction.
App::App() { InitializeComponent(); }

// Load the compiled page, preserving an explicit fail-closed construction report.
void App::OnLaunched(Windows::ApplicationModel::Activation::LaunchActivatedEventArgs^ args)
{
    (void)args;
    auto window = Windows::UI::Xaml::Window::Current;
    SaveContractReport("version=" COMPILED_PROBE_VERSION "\r\nstage=constructing-compiled-page\r\n");
    try { window->Content = ref new ContractPage(); }
    catch (Platform::Exception^ exception) {
        SaveContractFailure(exception->HResult, exception->Message);
        auto failure = ref new Windows::UI::Xaml::Controls::TextBlock();
        failure->Text = L"WEBKIT COMPILED FAIL " COMPILED_PROBE_VERSION_W L"\n" + exception->Message;
        failure->TextWrapping = Windows::UI::Xaml::TextWrapping::Wrap;
        window->Content = failure;
    }
    window->Activate();
}

} // namespace WebKitCompiledProbe
