#pragma once
#include "App.g.h"

namespace WebKitCompiledProbe {

// Own the SDK-generated metadata provider for compiled declarative WebKit activation.
[Windows::Foundation::Metadata::WebHostHidden]
public ref class App sealed {
public:
    // Install compiled application resources/type provider before constructing the page.
    App();
protected:
    // Show construction failures with their originating HRESULT and versioned report.
    virtual void OnLaunched(Windows::ApplicationModel::Activation::LaunchActivatedEventArgs^ args) override;
};

} // namespace WebKitCompiledProbe
