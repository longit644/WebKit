#pragma once

#include <windows.h>

namespace WebKit {

// Private owner-thread dispatcher hop for platform-service callbacks. Internal
// only: never constructed from XAML, so it contributes no WinMD surface.
ref class DispatcherUWP sealed {
internal:
    // Retain the owning dispatcher; a null dispatcher cannot enqueue anything.
    DispatcherUWP(Windows::UI::Core::CoreDispatcher^ dispatcher);
    // Report whether the caller runs on the owning dispatcher thread.
    bool HasThreadAccess();
    // Enqueue one handler on the owner dispatcher, preserving the origin HRESULT.
    HRESULT Post(Windows::UI::Core::DispatchedHandler^ handler);

private:
    // Owning dispatcher retained for process lifetime by the service owner.
    Windows::UI::Core::CoreDispatcher^ m_dispatcher;
};

} // namespace WebKit
