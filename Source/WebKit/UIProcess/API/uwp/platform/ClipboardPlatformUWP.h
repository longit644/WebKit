#pragma once

#include "ClipboardServiceUWP.h"
#include "DispatcherUWP.h"

namespace WebKit {

// Create the WinRT text transport; the service marshals every read completion.
std::unique_ptr<IClipboardTransportUWP> CreateClipboardPlatformTransport();

// Create the CoreDispatcher hop used for service completion delivery.
std::unique_ptr<IServiceDispatcherUWP> CreatePlatformServiceDispatcher(
    DispatcherUWP^ dispatcher);

// Fill a borrowed platform-services table from a caller-owned service (see 7.5).
HRESULT BuildClipboardPlatformServices(
    ClipboardServiceUWP* service, WKUWPPlatformServices* table);

} // namespace WebKit
