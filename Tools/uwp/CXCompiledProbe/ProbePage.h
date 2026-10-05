#pragma once
#include "ProbePage.g.h"

namespace CXCompileProbe {

// Validates the compiled outer control after initial XAML binding has run.
[Windows::Foundation::Metadata::WebHostHidden] public ref class ProbePage sealed {
public:
    // Registers a one-shot Loaded handler without retaining a page/handler cycle.
    ProbePage();

private:
    // Reports each acceptance check independently to LocalState and the screen.
    void RunChecks();

    // Removed on the first Loaded notification, before content reparenting.
    Windows::Foundation::EventRegistrationToken m_loadedToken;
};

}
