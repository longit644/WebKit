#include "WebViewConfigurationUWP.h"

#include <algorithm>
#include <bcrypt.h>
#include <limits>
#include <new>
#include <utility>
#include <vector>
#include <wincrypt.h>

namespace WebKit {
namespace {

// Binary kilobyte used by the public resource-range contract.
constexpr uint64_t kibibyte = 1024;
// Binary megabyte used by the public resource-range contract.
constexpr uint64_t mebibyte = 1024 * kibibyte;

// Scoped readable-file ownership; invalid handles are never closed.
struct FileHandle {
    // Native handle owned by this scope.
    HANDLE value { INVALID_HANDLE_VALUE };
    // Release the opened file on every success/error/exception path.
    ~FileHandle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};

// Scoped ownership of a font-directory enumeration.
struct FindHandle {
    // Search handle returned by FindFirstFileExW.
    HANDLE value { INVALID_HANDLE_VALUE };
    // End an active directory enumeration exactly once.
    ~FindHandle() { if (value != INVALID_HANDLE_VALUE) FindClose(value); }
};

// Scoped BCrypt SHA256 algorithm-provider ownership.
struct AlgorithmHandle {
    // Open algorithm provider, or null before successful setup.
    BCRYPT_ALG_HANDLE value { nullptr };
    // Release the provider after its hash/storage owners are destroyed.
    ~AlgorithmHandle() { if (value) BCryptCloseAlgorithmProvider(value, 0); }
};

// Scoped ownership of one incremental BCrypt digest.
struct HashHandle {
    // Created hash context, or null before successful setup.
    BCRYPT_HASH_HANDLE value { nullptr };
    // Destroy the hash before its backing object storage is released.
    ~HashHandle() { if (value) BCryptDestroyHash(value); }
};

// Resolve a requested/default path against the package and normalize Windows spelling.
HRESULT resolvePath(const std::wstring& requested, const std::wstring& package, const wchar_t* fallback, std::wstring& output)
{
    if (requested.find(L'\0') != std::wstring::npos || package.find(L'\0') != std::wstring::npos)
        return E_INVALIDARG;
    // Admit absolute drive/UNC paths; never infer a relative path from process CWD.
    auto absolute = [](const std::wstring& path) {
        return (path.size() >= 3 && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/'))
            || (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\');
    };
    std::wstring path = requested.empty() ? fallback : requested;
    if (!absolute(path)) {
        if (!absolute(package) || path.find(L':') != std::wstring::npos || path[0] == L'\\' || path[0] == L'/')
            return E_INVALIDARG;
        path = package + L"\\" + path;
    }
    std::replace(path.begin(), path.end(), L'/', L'\\');
    auto required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!required)
        return HRESULT_FROM_WIN32(GetLastError());
    std::vector<wchar_t> buffer(required);
    auto length = GetFullPathNameW(path.c_str(), required, buffer.data(), nullptr);
    if (!length || length >= required)
        return HRESULT_FROM_WIN32(length ? ERROR_INSUFFICIENT_BUFFER : GetLastError());
    // Windows package paths are case insensitive. Normalize case as well as
    // dot segments/separators so identical caller spelling joins font setup.
    auto mapped = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, buffer.data(), static_cast<int>(length), nullptr, 0, nullptr, nullptr, 0);
    if (!mapped)
        return HRESULT_FROM_WIN32(GetLastError());
    output.resize(mapped);
    if (!LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, buffer.data(), static_cast<int>(length), &output[0], mapped, nullptr, nullptr, 0))
        return HRESULT_FROM_WIN32(GetLastError());
    while (output.size() > 3 && output.back() == L'\\')
        output.pop_back();
    return S_OK;
}

// Read a nonempty file under a write/delete-excluding handle; preserve OS errors.
HRESULT readFile(const std::wstring& path, std::string& bytes)
{
    FileHandle file;
    // Excluding write/delete sharing prevents mutation during this read.
    file.value = CreateFile2(path.c_str(), GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, nullptr);
    if (file.value == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());
    LARGE_INTEGER size;
    if (!GetFileSizeEx(file.value, &size))
        return HRESULT_FROM_WIN32(GetLastError());
    if (size.QuadPart <= 0)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    if (static_cast<uint64_t>(size.QuadPart) > std::numeric_limits<uint32_t>::max())
        return HRESULT_FROM_WIN32(ERROR_FILE_TOO_LARGE);
    bytes.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    if (!ReadFile(file.value, &bytes[0], static_cast<DWORD>(bytes.size()), &read, nullptr))
        return HRESULT_FROM_WIN32(GetLastError());
    return read == bytes.size() ? S_OK : HRESULT_FROM_WIN32(ERROR_HANDLE_EOF);
}

// Parse every PEM certificate without consulting native trust or issuing requests.
HRESULT validateCertificates(const std::string& pem)
{
    if (pem.find('\0') != std::string::npos)
        return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
    // PEM boundary that introduces an X509 certificate block.
    constexpr char begin[] = "-----BEGIN CERTIFICATE-----";
    // PEM boundary that terminates the same certificate block.
    constexpr char end[] = "-----END CERTIFICATE-----";
    size_t cursor = 0;
    bool found = false;
    while ((cursor = pem.find(begin, cursor)) != std::string::npos) {
        auto finish = pem.find(end, cursor + sizeof(begin) - 1);
        if (finish == std::string::npos)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        finish += sizeof(end) - 1;
        auto size = static_cast<DWORD>(finish - cursor);
        DWORD decodedSize = 0;
        if (!CryptStringToBinaryA(pem.data() + cursor, size, CRYPT_STRING_BASE64HEADER, nullptr, &decodedSize, nullptr, nullptr))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        std::vector<BYTE> decoded(decodedSize);
        if (!CryptStringToBinaryA(pem.data() + cursor, size, CRYPT_STRING_BASE64HEADER, decoded.data(), &decodedSize, nullptr, nullptr))
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        auto certificate = CertCreateCertificateContext(X509_ASN_ENCODING, decoded.data(), decodedSize);
        if (!certificate)
            return HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
        CertFreeCertificateContext(certificate);
        found = true;
        cursor = finish;
    }
    return found ? S_OK : HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
}

// Incremental SHA256 also handles a font directory without concatenating all
// font files into another large buffer.
class Fingerprint {
public:
    // Create an incremental SHA256 context and retain its required object storage.
    HRESULT initialize()
    {
        auto status = BCryptOpenAlgorithmProvider(&m_algorithm.value, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
        if (status < 0)
            return HRESULT_FROM_NT(status);
        DWORD size = 0;
        DWORD read = 0;
        status = BCryptGetProperty(m_algorithm.value, BCRYPT_OBJECT_LENGTH, reinterpret_cast<BYTE*>(&size), sizeof(size), &read, 0);
        if (status < 0)
            return HRESULT_FROM_NT(status);
        m_storage.resize(size);
        status = BCryptCreateHash(m_algorithm.value, &m_hash.value, m_storage.data(), size, nullptr, 0, 0);
        return status < 0 ? HRESULT_FROM_NT(status) : S_OK;
    }

    // Hash one bounded byte span without assembling an extra concatenated buffer.
    HRESULT add(const std::string& bytes)
    {
        auto status = BCryptHashData(m_hash.value, reinterpret_cast<BYTE*>(const_cast<char*>(bytes.data())), static_cast<ULONG>(bytes.size()), 0);
        return status < 0 ? HRESULT_FROM_NT(status) : S_OK;
    }

    // Finish the digest and encode its 32 bytes as lowercase ASCII hexadecimal.
    HRESULT finish(std::string& output)
    {
        BYTE digest[32];
        auto status = BCryptFinishHash(m_hash.value, digest, sizeof(digest), 0);
        if (status < 0)
            return HRESULT_FROM_NT(status);
        // ASCII alphabet used for stable trust/font fingerprint serialization.
        constexpr char digits[] = "0123456789abcdef";
        output.clear();
        for (auto byte : digest) {
            output.push_back(digits[byte >> 4]);
            output.push_back(digits[byte & 15]);
        }
        return S_OK;
    }

private:
    // Algorithm owner, kept until after the hash and its backing storage.
    AlgorithmHandle m_algorithm;
    // BCrypt's opaque hash-object storage on the component heap.
    std::vector<BYTE> m_storage;
    // Incremental hash owner, destroyed first by member order.
    HashHandle m_hash;
};

// Verify required readable faces and hash sorted font names/contents like EngineUWP.
HRESULT validateFonts(const std::wstring& directory, std::string& fingerprint)
{
    auto attributes = GetFileAttributesW(directory.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return HRESULT_FROM_WIN32(GetLastError());
    if (!(attributes & FILE_ATTRIBUTE_DIRECTORY))
        return HRESULT_FROM_WIN32(ERROR_DIRECTORY);
    // These are the engine's required faces, not just an arbitrary nonempty
    // directory. Engine font parsing remains authoritative at initialization.
    std::string bytes;
    auto result = readFile(directory + L"\\DejaVuSans.ttf", bytes);
    if (FAILED(result))
        return result;
    result = readFile(directory + L"\\DejaVuSans-Bold.ttf", bytes);
    if (FAILED(result))
        return result;
    WIN32_FIND_DATAW data;
    FindHandle iterator;
    iterator.value = FindFirstFileExW((directory + L"\\*").c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
    if (iterator.value == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());
    std::vector<std::pair<std::string, std::wstring>> names;
    do {
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        std::wstring name(data.cFileName);
        auto lower = name;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t c) -> wchar_t {
            return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + (L'a' - L'A')) : c;
        });
        auto dot = lower.rfind(L'.');
        if (dot == std::wstring::npos)
            continue;
        auto extension = lower.substr(dot);
        if (extension != L".ttf" && extension != L".otf" && extension != L".ttc" && extension != L".otc")
            continue;
        auto length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name.data(), static_cast<int>(name.size()), nullptr, 0, nullptr, nullptr);
        if (!length)
            return HRESULT_FROM_WIN32(GetLastError());
        std::string utf8(length, '\0');
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name.data(), static_cast<int>(name.size()), &utf8[0], length, nullptr, nullptr))
            return HRESULT_FROM_WIN32(GetLastError());
        names.emplace_back(std::move(utf8), std::move(name));
    } while (FindNextFileW(iterator.value, &data));
    auto error = GetLastError();
    if (error != ERROR_NO_MORE_FILES)
        return HRESULT_FROM_WIN32(error);
    // UTF8 lexicographic order agrees with Unicode code-point ordering used by
    // EngineUWP. Hash the same exact names and contents in that order.
    std::sort(names.begin(), names.end());
    Fingerprint digest;
    result = digest.initialize();
    for (auto& name : names) {
        if (FAILED(result))
            return result;
        result = readFile(directory + L"\\" + name.second, bytes);
        if (FAILED(result))
            return result;
        result = digest.add(name.first);
        if (SUCCEEDED(result))
            result = digest.add(bytes);
    }
    return FAILED(result) ? result : digest.finish(fingerprint);
}

} // namespace

// Adopt validated values and owned bytes so retries cannot reread changed trust.
ValidatedWebViewOptionsUWP::ValidatedWebViewOptionsUWP(WebViewOptionsUWP options, std::string bundle, std::string trust, std::string fonts)
    : m_options(std::move(options))
    , m_caBundle(std::move(bundle))
    , m_trustFingerprint(std::move(trust))
    , m_fontFingerprint(std::move(fonts))
{
}

// Build the V1 page view with borrowed owned buffers and fixed queue/redirect caps.
WKUWPPageOptions ValidatedWebViewOptionsUWP::pageOptions() const
{
    return { { sizeof(WKUWPPageOptions), WKUWP_STRUCTURE_VERSION },
        { reinterpret_cast<const uint8_t*>(m_caBundle.data()), static_cast<uint32_t>(m_caBundle.size()) },
        { reinterpret_cast<const uint8_t*>(m_trustFingerprint.data()), static_cast<uint32_t>(m_trustFingerprint.size()) },
        m_options.maximumConcurrentRequests, m_options.maximumResourceBytes, m_options.maximumBufferedBytes,
        m_options.requestTimeoutMilliseconds, 128, 10 };
}

// Build the V1 initialization view with canonical font path/hash and borrowed services.
WKUWPEngineOptions ValidatedWebViewOptionsUWP::engineOptions(const WKUWPPlatformServices* services) const
{
    static_assert(sizeof(wchar_t) == sizeof(uint16_t), "Frozen UTF16 path representation");
    return { { sizeof(WKUWPEngineOptions), WKUWP_STRUCTURE_VERSION },
        { reinterpret_cast<const uint16_t*>(m_options.fontDirectoryPath.data()), static_cast<uint32_t>(m_options.fontDirectoryPath.size()) },
        { reinterpret_cast<const uint8_t*>(m_fontFingerprint.data()), static_cast<uint32_t>(m_fontFingerprint.size()) }, services };
}

// Retain an immutable value copy of the application's requested configuration.
WebViewConfigurationUWP::WebViewConfigurationUWP(WebViewOptionsUWP options)
    : m_options(std::move(options))
{
}

// Validate before page use; success adopts owned data, failure leaves output untouched.
HRESULT WebViewConfigurationUWP::validate(const std::wstring& package, std::unique_ptr<const ValidatedWebViewOptionsUWP>& output) const noexcept
{
    try {
        auto options = m_options;
        if (options.maximumConcurrentRequests < 1 || options.maximumConcurrentRequests > 16
            || options.maximumResourceBytes < 64 * kibibyte || options.maximumResourceBytes > 64 * mebibyte
            || options.maximumBufferedBytes < mebibyte || options.maximumBufferedBytes > 32 * mebibyte
            || options.requestTimeoutMilliseconds < 1000 || options.requestTimeoutMilliseconds > 120000)
            return E_INVALIDARG;
        auto result = resolvePath(options.certificateAuthorityPath, package, L"cacert.pem", options.certificateAuthorityPath);
        if (FAILED(result))
            return result;
        result = resolvePath(options.fontDirectoryPath, package, L"Fonts", options.fontDirectoryPath);
        if (FAILED(result))
            return result;
        std::string bundle;
        result = readFile(options.certificateAuthorityPath, bundle);
        if (FAILED(result))
            return result;
        result = validateCertificates(bundle);
        if (FAILED(result))
            return result;
        Fingerprint digest;
        result = digest.initialize();
        if (SUCCEEDED(result))
            result = digest.add(bundle);
        std::string trust;
        if (SUCCEEDED(result))
            result = digest.finish(trust);
        if (FAILED(result))
            return result;
        std::string fonts;
        result = validateFonts(options.fontDirectoryPath, fonts);
        if (FAILED(result))
            return result;
        output = std::make_unique<const ValidatedWebViewOptionsUWP>(std::move(options), std::move(bundle), std::move(trust), std::move(fonts));
        return S_OK;
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (...) {
        return E_FAIL;
    }
}

} // namespace WebKit
