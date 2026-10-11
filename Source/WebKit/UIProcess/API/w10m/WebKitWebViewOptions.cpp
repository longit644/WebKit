#include "WebKitWebViewOptions.h"

namespace WebKit {

// Install the public package-default paths and exact resource-policy defaults.
WebKitWebViewOptions::WebKitWebViewOptions()
{
    CertificateAuthorityPath = L"";
    FontDirectoryPath = L"";
    MaximumConcurrentRequests = 8;
    MaximumResourceBytes = 32ULL * 1024 * 1024;
    MaximumBufferedBytes = 16ULL * 1024 * 1024;
    RequestTimeoutMilliseconds = 30000;
}

// Copy an application's bag without retaining mutable host configuration.
WebKitWebViewOptions::WebKitWebViewOptions(WebKitWebViewOptions^ options)
{
    if (!options)
        throw ref new Platform::InvalidArgumentException(L"Options must not be null");
    CertificateAuthorityPath = options->CertificateAuthorityPath;
    FontDirectoryPath = options->FontDirectoryPath;
    MaximumConcurrentRequests = options->MaximumConcurrentRequests;
    MaximumResourceBytes = options->MaximumResourceBytes;
    MaximumBufferedBytes = options->MaximumBufferedBytes;
    RequestTimeoutMilliseconds = options->RequestTimeoutMilliseconds;
}

// Copy strings/scalars for immutable native validation; reject a null bag.
WebViewOptionsW10M WebKitWebViewOptions::CopyToNative(WebKitWebViewOptions^ options)
{
    if (!options)
        throw ref new Platform::InvalidArgumentException(L"Options must not be null");
    // Preserve explicit lengths so embedded NULs are rejected by path validation.
    auto copy = [](Platform::String^ value) {
        return value ? std::wstring(value->Data(), value->Length()) : std::wstring();
    };
    return { copy(options->CertificateAuthorityPath), copy(options->FontDirectoryPath), options->MaximumConcurrentRequests,
        options->MaximumResourceBytes, options->MaximumBufferedBytes, options->RequestTimeoutMilliseconds };
}

} // namespace WebKit


