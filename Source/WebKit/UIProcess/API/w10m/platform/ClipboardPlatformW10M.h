#pragma once

#include "ClipboardServiceW10M.h"
#include "DispatcherW10M.h"

namespace WebKit {

// Create the WinRT text transport; the service marshals every read completion.
std::unique_ptr<IClipboardTransportW10M> CreateClipboardPlatformTransport();

// Create the CoreDispatcher hop used for service completion delivery.
std::unique_ptr<IServiceDispatcherW10M> CreatePlatformServiceDispatcher(
    DispatcherW10M^ dispatcher);

// Fill a borrowed platform-services table from a caller-owned service (see 7.5).
HRESULT BuildClipboardPlatformServices(
    ClipboardServiceW10M* service, WKW10MPlatformServices* table);

} // namespace WebKit

