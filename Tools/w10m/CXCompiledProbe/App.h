#pragma once
#include "App.g.h"

namespace CXCompileProbe {

// Owns the generated XAML metadata provider used for declarative activation.
[Windows::Foundation::Metadata::WebHostHidden] public ref class App sealed {
public:
    // Initializes the generated provider before any page is constructed.
    App();

protected:
    // Keeps construction errors visible and preserves their actual HRESULT.
    virtual void OnLaunched(Windows::ApplicationModel::Activation::LaunchActivatedEventArgs ^ args) override;
};

}
