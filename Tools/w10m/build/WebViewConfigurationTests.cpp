#include "WebKitWebViewOptions.h"

#include <activation.h>
#include <cstdio>
#include <limits>
#include <roapi.h>

using namespace WebKit;
// Configuration assertions evaluated in this isolated fixture run.
static unsigned s_checks;
// Failed assertions; any nonzero count fails the process.
static unsigned s_failures;

// Print and aggregate a configuration assertion without masking earlier failures.
static void check(bool condition, const char* description)
{
    ++s_checks;
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", description);
    if (!condition)
        ++s_failures;
}

// Convert the supplied ASCII hexadecimal oracle into native byte-string form.
static std::string narrow(const wchar_t* text)
{
    std::string output;
    while (*text)
        output.push_back(static_cast<char>(*text++));
    return output;
}

// Mutate only a temporary fixture file to verify retained-byte immutability.
static bool overwrite(const std::wstring& path, const std::string& bytes)
{
    auto file = CreateFile2(path.c_str(), GENERIC_WRITE, 0, CREATE_ALWAYS, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD written = 0;
    auto success = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size();
    CloseHandle(file);
    return success;
}

#pragma warning(push)
#pragma warning(disable : 4447)
// Verify copied values, paths, ranges, assets, fingerprints and stable failure outputs.
int wmain(int argc, wchar_t** argv)
{
    if (argc != 4)
        return 2;
    auto initialized = RoInitialize(RO_INIT_MULTITHREADED);
    if (FAILED(initialized))
        return 3;
    std::wstring package(argv[1]);
    auto expectedTrust = narrow(argv[2]);
    auto expectedFonts = narrow(argv[3]);
    auto publicOptions = ref new WebKitWebViewOptions();
    auto copied = WebKitWebViewOptions::CopyToNative(publicOptions);
    WebViewConfigurationW10M configuration(copied);
    publicOptions->CertificateAuthorityPath = L"missing.pem";
    publicOptions->FontDirectoryPath = L"missing-fonts";
    publicOptions->MaximumConcurrentRequests = 0;
    publicOptions->MaximumResourceBytes = 0;
    publicOptions->MaximumBufferedBytes = 0;
    publicOptions->RequestTimeoutMilliseconds = 0;
    copied.maximumResourceBytes = 0;
    std::unique_ptr<const ValidatedWebViewOptionsW10M> valid;
    auto result = configuration.validate(package, valid);
    check(result == S_OK && valid, "all six options are copied independently of later public/native input mutation");
    if (!valid)
        return 1;
    check(valid->options().maximumConcurrentRequests == 8 && valid->options().maximumResourceBytes == 32ULL * 1024 * 1024
            && valid->options().maximumBufferedBytes == 16ULL * 1024 * 1024 && valid->options().requestTimeoutMilliseconds == 30000,
        "default limits retained in immutable validated copy");
    check(valid->trustFingerprint() == expectedTrust, "retained PEM SHA256 agrees with independent file oracle");
    check(valid->fontFingerprint() == expectedFonts, "sorted font name/content SHA256 agrees with independent oracle");
    auto nullPaths = ref new WebKitWebViewOptions();
    nullPaths->CertificateAuthorityPath = nullptr;
    nullPaths->FontDirectoryPath = nullptr;
    std::unique_ptr<const ValidatedWebViewOptionsW10M> defaults;
    check(WebViewConfigurationW10M(WebKitWebViewOptions::CopyToNative(nullPaths)).validate(package, defaults) == S_OK
            && defaults->options().certificateAuthorityPath == valid->options().certificateAuthorityPath,
        "null option paths also select containing-package defaults");
    try {
        WebKitWebViewOptions::CopyToNative(nullptr);
        check(false, "null options object rejected before page creation");
    } catch (Platform::Exception^ exception) {
        check(exception->HResult == E_INVALIDARG, "null options object rejected before page creation");
    }
    auto page = valid->pageOptions();
    check(page.header.structureSize == sizeof(page) && page.header.structureVersion == WKW10M_STRUCTURE_VERSION
            && page.maximumQueuedRequests == 128 && page.maximumRedirects == 10,
        "frozen page ABI views include fixed queue/redirect limits");
    check(page.caBundle.data == reinterpret_cast<const uint8_t*>(valid->caBundle().data()) && page.caBundle.length == valid->caBundle().size()
            && page.trustFingerprint.data == reinterpret_cast<const uint8_t*>(valid->trustFingerprint().data()),
        "page ABI borrows owned immutable bundle bytes and hash");
    auto engine = valid->engineOptions(nullptr);
    check(engine.header.structureSize == sizeof(engine) && engine.fontDirectory.length == valid->options().fontDirectoryPath.size()
            && engine.fontFingerprint.length == 64 && !engine.platformServices,
        "engine ABI view supplies canonical path/fingerprint without projections");
    WebViewOptionsW10M relative;
    relative.certificateAuthorityPath = L".\\cacert.pem";
    relative.fontDirectoryPath = L".\\Fonts\\";
    std::unique_ptr<const ValidatedWebViewOptionsW10M> normalized;
    result = WebViewConfigurationW10M(relative).validate(package, normalized);
    check(result == S_OK && normalized->options().certificateAuthorityPath == valid->options().certificateAuthorityPath
            && normalized->options().fontDirectoryPath == valid->options().fontDirectoryPath,
        "relative paths resolve against package and canonicalize dot segments/separators");
    relative.certificateAuthorityPath = L"CACERT.PEM";
    relative.fontDirectoryPath = L"FONTS";
    result = WebViewConfigurationW10M(relative).validate(package, normalized);
    check(result == S_OK && normalized->options().fontDirectoryPath == valid->options().fontDirectoryPath,
        "case-equivalent package paths share canonical configuration");

    auto reject = [&](WebViewOptionsW10M options, HRESULT expected, const char* description) {
        auto previous = valid.get();
        auto rejected = WebViewConfigurationW10M(options).validate(package, valid);
        check(rejected == expected && valid.get() == previous, description);
    };
    for (auto value : { 0U, 17U, std::numeric_limits<uint32_t>::max() }) {
        WebViewOptionsW10M options;
        options.maximumConcurrentRequests = value;
        reject(options, E_INVALIDARG, "concurrency outside 1..16 rejected with untouched output");
    }
    for (auto value : { 65535ULL, 64ULL * 1024 * 1024 + 1, std::numeric_limits<uint64_t>::max() }) {
        WebViewOptionsW10M options;
        options.maximumResourceBytes = value;
        reject(options, E_INVALIDARG, "resource limit outside 64KiB..64MiB rejected");
    }
    for (auto value : { 1024ULL * 1024 - 1, 32ULL * 1024 * 1024 + 1, std::numeric_limits<uint64_t>::max() }) {
        WebViewOptionsW10M options;
        options.maximumBufferedBytes = value;
        reject(options, E_INVALIDARG, "buffer limit outside 1MiB..32MiB rejected");
    }
    for (auto value : { 999U, 120001U, std::numeric_limits<uint32_t>::max() }) {
        WebViewOptionsW10M options;
        options.requestTimeoutMilliseconds = value;
        reject(options, E_INVALIDARG, "timeout outside 1000..120000 rejected");
    }
    for (bool maximum : { false, true }) {
        WebViewOptionsW10M options;
        options.maximumConcurrentRequests = maximum ? 16 : 1;
        options.maximumResourceBytes = maximum ? 64ULL * 1024 * 1024 : 65536;
        options.maximumBufferedBytes = maximum ? 32ULL * 1024 * 1024 : 1024 * 1024;
        options.requestTimeoutMilliseconds = maximum ? 120000 : 1000;
        check(WebViewConfigurationW10M(options).validate(package, normalized) == S_OK, "inclusive range endpoints accepted");
    }
    WebViewOptionsW10M options;
    options.certificateAuthorityPath = L"missing.pem";
    reject(options, HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), "missing CA file preserves originating error before page creation");
    options.certificateAuthorityPath = L"empty.pem";
    reject(options, HRESULT_FROM_WIN32(ERROR_INVALID_DATA), "empty CA fails closed");
    options.certificateAuthorityPath = L"malformed.pem";
    reject(options, HRESULT_FROM_WIN32(ERROR_INVALID_DATA), "malformed PEM fails closed");
    options.certificateAuthorityPath = L"not-certificates.pem";
    reject(options, HRESULT_FROM_WIN32(ERROR_INVALID_DATA), "noncertificate bytes fail closed");
    options.certificateAuthorityPath = L"nul.pem";
    reject(options, HRESULT_FROM_WIN32(ERROR_INVALID_DATA), "embedded-NUL bundle bytes fail closed");
    options.certificateAuthorityPath = L"";
    options.fontDirectoryPath = L"missing-fonts";
    reject(options, HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), "missing font directory preserves originating error");
    options.fontDirectoryPath = L"FontsIncomplete";
    reject(options, HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND), "missing required bold face rejected before page creation");
    options.fontDirectoryPath = L"cacert.pem";
    reject(options, HRESULT_FROM_WIN32(ERROR_DIRECTORY), "font path must be a directory");
    options.fontDirectoryPath = L"";
    options.certificateAuthorityPath = L"cacert.pem";
    options.certificateAuthorityPath.push_back(L'\0');
    options.certificateAuthorityPath += L"hidden";
    reject(options, E_INVALIDARG, "embedded-NUL paths cannot truncate validation");
    options.certificateAuthorityPath = L"https://example.test/ca.pem";
    reject(options, E_INVALIDARG, "CA option is a filesystem path rather than network URL");
    options.certificateAuthorityPath = L"cacert.pem";
    auto before = valid.get();
    check(WebViewConfigurationW10M(options).validate(L"relative-package", valid) == E_INVALIDARG && valid.get() == before,
        "relative package root rejected instead of consulting working directory");
    options = { };
    options.certificateAuthorityPath = valid->options().certificateAuthorityPath;
    options.fontDirectoryPath = valid->options().fontDirectoryPath;
    check(WebViewConfigurationW10M(options).validate(L"", normalized) == S_OK, "explicit absolute paths need no default-package resolution");
    auto original = valid->caBundle();
    check(overwrite(valid->options().certificateAuthorityPath, "changed invalid bundle"), "mutate only the temporary fixture CA path");
    check(valid->caBundle() == original && valid->trustFingerprint() == expectedTrust, "validated trust remains immutable after filesystem mutation");
    reject(options, HRESULT_FROM_WIN32(ERROR_INVALID_DATA), "failed revalidation cannot replace an existing validated snapshot");
    check(overwrite(valid->options().certificateAuthorityPath, original), "restore temporary fixture CA bytes");
    check(WebViewConfigurationW10M(options).validate(L"", normalized) == S_OK && normalized->trustFingerprint() == expectedTrust,
        "new successful validation recovers same frozen option values");
    std::printf("M10 configuration: %u checks, %u failures\n", s_checks, s_failures);
    RoUninitialize();
    return s_failures ? 1 : 0;
}
#pragma warning(pop)
