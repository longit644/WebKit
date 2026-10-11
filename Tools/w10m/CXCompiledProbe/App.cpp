#include "App.g.hpp"
#include "Diagnostics.h"
#include "pch.h"

namespace CXCompileProbe {

// Generated InitializeComponent installs the application's metadata provider.
App::App()
{
    InitializeComponent();
}

// The compiled page, rather than a manually returned visual, owns the probe.
void App::OnLaunched(Windows::ApplicationModel::Activation::LaunchActivatedEventArgs ^)
{
    auto window = Windows::UI::Xaml::Window::Current;
    RecordResult("stage=constructing-page\r\n");
    try {
        window->Content = ref new ProbePage();
    } catch (Platform::Exception ^ exception) {
        RecordFailure(exception->HResult, exception->Message);
        auto error = ref new Windows::UI::Xaml::Controls::TextBlock();
        error->Text = L"COMPILED XAML FAIL: page construction\n" + exception->Message;
        error->TextWrapping = Windows::UI::Xaml::TextWrapping::Wrap;
        window->Content = error;
    }
    window->Activate();
}

}
