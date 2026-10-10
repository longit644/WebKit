/*
 * WKUWPEngine.h - frozen V1 engine contract shared by the WebCore engine DLL and
 * every out-of-process consumer.
 *
 * This header is the machine-readable form of
 * openspec/changes/wkview-m10-uwp-cppcx-restructure/artifact/native-abi.md.
 * That document is the authority; if this file and the document ever disagree,
 * the change is wrong and must be revised before code follows it.
 *
 * Design constraints that are not negotiable at the ABI boundary:
 *
 *   - Little-endian ARM32 only, verified by the layout conformance check. No
 *     compiler may silently choose a different layout; both the native engine
 *     compiler and the MSVC C++/CX compiler must agree byte for byte.
 *   - Fixed-width stdint types only. No long, no size_t, no enum members and no
 *     bool cross this boundary, because their widths differ between the two
 *     compilers and between architectures.
 *   - Every structure carries structureSize/structureVersion so a caller and
 *     provider can negotiate instead of assuming. Compatible growth appends
 *     members; incompatible change increments the major version below.
 *   - No WebCore type, C++/CX type, STL type or cross-module allocation ever
 *     crosses here. Strings are borrowed spans or caller-owned buffers, and
 *     ownership is stated on every member.
 *   - Nothing throws across this boundary. A callback reports failure through
 *     its documented result value.
 *
 * Callbacks are __stdcall for ABI stability even where ARM ignores the
 * convention. The single discovery entry point is extern "C" with C linkage and
 * the exact unmangled name declared at the bottom of this header.
 */

#ifndef WKUWP_ENGINE_H
#define WKUWP_ENGINE_H

#include <stdint.h>

/*
 * Every ABI structure in this header and its companions is packed to 4 bytes, so
 * the two compilers agree on offsets regardless of their default alignment of
 * 8-byte types. The pack region spans the structures only; it is closed before
 * the export declaration at the end of this file.
 */
#pragma pack(push, 4)

/* Engine module shared-library name the consumer binds with LoadLibrary. */
#define WKUWP_ENGINE_MODULE_NAME "WebCore.dll"

/* ABI major version. A consumer requiring a different major cannot proceed. */
#define WKUWP_ABI_MAJOR_VERSION 1

/*
 * ABI minor version. A provider may append members for a higher minor; a
 * consumer must check the filled structureSize before reading appended members
 * it knows about.
 */
#define WKUWP_ABI_MINOR_VERSION 0

/* Structure header version carried by every WKUWP structure. */
#define WKUWP_STRUCTURE_VERSION 1

/*
 * Fixed entry-point structureSize the provider fills. A caller must pass at
 * least this many bytes of its own structure, and a provider writes no more
 * than this in V1.
 */
#define WKUWP_ENGINE_API_STRUCTURE_SIZE 128

/* Page handles are process-unique nonzero identifiers, never pointers. */
typedef uint64_t WKUWPPageHandle;

/* Borrowed UTF-8 byte span. Length is authoritative; no terminator is implied. */
typedef struct {
    const uint8_t* data;
    uint32_t length;
} WKUWPString;

/* Borrowed UTF-16 code-unit span used for filesystem paths. */
typedef struct {
    const uint16_t* data;
    uint32_t length;
} WKUWPPath;

/*
 * Opaque certificate-decision capability.
 *
 * issuer is the issuing page/session handle and decision is that session's
 * monotonic failure revision. Neither value is exposed through the public
 * C++/CX token type, and equal numeric navigation IDs in two controls never
 * imply equal tokens. Zero/zero means "no decision".
 */
typedef struct {
    uint64_t issuer;
    uint64_t decision;
} WKUWPCertificateToken;

/*
 * Present on every WKUWP structure.
 *
 * structureSize is the caller's byte extent, so a provider can detect a caller
 * built against a newer minor version. structureVersion is the layout revision
 * of the structure itself, independent of the table's minor version.
 */
typedef struct {
    uint32_t structureSize;
    uint32_t structureVersion;
} WKUWPHeader;

/*
 * Operation results returned by every entry point below.
 *
 * Positive values are this contract's own codes. A negative value is an
 * originating HRESULT passed through unchanged, which is how a platform or
 * handler failure keeps its identity across the boundary. Success is exactly
 * zero; callers must test the whole value, not "non-negative".
 *
 * These are operation results, not navigation errors. Navigation outcome and
 * error domain travel separately in WKUWPPageEvent.
 */
typedef enum {
    /* Successful operation; canonical acronym spelling for new callers. */
    WKUWPResultOK = 0,
    WKUWPResultInvalidArgument = 1,
    /* 2 and 3 are reserved legacy gaps; never assign them. */
    WKUWPResultNoMainFrame = 4,
    WKUWPResultNoActiveDocumentLoader = 5,
    WKUWPResultNoPaintSurface = 6,
    WKUWPResultDocumentWriterBeginFailed = 7,
    WKUWPResultWrongThread = 8,
    WKUWPResultCompositorConfigurationUnsupported = 9,
    WKUWPResultCompositorUnavailable = 10,
    /* 11 through 15 are reserved. */
    WKUWPResultClosed = 16,
    WKUWPResultAbiMismatch = 17,
    WKUWPResultMissingCapability = 18,
    WKUWPResultInvalidState = 19,
    WKUWPResultPending = 20,
    WKUWPResultBufferTooSmall = 21,
    WKUWPResultUnsupported = 22,
    WKUWPResultBudgetExceeded = 23,
    WKUWPResultShuttingDown = 24,
    WKUWPResultConfigurationConflict = 25,
    WKUWPResultInvalidCertificateToken = 26,
    WKUWPResultNotInitialized = 27,
    WKUWPResultDispatcherUnavailable = 28
} WKUWPResult;

/*
 * Capability bits reported by the provider and requested by the consumer.
 *
 * The public control requires structured notifications, host HTML, independent
 * sessions, opaque certificate decisions, input, immutable trust policy and
 * global font conflict checking, plus at least one presentation mode. Clipboard
 * and GPU compositor are optional and must degrade to an explicit unsupported
 * state rather than a silent no-op.
 */
typedef enum {
    WKUWPCapabilityStructuredNotifications = 0x001,
    WKUWPCapabilityHostHtml = 0x002,
    WKUWPCapabilityIndependentSessions = 0x004,
    /* Host-owned CPU premultiplied BGRA paint capability. */
    WKUWPCapabilityCPUPaint = 0x008,
    /* Engine-owned GPU compositor capability. */
    WKUWPCapabilityGPUCompositor = 0x010,
    WKUWPCapabilityClipboardService = 0x020,
    WKUWPCapabilityCertificateDecisions = 0x040,
    WKUWPCapabilityInput = 0x080,
    WKUWPCapabilityImmutableTrustPolicy = 0x100,
    WKUWPCapabilityGlobalFontConfiguration = 0x200
} WKUWPCapability;

/* Callback context lifetime protocol shared by pages and platform services. */
typedef void(__stdcall* WKUWPRetainContext)(void* context);
typedef void(__stdcall* WKUWPReleaseContext)(void* context);

/* Clipboard access kinds passed to WKUWPPlatformServices::requestClipboard. */
typedef enum {
    WKUWPClipboardRead = 0,
    WKUWPClipboardWrite = 1
} WKUWPClipboardKind;

/*
 * Clipboard completion callback.
 *
 * text is valid only during the call for a read; the engine copies it. The
 * engine retains completionContext until this returns and never calls it again
 * for the same operationId, so a canceled operation still completes exactly
 * once before its context is released.
 */
typedef void(__stdcall* WKUWPClipboardCompletion)(
    void* completionContext, uint64_t operationId, WKUWPResult result, const WKUWPString* text);

/*
 * Forward declarations of the structures referenced by pointer in the function
 * table below. They are declared here rather than after the table so a member
 * declared as `const struct Foo*` refers to the same visible tag on every
 * compiler, instead of to an unrelated type-scope-local declaration.
 */
struct WKUWPEngineOptions;
struct WKUWPPageOptions;
struct WKUWPPageState;
struct WKUWPNavigationRequest;
struct WKUWPInputEvent;
struct WKUWPPaintBuffer;
struct WKUWPCompositorOptions;
struct WKUWPPageCallbacks;

/*
 * Process-wide platform services the engine may call back into.
 *
 * All members except postWake's enqueue request execute on the owner thread.
 * The context is retained once on successful initialize and released only after
 * shutdown quiescence, never per call.
 */
typedef struct WKUWPPlatformServices {
    WKUWPHeader header;

    /* Opaque host context; the provider retains it once via retainContext. */
    void* context;

    /* Retains/releases the context above; required when context is non-null. */
    WKUWPRetainContext retainContext;
    WKUWPReleaseContext releaseContext;

    /*
     * Requests clipboard read or write. On WKUWPResultOK the operation must
     * complete exactly once, including when later cancelled, and the provider
     * owns completionContext until that completion returns. A rejected request
     * must never complete later.
     */
    WKUWPResult(__stdcall* requestClipboard)(
        void* context, WKUWPPageHandle page, uint64_t operationId, uint32_t kind,
        const WKUWPString* text, WKUWPClipboardCompletion completion, void* completionContext);

    /* Cancels an accepted request; the canceled completion still arrives once. */
    WKUWPResult(__stdcall* cancelClipboard)(void* context, uint64_t operationId);

    /*
     * Wakes the owner thread after transport progress. Callable from workers; it
     * must not touch page or XAML state, only schedule owner-thread work.
     */
    WKUWPResult(__stdcall* postWake)(void* context);
} WKUWPPlatformServices;

/*
 * Engine function table.
 *
 * header.structureSize is the number of bytes the provider actually filled, so
 * a consumer can detect appended slots before reading them. Slots are appended
 * only; existing slot order never changes.
 */
typedef struct {
    WKUWPHeader header;

    /* Provided ABI version, reported for logging and mismatch diagnostics. */
    uint32_t abiMajor;
    uint32_t abiMinor;

    /* Bitwise OR of WKUWPCapability values this provider supports. */
    uint64_t capabilities;

    /* Module-owned immutable build identity, valid for the process lifetime. */
    WKUWPString buildId;

    /*
     * Initializes the process engine. Idempotent only for an identical frozen
     * font configuration and platform-service identity; joining must not retain
     * the services context twice. After beginShutdown it fails with
     * WKUWPResultShuttingDown rather than reinitializing global state.
     */
    WKUWPResult(__stdcall* initialize)(const struct WKUWPEngineOptions* options);

    /* Runs timers, transport pump and deferred work once on the owner thread. */
    WKUWPResult(__stdcall* service)(void);

    /* Records application suspension so deadlines pause and work quiesces. */
    WKUWPResult(__stdcall* setSuspended)(uint32_t suspended);

    /* Creates one page with independent session and immutable trust policy. */
    WKUWPResult(__stdcall* createPage)(const struct WKUWPPageOptions* options, WKUWPPageHandle* page);

    /* Releases a page, cancelling work and acknowledging callbacks first. */
    WKUWPResult(__stdcall* closePage)(WKUWPPageHandle page);

    /*
     * Installs the page callback table. The provider copies it and calls
     * retainContext once on the owner thread. Replacing an attachment before a
     * detach is acknowledged fails with WKUWPResultInvalidState.
     */
    WKUWPResult(__stdcall* setCallbacks)(WKUWPPageHandle page, const struct WKUWPPageCallbacks* callbacks);

    /*
     * Deactivates callbacks immediately. Returns WKUWPResultPending when called
     * during dispatch; releaseContext then runs exactly once as the
     * acknowledgment that host storage may be freed.
     */
    WKUWPResult(__stdcall* detachCallbacks)(WKUWPPageHandle page);

    /* Starts a remote navigation; returns the allocated navigation ID. */
    WKUWPResult(__stdcall* navigate)(WKUWPPageHandle page, const struct WKUWPNavigationRequest* request, uint64_t* navigationId);

    /* Loads host HTML under the documented substitute-data contract. */
    WKUWPResult(__stdcall* loadHtml)(WKUWPPageHandle page, const struct WKUWPNavigationRequest* request, uint64_t* navigationId);

    WKUWPResult(__stdcall* goBack)(WKUWPPageHandle page);
    WKUWPResult(__stdcall* goForward)(WKUWPPageHandle page);

    /* Reloads the current document with a new navigation ID and no history entry. */
    WKUWPResult(__stdcall* reload)(WKUWPPageHandle page);

    /* Cancels outstanding load work for this page. */
    WKUWPResult(__stdcall* stop)(WKUWPPageHandle page);

    /*
     * Snapshots page state into caller-owned storage. Strings are written into
     * storage and valid until the caller changes or frees it. On
     * WKUWPResultBufferTooSmall only requiredBytes is written.
     */
    WKUWPResult(__stdcall* getState)(WKUWPPageHandle page, struct WKUWPPageState* state,
        uint8_t* storage, uint32_t capacityBytes, uint32_t* requiredBytes);

    /*
     * Consumes an eligible certificate decision and retries its exact target
     * with a new navigation ID. The decision is consumed atomically even if the
     * retry is later vetoed or fails.
     */
    WKUWPResult(__stdcall* proceedWithCertificateFailure)(WKUWPPageHandle page,
        const WKUWPCertificateToken* token, uint64_t* retryNavigationId);

    /* Delivers one input event in page viewport CSS pixels. */
    WKUWPResult(__stdcall* sendInput)(WKUWPPageHandle page, const struct WKUWPInputEvent* input);

    /* Sets keyboard focus state for the page. */
    WKUWPResult(__stdcall* setFocus)(WKUWPPageHandle page, uint32_t focused);

    /* Sets CSS size and device scale; physical buffers follow from these. */
    WKUWPResult(__stdcall* resize)(WKUWPPageHandle page, uint32_t widthCssPixels, uint32_t heightCssPixels,
        double scaleX, double scaleY);

    /* Paints one premultiplied BGRA frame into the caller's buffer. */
    WKUWPResult(__stdcall* paint)(WKUWPPageHandle page, const struct WKUWPPaintBuffer* buffer);

    /*
     * Attaches the engine-owned GPU compositor to a panel the host owns. The
     * panel pointer is borrowed for this call; the provider QIs and AddRefs
     * anything it retains and releases it on detach or close.
     */
    WKUWPResult(__stdcall* attachCompositor)(WKUWPPageHandle page, const struct WKUWPCompositorOptions* options);

    /* Detaches and releases engine GPU resources before a CPU surface attaches. */
    WKUWPResult(__stdcall* detachCompositor)(WKUWPPageHandle page);

    /* Presents one engine-owned compositor frame with overscroll offset. */
    WKUWPResult(__stdcall* renderCompositor)(WKUWPPageHandle page, double overscrollYCssPixels);

    /*
     * Begins orderly process shutdown: reject new work, invalidate decisions,
     * cancel workers, detach pages, acknowledge platform requests and revoke
     * global hooks. Idempotent.
     */
    WKUWPResult(__stdcall* beginShutdown)(void);

    /*
     * Reports shutdown progress, returning WKUWPResultPending until dispatch,
     * worker, platform and cleanup counts all reach zero.
     */
    WKUWPResult(__stdcall* pollShutdown)(void);
} WKUWPEngineAPI;

/*
 * Engine initialization options.
 *
 * fontDirectory and fontFingerprint participate in the process-global font
 * decision documented in artifact/public-api.md: the first successful setup
 * freezes them and a conflicting later request fails with
 * WKUWPResultConfigurationConflict instead of silently changing live pages.
 */
typedef struct WKUWPEngineOptions {
    WKUWPHeader header;

    /* Canonical packaged font directory; validated before it is adopted. */
    WKUWPPath fontDirectory;

    /* ASCII hex SHA-256 over the canonical sorted font file contents. */
    WKUWPString fontFingerprint;

    /* Optional host services; the provider retains the context once. */
    const WKUWPPlatformServices* platformServices;
} WKUWPEngineOptions;

#pragma pack(pop)

#if defined(_WIN32)
#define WKUWP_EXPORT __declspec(dllexport)
#else
#define WKUWP_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Sole discovery entry point.
 *
 * The provider checks that minimumMajor matches exactly, that its own minor is
 * at least minimumMinor, and that every requested capability bit is supported.
 * On rejection it writes no output. On success it zeroes the caller's declared
 * extent and fills exactly structureSize bytes.
 */
WKUWP_EXPORT WKUWPResult __stdcall WKUWPGetEngineAPI(
    uint32_t minimumMajor, uint32_t minimumMinor, uint64_t requiredCapabilities, WKUWPEngineAPI* outAPI);

#ifdef __cplusplus
}
#endif

#endif /* WKUWP_ENGINE_H */
