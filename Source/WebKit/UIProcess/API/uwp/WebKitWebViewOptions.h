#pragma once

#include "native/WebViewConfigurationUWP.h"

namespace WebKit {

// An application-owned configuration bag. The control copies it at construction.
public ref class WebKitWebViewOptions sealed {
public:
    // Create an application-owned bag with the exact M10 path/limit defaults.
    WebKitWebViewOptions();

    // CA PEM path; empty/null selects the containing package's cacert.pem.
    property Platform::String^ CertificateAuthorityPath;
    // Font directory; empty/null selects the containing package's Fonts folder.
    property Platform::String^ FontDirectoryPath;
    // Active transfers per session; copied validation accepts 1..16.
    property unsigned int MaximumConcurrentRequests;
    // Decoded response-body bytes per resource; accepts 64 KiB..64 MiB.
    property unsigned long long MaximumResourceBytes;
    // Transient undelivered session bytes; accepts 1 MiB..32 MiB.
    property unsigned long long MaximumBufferedBytes;
    // Active request deadline including queue/redirect time; accepts 1000..120000 ms.
    property unsigned int RequestTimeoutMilliseconds;

internal:
    // Strings are immutable WinRT values; all six option values are copied.
    WebKitWebViewOptions(WebKitWebViewOptions^);
    // Copy all six values into component-owned native storage with explicit string lengths.
    static WebViewOptionsUWP CopyToNative(WebKitWebViewOptions^);
};

} // namespace WebKit
