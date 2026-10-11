#pragma once

#include "WKW10MPage.h"

#include <memory>
#include <string>
#include <windows.h>

namespace WebKit {

// Caller-owned input. WebViewConfigurationW10M makes an immutable value copy.
struct WebViewOptionsW10M {
    // Requested CA-file path, or empty for the package default.
    std::wstring certificateAuthorityPath;
    // Requested font directory, or empty for the package default.
    std::wstring fontDirectoryPath;
    // Maximum active transfers per page session, default eight.
    uint32_t maximumConcurrentRequests { 8 };
    // Per-resource decoded-byte cap, default 32 MiB.
    uint64_t maximumResourceBytes { 32ULL * 1024 * 1024 };
    // Session transient-delivery cap, default 16 MiB.
    uint64_t maximumBufferedBytes { 16ULL * 1024 * 1024 };
    // Active-time deadline from enqueue, default thirty seconds.
    uint32_t requestTimeoutMilliseconds { 30000 };
};

// Owned bytes survive path/file changes and initialization retries. Frozen ABI
// views borrow only this object's storage; no STL object crosses the boundary.
class ValidatedWebViewOptionsW10M {
public:
    // Adopt fully validated copied options, retained CA bytes and both content hashes.
    ValidatedWebViewOptionsW10M(WebViewOptionsW10M, std::string caBundle, std::string trustFingerprint, std::string fontFingerprint);
    // Borrow immutable canonical paths/limits for this holder's lifetime.
    const WebViewOptionsW10M& options() const { return m_options; }
    // Borrow retained PEM bytes, independent of later filesystem mutation.
    const std::string& caBundle() const { return m_caBundle; }
    // Borrow ASCII lowercase SHA256 of the retained PEM bytes.
    const std::string& trustFingerprint() const { return m_trustFingerprint; }
    // Borrow ASCII lowercase SHA256 of sorted font names/contents.
    const std::string& fontFingerprint() const { return m_fontFingerprint; }
    // Build a frozen V1 page view borrowing this holder's owned buffers.
    WKW10MPageOptions pageOptions() const;
    // Build a frozen V1 process-init view borrowing the font path/hash.
    WKW10MEngineOptions engineOptions(const WKW10MPlatformServices*) const;

private:
    // Canonical validated options; no caller can mutate them after adoption.
    const WebViewOptionsW10M m_options;
    // Actual PEM contents retained before any page is created.
    const std::string m_caBundle;
    // Trust identity verified against the retained PEM contents.
    const std::string m_trustFingerprint;
    // Process-font configuration identity for conflict detection.
    const std::string m_fontFingerprint;
};

// Immutable constructor copy with explicit asset/range validation before page use.
class WebViewConfigurationW10M {
public:
    // Copy/move the six requested values without retaining a mutable application bag.
    explicit WebViewConfigurationW10M(WebViewOptionsW10M);
    // Validation is independent of page/engine creation. On failure output is
    // untouched and the original HRESULT is retained. Relative/default paths
    // resolve against the containing package, never the process working folder.
    HRESULT validate(const std::wstring& packageDirectory, std::unique_ptr<const ValidatedWebViewOptionsW10M>& output) const noexcept;

private:
    // Original frozen request, reused when an initialization attempt is retried.
    const WebViewOptionsW10M m_options;
};

} // namespace WebKit

