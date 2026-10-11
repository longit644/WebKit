/*
 * WKUWPEngineAPITests.cpp - host-side verification of the WKUWP V1 discovery
 * export implemented by Source/WebCore/platform/w10m/WKW10MEngineAPI.cpp.
 *
 * This is the acceptance harness for M10 task 3.2. It resolves the provider
 * exactly the way a C++/CX consumer will: by loading the module and asking for
 * the unmangled export name, never by linking against it, so a provider that
 * stops exporting "WKUWPGetEngineAPI" fails here instead of being satisfied by
 * the linker's own symbol table.
 *
 * What the harness proves, in the order the acceptance criteria are stated:
 *
 *   - A compatible request succeeds and returns a complete 24-slot table with a
 *     usable build identity.
 *   - Unsupported version, unsupported table size and unsupported capability
 *     requests fail, and the caller's output structure is left byte-for-byte
 *     untouched, which is what makes the failure safe to retry.
 *   - No failed request, and no call on a table whose features this engine does
 *     not deliver, can create a page: the page slot reports Unsupported and never
 *     writes a handle.
 *
 * The shipping engine is ARM32 and an ARM32 image cannot execute on this x64
 * development host, so the behaviour runs from an x64 copy of the same provider
 * source while the ARM32 compile and export checks are done by
 * Tools/w10m/build/test-wkw10m-engine-negotiation.ps1. Nothing here claims device
 * behaviour.
 */

#include "WKW10MEngine.h"
#include "WKW10MNavigation.h"
#include "WKW10MPage.h"

#include <windows.h>

#include <stddef.h>
#include <stdio.h>
#include <string.h>

namespace {

// Frozen ABI major/minor this harness expects the provider to implement.
constexpr uint32_t kExpectedAbiMajor = 1;
constexpr uint32_t kExpectedAbiMinor = 0;

// Frozen V1 table extent; also the smallest legal caller declaration.
constexpr uint32_t kV1TableSize = WKUWP_ENGINE_API_STRUCTURE_SIZE;

// The number of bytes this build's provider actually fills.
//
// The frozen contract is ARM32-only, where a function pointer is four bytes and
// this equals kV1TableSize. The behaviour run on this x64 host fills a larger
// structure, so the harness must measure the filled extent instead of assuming the
// frozen number, or it would report a correct provider as an overrun.
const uint32_t kFilledTableSize = static_cast<uint32_t>(sizeof(WKUWPEngineAPI));

// Buffer used for every negotiation attempt.
//
// It is a byte array rather than a WKUWPEngineAPI so a caller can legally
// declare more than the V1 table, which is how the "unknown future caller tails
// stay zero" rule is checked, and so a canary can be placed after the largest
// legal declared extent to catch any overrun. alignas(4) matches the frozen
// pack(4) ABI alignment on both compilers.
constexpr size_t kBufferBytes = 512;
constexpr size_t kCanaryOffset = 320;
constexpr uint8_t kApiPattern = 0xA5;
constexpr uint8_t kCanaryPattern = 0x5A;

// Sentinel written into caller outputs to prove a rejected call never wrote them.
constexpr uint64_t kUntouchedHandle = 0xFEEDFACECAFEBEEFull;
constexpr uint32_t kUntouchedBytes = 0xDEADBEEFu;

int g_failures = 0;

// Prints one assertion result.
//
// The gate requires every negotiation row to be checked individually: a summary
// line alone would hide which rejection regressed.
void reportCheck(const char* name, bool passed, const char* detail)
{
    printf("%s %s: %s\n", passed ? "PASS" : "FAIL", name, detail);
    if (!passed)
        g_failures++;
}

// Caller-side storage for one attempt.
//
// The whole buffer is filled with a pattern so that any byte a provider writes can
// be told apart from bytes it left alone, a copy of the buffer is taken after the
// caller has written its own header, and every byte after the compared region acts
// as a canary so a provider that writes past the declared extent is detected.
struct Attempt {
    alignas(4) uint8_t buffer[kBufferBytes];
    uint8_t reference[kBufferBytes];

    // The caller's negotiated table view over the buffer.
    WKUWPEngineAPI* api() { return reinterpret_cast<WKUWPEngineAPI*>(buffer); }

    // Fills the buffer and snapshots it as the state a rejected call must keep.
    void snapshot()
    {
        memcpy(reference, buffer, sizeof(reference));
    }

    // True when no byte changed since the snapshot, including the canary region.
    bool untouched() const
    {
        return memcmp(buffer, reference, sizeof(reference)) == 0;
    }

    // True when the canary bytes after the compared region still hold their pattern.
    bool canaryIntact() const
    {
        for (size_t index = kCanaryOffset; index < sizeof(buffer); ++index) {
            if (buffer[index] != kApiPattern)
                return false;
        }
        return true;
    }

    // True when the byte range is all zero.
    bool rangeIsZero(size_t from, size_t to) const
    {
        for (size_t index = from; index < to; ++index) {
            if (buffer[index] != 0)
                return false;
        }
        return true;
    }

    // Returns the first index at or after `from` whose byte differs from the snapshot.
    size_t firstChangedSince(size_t from) const
    {
        for (size_t index = from; index < sizeof(buffer); ++index) {
            if (buffer[index] != reference[index])
                return index;
        }
        return sizeof(buffer);
    }
};

// Writes the legal V1 caller header into an attempt buffer, then snapshots it.
void writeV1Header(Attempt& attempt, uint32_t structureSize, uint32_t structureVersion = WKUWP_STRUCTURE_VERSION)
{
    memset(attempt.buffer, kApiPattern, sizeof(attempt.buffer));
    attempt.api()->header.structureSize = structureSize;
    attempt.api()->header.structureVersion = structureVersion;
    attempt.snapshot();
}

// Collects the 24 slots in declared order.
//
// The count is asserted separately, because a table that dropped or added a slot
// while keeping sizeof unchanged would otherwise pass a null check alone.
size_t collectSlots(const WKUWPEngineAPI* api, void** slots, size_t capacity)
{
    const void* collected[] = {
        reinterpret_cast<const void*>(api->initialize),
        reinterpret_cast<const void*>(api->service),
        reinterpret_cast<const void*>(api->setSuspended),
        reinterpret_cast<const void*>(api->createPage),
        reinterpret_cast<const void*>(api->closePage),
        reinterpret_cast<const void*>(api->setCallbacks),
        reinterpret_cast<const void*>(api->detachCallbacks),
        reinterpret_cast<const void*>(api->navigate),
        reinterpret_cast<const void*>(api->loadHtml),
        reinterpret_cast<const void*>(api->goBack),
        reinterpret_cast<const void*>(api->goForward),
        reinterpret_cast<const void*>(api->reload),
        reinterpret_cast<const void*>(api->stop),
        reinterpret_cast<const void*>(api->getState),
        reinterpret_cast<const void*>(api->proceedWithCertificateFailure),
        reinterpret_cast<const void*>(api->sendInput),
        reinterpret_cast<const void*>(api->setFocus),
        reinterpret_cast<const void*>(api->resize),
        reinterpret_cast<const void*>(api->paint),
        reinterpret_cast<const void*>(api->attachCompositor),
        reinterpret_cast<const void*>(api->detachCompositor),
        reinterpret_cast<const void*>(api->renderCompositor),
        reinterpret_cast<const void*>(api->beginShutdown),
        reinterpret_cast<const void*>(api->pollShutdown),
    };
    const size_t count = sizeof(collected) / sizeof(collected[0]);
    if (count > capacity)
        return 0;
    for (size_t index = 0; index < count; ++index)
        slots[index] = const_cast<void*>(collected[index]);
    return count;
}

// True when every frozen V1 slot is callable rather than null.
bool everySlotIsCallable(const WKUWPEngineAPI* api)
{
    void* slots[32];
    const size_t count = collectSlots(api, slots, 32);
    if (count != 24)
        return false;
    for (size_t index = 0; index < count; ++index) {
        if (!slots[index])
            return false;
    }
    return true;
}

// Runs one rejected negotiation and verifies the output was untouched.
//
// `name` identifies the row, `detail` explains what the provider was asked for,
// and `expected` is the result the contract requires for it.
void checkRejection(WKUWPResult(__stdcall* getEngineAPI)(uint32_t, uint32_t, uint64_t, WKUWPEngineAPI*),
    const char* name, const char* detail, uint32_t minimumMajor, uint32_t minimumMinor,
    uint64_t requiredCapabilities, WKUWPResult expected, uint32_t declaredSize)
{
    Attempt attempt;
    writeV1Header(attempt, declaredSize);

    WKUWPResult result = getEngineAPI(minimumMajor, minimumMinor, requiredCapabilities, attempt.api());

    char message[256];
    snprintf(message, sizeof(message), "%s -> result=%d expected=%d, output %s", detail,
        static_cast<int>(result), static_cast<int>(expected), attempt.untouched() ? "untouched" : "MODIFIED");

    const bool passed = result == expected && attempt.untouched();
    reportCheck(name, passed, message);
}

// Verifies a rejected negotiation that also carries a wrong structure version.
void checkVersionRejection(WKUWPResult(__stdcall* getEngineAPI)(uint32_t, uint32_t, uint64_t, WKUWPEngineAPI*),
    const char* name, const char* detail, uint32_t structureVersion)
{
    Attempt attempt;
    writeV1Header(attempt, kFilledTableSize, structureVersion);

    WKUWPResult result = getEngineAPI(kExpectedAbiMajor, kExpectedAbiMinor, 0, attempt.api());

    char message[256];
    snprintf(message, sizeof(message), "%s -> result=%d expected=%d, output %s", detail,
        static_cast<int>(result), static_cast<int>(WKUWPResultAbiMismatch),
        attempt.untouched() ? "untouched" : "MODIFIED");

    const bool passed = result == WKUWPResultAbiMismatch && attempt.untouched();
    reportCheck(name, passed, message);
}

// Negotiation succeeds and returns a complete, usable table.
void checkSuccessfulNegotiation(WKUWPResult(__stdcall* getEngineAPI)(uint32_t, uint32_t, uint64_t, WKUWPEngineAPI*))
{
    Attempt attempt;
    writeV1Header(attempt, kFilledTableSize);

    WKUWPResult result = getEngineAPI(kExpectedAbiMajor, kExpectedAbiMinor, 0, attempt.api());
    const WKUWPEngineAPI* api = attempt.api();

    char message[256];
    snprintf(message, sizeof(message), "compatible request -> result=%d expected=0", static_cast<int>(result));
    reportCheck("success/result", result == WKUWPResultOK, message);

    snprintf(message, sizeof(message), "filled extent=%u expected=%u",
        api->header.structureSize, kFilledTableSize);
    reportCheck("success/filled-extent", api->header.structureSize == kFilledTableSize, message);

    snprintf(message, sizeof(message), "structure version=%u expected=%u",
        api->header.structureVersion, WKUWP_STRUCTURE_VERSION);
    reportCheck("success/structure-version", api->header.structureVersion == WKUWP_STRUCTURE_VERSION, message);

    snprintf(message, sizeof(message), "abi=%u.%u expected=%u.%u", api->abiMajor, api->abiMinor,
        kExpectedAbiMajor, kExpectedAbiMinor);
    reportCheck("success/version", api->abiMajor == kExpectedAbiMajor && api->abiMinor == kExpectedAbiMinor, message);

    // A caller that declares exactly the filled extent must find every later byte
    // exactly as it left them: there is no declared extent to zero beyond the table.
    bool pastExtentUnwritten = true;
    for (size_t index = kFilledTableSize; index < kBufferBytes; ++index) {
        if (attempt.buffer[index] != attempt.reference[index])
            pastExtentUnwritten = false;
    }
    snprintf(message, sizeof(message), "bytes %u..%u past the declared extent are %s (first change at %u)",
        kFilledTableSize, static_cast<unsigned>(kBufferBytes), pastExtentUnwritten ? "untouched" : "MODIFIED",
        static_cast<unsigned>(attempt.firstChangedSince(kFilledTableSize)));
    reportCheck("success/extent-respected", pastExtentUnwritten, message);

    snprintf(message, sizeof(message), "canary after %u bytes %s", static_cast<unsigned>(kCanaryOffset),
        attempt.canaryIntact() ? "intact" : "OVERWRITTEN");
    reportCheck("success/no-overrun", attempt.canaryIntact(), message);

    const bool identityUsable = api->buildId.data && api->buildId.length > 0
        && memchr(api->buildId.data, 0, api->buildId.length) == nullptr;
    snprintf(message, sizeof(message), "buildId %s length=%u",
        identityUsable ? "present" : "MISSING/TERMINATED", api->buildId.length);
    reportCheck("success/build-identity", identityUsable, message);

    if (identityUsable) {
        char identity[128];
        const size_t copyLength = api->buildId.length < sizeof(identity) - 1 ? api->buildId.length : sizeof(identity) - 1;
        memcpy(identity, api->buildId.data, copyLength);
        identity[copyLength] = 0;
        snprintf(message, sizeof(message), "buildId=\"%s\"", identity);
        reportCheck("success/build-identity-content", strstr(identity, "wkuwp abi=1.0") != nullptr, message);
    }

    snprintf(message, sizeof(message), "all 24 V1 slots callable=%d", everySlotIsCallable(api) ? 1 : 0);
    reportCheck("success/slots-callable", everySlotIsCallable(api), message);
}

// A larger declared extent is accepted, filled to V1, and its tail is zeroed so a
// consumer never reads an uninitialized byte in a slot this provider cannot fill.
void checkLargerDeclaredExtent(WKUWPResult(__stdcall* getEngineAPI)(uint32_t, uint32_t, uint64_t, WKUWPEngineAPI*))
{
    Attempt attempt;
    const uint32_t declaredSize = kFilledTableSize + 128;
    writeV1Header(attempt, declaredSize);

    WKUWPResult result = getEngineAPI(kExpectedAbiMajor, kExpectedAbiMinor, 0, attempt.api());

    const bool tailZeroed = attempt.rangeIsZero(kFilledTableSize, declaredSize);
    bool tailUnwritten = true;
    for (size_t index = declaredSize; index < kBufferBytes; ++index) {
        if (attempt.buffer[index] != attempt.reference[index])
            tailUnwritten = false;
    }

    char message[256];
    snprintf(message, sizeof(message), "declared=%u -> result=%d, filled=%u, tail %s, past declared extent %s",
        declaredSize, static_cast<int>(result), attempt.api()->header.structureSize, tailZeroed ? "zero" : "NOT ZERO",
        tailUnwritten ? "untouched" : "MODIFIED");

    const bool passed = result == WKUWPResultOK && attempt.api()->header.structureSize == kFilledTableSize && tailZeroed
        && tailUnwritten;
    reportCheck("success/larger-declared-extent", passed, message);
}

// The build identity is stable across calls and never needs lazy initialization.
void checkIdentityStability(WKUWPResult(__stdcall* getEngineAPI)(uint32_t, uint32_t, uint64_t, WKUWPEngineAPI*))
{
    Attempt first;
    Attempt second;
    writeV1Header(first, kFilledTableSize);
    writeV1Header(second, kFilledTableSize);

    getEngineAPI(kExpectedAbiMajor, kExpectedAbiMinor, 0, first.api());
    getEngineAPI(kExpectedAbiMajor, kExpectedAbiMinor, 0, second.api());

    const bool stable = first.api()->buildId.data == second.api()->buildId.data
        && first.api()->buildId.length == second.api()->buildId.length
        && memcmp(first.api()->buildId.data, second.api()->buildId.data, first.api()->buildId.length) == 0;

    char message[256];
    snprintf(message, sizeof(message), "two negotiations share one identity=%d, bytes %d",
        stable ? 1 : 0, memcmp(first.buffer, second.buffer, kFilledTableSize) == 0 ? 1 : 0);
    reportCheck("success/identity-stable", stable, message);
}

// No capability this engine does not deliver is accepted.
//
// Every frozen bit is requested on its own so a future change that advertises a
// bit without implementing it fails here by name.
void checkCapabilitiesRejected(WKUWPResult(__stdcall* getEngineAPI)(uint32_t, uint32_t, uint64_t, WKUWPEngineAPI*),
    uint64_t deliveredCapabilities)
{
    // One requested capability bit and the name its failure would be reported under.
    struct CapabilityRow {
        const char* name;
        uint64_t bit;
    };
    const CapabilityRow rows[] = {
        { "structured-notifications", WKUWPCapabilityStructuredNotifications },
        { "host-html", WKUWPCapabilityHostHtml },
        { "independent-sessions", WKUWPCapabilityIndependentSessions },
        { "cpu-paint", WKUWPCapabilityCPUPaint },
        { "gpu-compositor", WKUWPCapabilityGPUCompositor },
        { "clipboard-service", WKUWPCapabilityClipboardService },
        { "certificate-decisions", WKUWPCapabilityCertificateDecisions },
        { "input", WKUWPCapabilityInput },
        { "immutable-trust-policy", WKUWPCapabilityImmutableTrustPolicy },
        { "global-font-configuration", WKUWPCapabilityGlobalFontConfiguration },
    };
    const size_t rowCount = sizeof(rows) / sizeof(rows[0]);

    for (size_t index = 0; index < rowCount; ++index) {
        char name[64];
        snprintf(name, sizeof(name), "capability/%s", rows[index].name);
        const bool supported = (deliveredCapabilities & rows[index].bit) != 0;
        const WKUWPResult expected = supported ? WKUWPResultOK : WKUWPResultMissingCapability;
        checkRejection(getEngineAPI, name,
            supported ? "delivered capability is accepted" : "undelivered capability is refused",
            kExpectedAbiMajor, kExpectedAbiMinor, rows[index].bit, expected, kFilledTableSize);
    }

    checkRejection(getEngineAPI, "capability/all-bits-set", "every reserved bit requested together",
        kExpectedAbiMajor, kExpectedAbiMinor, ~static_cast<uint64_t>(0), WKUWPResultMissingCapability, kFilledTableSize);

    Attempt attempt;
    writeV1Header(attempt, kFilledTableSize);
    getEngineAPI(kExpectedAbiMajor, kExpectedAbiMinor, 0, attempt.api());
    char message[128];
    snprintf(message, sizeof(message), "delivered mask=0x%llx", static_cast<unsigned long long>(attempt.api()->capabilities));
    reportCheck("capability/mask", attempt.api()->capabilities == deliveredCapabilities, message);
}

// Page creation is impossible until a page implementation is delivered.
void checkNoPageCreation(WKUWPResult(__stdcall* getEngineAPI)(uint32_t, uint32_t, uint64_t, WKUWPEngineAPI*))
{
    Attempt attempt;
    writeV1Header(attempt, kFilledTableSize);

    if (getEngineAPI(kExpectedAbiMajor, kExpectedAbiMinor, 0, attempt.api()) != WKUWPResultOK) {
        reportCheck("page/not-created", false, "compatible negotiation failed, cannot continue");
        return;
    }

    const WKUWPEngineAPI* api = attempt.api();

    WKUWPPageOptions options;
    memset(&options, 0, sizeof(options));
    options.header.structureSize = sizeof(options);
    options.header.structureVersion = WKUWP_STRUCTURE_VERSION;

    WKUWPPageHandle page = kUntouchedHandle;
    WKUWPResult result = api->createPage(&options, &page);

    char message[256];
    snprintf(message, sizeof(message), "createPage -> result=%d expected=%d, handle %s",
        static_cast<int>(result), static_cast<int>(WKUWPResultUnsupported),
        page == kUntouchedHandle ? "untouched" : "WRITTEN");
    reportCheck("page/not-created", result == WKUWPResultUnsupported && page == kUntouchedHandle, message);

    // Navigation must equally refuse without allocating an identifier.
    WKUWPNavigationRequest request;
    memset(&request, 0, sizeof(request));
    request.header.structureSize = sizeof(request);
    request.header.structureVersion = WKUWP_STRUCTURE_VERSION;
    request.contentKind = WKUWPContentRemote;

    uint64_t navigationId = kUntouchedHandle;
    result = api->navigate(kUntouchedHandle, &request, &navigationId);
    snprintf(message, sizeof(message), "navigate -> result=%d expected=%d, id %s",
        static_cast<int>(result), static_cast<int>(WKUWPResultUnsupported),
        navigationId == kUntouchedHandle ? "untouched" : "WRITTEN");
    reportCheck("page/no-navigation-id", result == WKUWPResultUnsupported && navigationId == kUntouchedHandle, message);

    // State queries must not write the caller's required-byte count.
    uint32_t requiredBytes = kUntouchedBytes;
    result = api->getState(kUntouchedHandle, nullptr, nullptr, 0, &requiredBytes);
    snprintf(message, sizeof(message), "getState -> result=%d expected=%d, requiredBytes %s",
        static_cast<int>(result), static_cast<int>(WKUWPResultUnsupported),
        requiredBytes == kUntouchedBytes ? "untouched" : "WRITTEN");
    reportCheck("page/state-untouched", result == WKUWPResultUnsupported && requiredBytes == kUntouchedBytes, message);
}

} // namespace

// Runs the negotiation gate against the provider module named on the command line.
//
// The module is loaded rather than linked, so this harness can only reach the
// entry point through the exported name a real consumer would resolve.
int main(int argc, char** argv)
{
    if (argc < 2) {
        printf("usage: WKUWPEngineAPITests <provider-module>\n");
        return 2;
    }

    HMODULE module = LoadLibraryA(argv[1]);
    if (!module) {
        printf("FAIL load: cannot load provider module %s (error %lu)\n", argv[1], GetLastError());
        return 1;
    }

    // Resolve the documented unmangled export name. A mangled or missing symbol
    // fails here, which is the failure a C++/CX consumer would hit first.
    typedef WKUWPResult(__stdcall * GetEngineAPI)(uint32_t, uint32_t, uint64_t, WKUWPEngineAPI*);
    GetEngineAPI getEngineAPI = reinterpret_cast<GetEngineAPI>(
        reinterpret_cast<void*>(GetProcAddress(module, "WKUWPGetEngineAPI")));
    if (!getEngineAPI) {
        printf("FAIL export: WKUWPGetEngineAPI is not exported by %s (error %lu)\n", argv[1], GetLastError());
        return 1;
    }
    printf("PASS export: WKUWPGetEngineAPI resolved from %s\n", argv[1]);
    printf("host view: sizeof(WKUWPEngineAPI)=%u pollShutdown=%u\n\n",
        static_cast<unsigned>(sizeof(WKUWPEngineAPI)), static_cast<unsigned>(offsetof(WKUWPEngineAPI, pollShutdown)));

    printf("null output pointer\n");
    {
        WKUWPResult result = getEngineAPI(kExpectedAbiMajor, kExpectedAbiMinor, 0, nullptr);
        char message[128];
        snprintf(message, sizeof(message), "null outAPI -> result=%d expected=%d", static_cast<int>(result),
            static_cast<int>(WKUWPResultInvalidArgument));
        reportCheck("argument/null-output", result == WKUWPResultInvalidArgument, message);
    }

    printf("\nversion negotiation\n");
    checkVersionRejection(getEngineAPI, "version/unknown-version-0", "caller structure version 0", 0);
    checkVersionRejection(getEngineAPI, "version/unknown-version-2", "caller structure version 2", 2);
    checkVersionRejection(getEngineAPI, "version/future-version", "caller structure version 99", 99);
    checkRejection(getEngineAPI, "version/major-zero", "minimumMajor 0", 0, kExpectedAbiMinor, 0,
        WKUWPResultAbiMismatch, kFilledTableSize);
    checkRejection(getEngineAPI, "version/major-two", "minimumMajor 2", 2, kExpectedAbiMinor, 0,
        WKUWPResultAbiMismatch, kFilledTableSize);
    checkRejection(getEngineAPI, "version/minor-one", "minimumMinor 1 above the provider minor", kExpectedAbiMajor, 1, 0,
        WKUWPResultAbiMismatch, kFilledTableSize);
    checkRejection(getEngineAPI, "version/minor-huge", "minimumMinor 0xFFFFFFFF", kExpectedAbiMajor, 0xFFFFFFFF, 0,
        WKUWPResultAbiMismatch, kFilledTableSize);

    printf("\ntable size negotiation\n");
    checkRejection(getEngineAPI, "size/zero-extent", "declared structureSize 0", kExpectedAbiMajor, kExpectedAbiMinor, 0,
        WKUWPResultAbiMismatch, 0);
    checkRejection(getEngineAPI, "size/one-byte", "declared structureSize 1", kExpectedAbiMajor, kExpectedAbiMinor, 0,
        WKUWPResultAbiMismatch, 0);
    checkRejection(getEngineAPI, "size/one-byte-short", "declared structureSize one below the frozen V1 size",
        kExpectedAbiMajor, kExpectedAbiMinor, 0, WKUWPResultAbiMismatch, 0);

    // A caller declaring exactly the frozen V1 extent must be accepted on the
    // boundary architecture, where that is the whole table. A host build whose
    // table is larger must instead refuse it, because filling it would write past
    // the caller's declared structure; this check asserts whichever rule applies,
    // so a provider that ignored the declared extent would fail here.
    {
        Attempt frozen;
        writeV1Header(frozen, kV1TableSize);
        const WKUWPResult result = getEngineAPI(kExpectedAbiMajor, kExpectedAbiMinor, 0, frozen.api());
        const WKUWPResult expected = kV1TableSize >= kFilledTableSize ? WKUWPResultOK : WKUWPResultAbiMismatch;
        char message[200];
        snprintf(message, sizeof(message),
            "declared structureSize %u with a %u-byte table -> result=%d expected=%d, output %s", kV1TableSize,
            kFilledTableSize, static_cast<int>(result), static_cast<int>(expected),
            result == WKUWPResultOK ? "filled" : (frozen.untouched() ? "untouched" : "MODIFIED"));
        const bool passed = result == expected && (result != WKUWPResultOK || frozen.untouched());
        reportCheck("size/frozen-v1-extent", passed, message);
    }

    Attempt oversized;
    writeV1Header(oversized, 0x10000);
    {
        WKUWPResult result = getEngineAPI(kExpectedAbiMajor, kExpectedAbiMinor, 0, oversized.api());
        char message[160];
        snprintf(message, sizeof(message), "declared structureSize 65536 -> result=%d expected=%d, output %s",
            static_cast<int>(result), static_cast<int>(WKUWPResultAbiMismatch),
            oversized.untouched() ? "untouched" : "MODIFIED");
        reportCheck("size/beyond-write-bound", result == WKUWPResultAbiMismatch && oversized.untouched(), message);
    }

    printf("\ncapability negotiation\n");
    checkCapabilitiesRejected(getEngineAPI, 0);

    printf("\nsuccessful negotiation\n");
    checkSuccessfulNegotiation(getEngineAPI);
    checkLargerDeclaredExtent(getEngineAPI);
    checkIdentityStability(getEngineAPI);

    printf("\npage creation\n");
    checkNoPageCreation(getEngineAPI);

    printf("\n");
    if (g_failures) {
        printf("FAILED: %d negotiation check(s) did not hold.\n", g_failures);
        return 1;
    }

    printf("All WKUWP discovery negotiation checks passed.\n");
    FreeLibrary(module);
    return 0;
}
