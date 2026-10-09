#pragma once
#include "ContractPage.g.h"

namespace WebKitCompiledProbe {

// Validate the real public control constructed by compiled markup and generated type data.
[Windows::Foundation::Metadata::WebHostHidden]
public ref class ContractPage sealed {
public:
    // Construct XBF controls and register a weak one-shot Loaded assertion callback.
    ContractPage();
private:
    // Assert compiled identity/properties/binding/layout and public lifetime contracts.
    void RunChecks();
    // Revoke the Loaded subscription before reparenting or assertion dispatch.
    Windows::Foundation::EventRegistrationToken m_loaded;
    // Capture the URI conversion/setter result before lazy initialization runs asynchronously.
    bool m_markupSourceAssigned { false };
};

} // namespace WebKitCompiledProbe
