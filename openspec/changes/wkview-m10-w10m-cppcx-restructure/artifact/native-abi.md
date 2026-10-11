# M10 native engine ABI - V1 schema

Companion to [../design.md](../design.md) and [public-api.md](public-api.md).

Revision: independent-review corrections. This is a proposed frozen schema, not an existing export. Implement task 3.0 schema/layout conformance before either producer or consumer. Do not choose different signatures/packing while coding. If both ARM compilers cannot honor this schema, revise the design before proceeding.

## Headers and layout rules

Headers live in Source/WebKit/Shared/uwp: WKUWPEngine.h (discovery/services), WKUWPPage.h (page/config/state), WKUWPNavigation.h (request/events/tokens), WKUWPInput.h (input), WKUWPRendering.h (CPU/GPU interop).

V1 targets little-endian ARM32 only: sizeof(void*)=4, stdint fixed-width integers, `#pragma pack(push, 4)` / pop around ABI structures, no bool/enum layout, no STL/CX/WebCore type. Every callback/function pointer uses __stdcall. The single discovery export uses extern "C" with the exact unmangled name below. Preserve SDK COM ABI only at the documented graphics interface; it does not grant general permission to exchange implementation pointers. No cross-compiler malloc/free.

Declarations in this document are the required schema (typedefs named here may be split among the headers without changing layout). All reserved/absent members are zero. Embedded NUL is rejected in paths/URI; text HTML may contain it by length. Span data with nonzero length must be nonnull. Paths use uint16_t code units; never assume wchar_t size implicitly. Input spans are copied before callback dispatch or asynchronous use. Unknown structure tails are ignored only on a compatible major/version, not interpreted as existing fields.

```c
typedef int32_t WKUWPResult;
typedef uint64_t WKUWPPageHandle; /* nonzero process-unique ID, never pointer */
typedef struct { uint32_t structureSize, structureVersion; } WKUWPHeader;
typedef struct { const uint8_t* data; uint32_t length; } WKUWPString;
typedef struct { const uint16_t* data; uint32_t length; } WKUWPPath;
typedef struct { uint64_t issuer, decision; } WKUWPCertificateToken;
```

Header version=1; size is the actual caller/producer byte extent. Token issuer is the process-unique page/session handle; decision is its monotonic failure revision. Neither value is exported through the public CX token. Engine checks both plus current eligible failure; page-handle generations are never reused. Zero pair means absent. Tokens are capabilities for trusted native host use, not web-content data.

## Results and capabilities

| WKUWPResult value | Meaning / public mapping |
|---|---|
| 0 | Ok |
| 1 | InvalidArgument / E_INVALIDARG |
| 2,3 | Reserved legacy gaps; never assign |
| 4 | NoMainFrame / E_FAIL |
| 5 | NoActiveDocumentLoader / E_FAIL |
| 6 | NoPaintSurface / E_FAIL |
| 7 | DocumentWriterBeginFailed / E_FAIL |
| 8 | WrongThread / RPC_E_WRONG_THREAD |
| 9 | CompositorConfigurationUnsupported / E_NOTIMPL |
| 10 | CompositorUnavailable / E_FAIL |
| 11..15 | Reserved |
| 16 | Closed / RO_E_CLOSED |
| 17 | AbiMismatch / HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH) |
| 18 | MissingCapability / E_NOTIMPL |
| 19 | InvalidState / E_ILLEGAL_METHOD_CALL |
| 20 | Pending (detach/close/shutdown still unwinding; not an error terminal) |
| 21 | BufferTooSmall / HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER) |
| 22 | Unsupported / E_NOTIMPL |
| 23 | BudgetExceeded / HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY) |
| 24 | ShuttingDown / RO_E_CLOSED |
| 25 | ConfigurationConflict / HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED) |
| 26 | InvalidCertificateToken / E_INVALIDARG |
| 27 | NotInitialized / E_ILLEGAL_METHOD_CALL |
| 28 | DispatcherUnavailable / HRESULT_FROM_WIN32(ERROR_INVALID_STATE) |

These are operation results, not navigation errors. Legacy exports retain old signatures/layouts/numerics and map new results explicitly; never reinterpret a legacy state struct. Navigation error domain/outcome values match public-api.md. Negative WKUWPResult values carry an originating failed HRESULT unchanged; positive values use the table mapping. A Starting callback error therefore preserves a negative handler HRESULT as Platform/ErrorCode, or maps a positive result before building that terminal event. Success is exactly zero, not any nonnegative value. Network values 1..12 retain existing adapter meaning; add BufferBudgetExceeded=13 and RequestQueueExceeded=14. Initialization returns originating platform failure separately in its diagnostic/async mapping, not invented CURLcode.

| Capability uint64 bit | Meaning |
|---|---|
| 0x001 | Structured navigation/state notifications |
| 0x002 | Opaque-origin host HTML, preserving-warning overload semantics |
| 0x004 | Independent ephemeral sessions |
| 0x008 | CPU BGRA paint |
| 0x010 | Engine-owned GPU compositor |
| 0x020 | Host clipboard service |
| 0x040 | Opaque certificate decisions |
| 0x080 | Pointer/keyboard/focus input |
| 0x100 | Immutable per-session CA-file-only trust policy |
| 0x200 | Process-global canonical font configuration/conflict checking |

Public control requires 0x001/002/004/040/080/100/200 and at least CPU or GPU. Clipboard/GPU may be absent with explicit unsupported behavior. All V1 table slots exist; absent optional features return Unsupported rather than a null call.

## Page configuration and snapshot schema

```c
typedef struct WKUWPPlatformServices WKUWPPlatformServices;
typedef struct {
    WKUWPHeader header;
    WKUWPPath fontDirectory;
    WKUWPString fontFingerprint;
    const WKUWPPlatformServices* platformServices;
} WKUWPEngineOptions;
typedef struct {
    WKUWPHeader header;
    WKUWPString caBundle;          /* copied PEM bytes, native trust excluded */
    WKUWPString trustFingerprint;  /* SHA-256 ASCII hex of caBundle */
    uint32_t maximumConcurrentRequests;
    uint64_t maximumResourceBytes;
    uint64_t maximumBufferedBytes;
    uint32_t requestTimeoutMilliseconds;
    uint32_t maximumQueuedRequests; /* V1=128 */
    uint32_t maximumRedirects;      /* V1=10 */
} WKUWPPageOptions;
typedef struct {
    WKUWPHeader header;
    uint64_t pageId, documentGeneration, activeNavigationId;
    uint32_t canGoBack, canGoForward, isLoading, widthCssPixels, heightCssPixels;
    int32_t scrollX, scrollY, contentWidth, contentHeight;
    WKUWPString uri, title;
    uint32_t hasHttpStatus, httpStatus;
} WKUWPPageState;
typedef struct {
    WKUWPHeader header;
    uint32_t contentKind;           /* Remote=0, HostHtml=1 */
    WKUWPString uri, html;
    WKUWPCertificateToken warningToken;
} WKUWPNavigationRequest;
```

Engine verifies the CA fingerprint and copies bundle bytes; no singleton mutation. Font fingerprint is computed over sorted canonical font files/content, checked before page creation; conflicting process-global font config returns 25. Options are immutable; ranges/counters/deadlines are exactly public-api.md's policy. initialize is idempotent only for identical frozen font configuration and platform service identity; joining initialization does not retain the services context twice. Successful module/engine initialization is process-lifetime; after beginShutdown new initialize/page creation fails ShuttingDown, not a new global setup attempt.

getState uses caller-owned string storage. It first computes needed bytes; on BufferTooSmall only requiredBytes is written, state/storage untouched. On success uri/title spans point into that caller buffer, valid until caller changes/frees it. No hidden borrowed title that expires on the next engine change. Other failed operations leave out objects untouched; requiredBytes is the stated exception.

navigate requires Remote, valid URI, empty html/zero warningToken. loadHtml requires HostHtml, empty URI (engine selects about:blank), text span, and either zero token (invalidate current decision) or validated exact warning token (preserve). Accepted commands return Ok and allocated ID, even if subsequent synchronous policy produces a terminal cancellation; argument/pre-admission failures allocate no ID. IDs do not wrap/reuse; exhaustion returns InvalidState before Starting.

## Callback schema and context ownership

```c
typedef struct {
    WKUWPHeader header;
    uint64_t pageId, callbackSequence, navigationId, documentGeneration;
    uint32_t eventKind, contentKind, outcome, errorDomain;
    int32_t errorCode;
    uint32_t httpStatus, hasHttpStatus, changeKind;
    uint32_t canGoBack, canGoForward, isLoading;
    WKUWPString uri, title;
    WKUWPCertificateToken certificateToken;
} WKUWPPageEvent;
typedef void (__stdcall* WKUWPRetainContext)(void*);
typedef void (__stdcall* WKUWPReleaseContext)(void*);
typedef WKUWPResult (__stdcall* WKUWPStarting)(void*, const WKUWPPageEvent*, uint32_t* cancel);
typedef void (__stdcall* WKUWPNotify)(void*, const WKUWPPageEvent*);
typedef struct {
    WKUWPHeader header;
    void* context;
    WKUWPRetainContext retainContext;
    WKUWPReleaseContext releaseContext;
    WKUWPStarting starting;
    WKUWPNotify notify;
} WKUWPPageCallbacks;
```

eventKind: Starting=0, SourceChanged=1, HistoryChanged=2, TitleChanged=3, Completed=4, RepaintRequested=5. Inapplicable fields/spans/token are zero. Starting sets input cancel=0; callback returns Ok plus Boolean, or failure converted to Failed/Platform unless cancellation/closing already wins. For handler-originated HRESULT diagnostics the host records the exact HRESULT; no exception crosses ABI. Source/title/repaint with no started navigation use navigationId=0 and a document generation. Completed includes contentKind/outcome/domain/status/token. Spans last only for callback duration. Token belongs to the issuing page/session/revision, not the numeric navigation ID. Callback sequence is strictly increasing per page.

setCallbacks copies table, calls retainContext once on owner thread before install, and rejects replacing an attachment until detach acknowledgment. Engine maintains one attachment reference plus dispatch guards. Detach marks inactive immediately; queued work stores page/generation, never a bare host pointer. It returns Ok when fully detached or Pending if called in a dispatch. After active dispatch unwinds, releaseContext is called exactly once; that callback is the acknowledgment that host storage can be released, with no future calls to that context. Caller releases its own setup reference separately. closePage follows the same deferred safe release.

Navigation engine callbacks execute only on owner thread. Public notification transactions/command queue follow public-api.md; payload invalidation must not drop an already-owed public terminal event. Native code never invokes dead public objects. Logging is observational and outside this mandatory semantic table.

## Platform service and completion schema

```c
typedef void (__stdcall* WKUWPClipboardCompletion)(void* completionContext,
    uint64_t operationId, WKUWPResult, const WKUWPString* text);
struct WKUWPPlatformServices {
    WKUWPHeader header;
    void* context;
    WKUWPRetainContext retainContext;
    WKUWPReleaseContext releaseContext;
    WKUWPResult (__stdcall* requestClipboard)(void*, WKUWPPageHandle,
        uint64_t operationId, uint32_t kind, const WKUWPString* text,
        WKUWPClipboardCompletion, void* completionContext);
    WKUWPResult (__stdcall* cancelClipboard)(void*, uint64_t operationId);
    WKUWPResult (__stdcall* postWake)(void*);
};
```

Clipboard kind Read=0/Write=1. All functions/completions except postWake's enqueue request execute on owner thread; postWake may be requested by worker and returns an enqueue result without invoking page/XAML operations. Services context retained once on successful initialize and released only after shutdown quiescence.

Accepted requestClipboard (Ok) must complete exactly once, including cancellation; rejected request has no later completion. Host copies Write text on acceptance. Engine retains its completionContext until completion returns; host cannot call it again afterward. Read span is borrowed during completion and engine copies it. cancelClipboard causes an accepted operation's canceled completion before releasing its host reference; it may complete synchronously. Close invalidates page deliveries but operation ownership remains until acknowledgment. Failed dispatcher enqueue enters the shutdown protocol, not an unowned delayed callback. Service table absence means Clipboard capability is absent; no fake successful clipboard operation.

## Input and rendering schema

```c
typedef struct {
    WKUWPHeader header;
    uint32_t kind, modifiers, pointerId, virtualKey, unicodeScalar;
    double xCssPixels, yCssPixels;
    int32_t wheelDeltaX, wheelDeltaY;
} WKUWPInputEvent;
typedef struct {
    WKUWPHeader header;
    uint8_t* data;
    uint64_t capacityBytes;
    uint32_t strideBytes, widthPixels, heightPixels;
} WKUWPPaintBuffer;
typedef struct {
    WKUWPHeader header;
    void* panelInspectable;
    double scaleX, scaleY;
} WKUWPCompositorOptions;
```

Input kinds Press=0/Move=1/Release=2/Wheel=3/Cancel=4/KeyDown=5/KeyUp=6/Character=7; modifiers Shift=1/Control=2/Alt=4. Points are CSS page-viewport coordinates. Wheel deltas use Win32 wheel units (120 per detent). Unicode scalar validates 0..0x10ffff excluding surrogate code points; unused members zero.

CPU mode: presenter owns D3D swap chain/upload resources; paint writes premultiplied BGRA8 into caller data for the call only, checked stride/capacity/extent. Engine retains no pixel pointer. GPU mode: engine owns ANGLE/Skia compositor and its swap-chain buffers; host owns the SwapChainPanel visual, NOT a second swap chain. attachCompositor borrows an IInspectable ABI pointer for the call and engine QIs/ AddRefs any retained interfaces. It releases them on detach/close on owner thread; success means engine attached the only swap chain. renderCompositor presents that engine surface. Switch modes by detach/release before CPU presenter attaches; lost-device recovery never attaches both. M10 does not migrate ANGLE ownership by renaming the native presenter. Scale finite 0<scale<=8; physical buffer edges<=4096, CSS viewport edges<=2048, BGRA stride<=16384; invalid ranges fail before allocation.

## Exact engine function table

```c
typedef struct {
    WKUWPHeader header;
    uint32_t abiMajor, abiMinor;
    uint64_t capabilities;
    WKUWPString buildId; /* module-owned immutable bytes, process lifetime */
    WKUWPResult (__stdcall* initialize)(const WKUWPEngineOptions*);
    WKUWPResult (__stdcall* service)(void);
    WKUWPResult (__stdcall* setSuspended)(uint32_t suspended);
    WKUWPResult (__stdcall* createPage)(const WKUWPPageOptions*, WKUWPPageHandle*);
    WKUWPResult (__stdcall* closePage)(WKUWPPageHandle);
    WKUWPResult (__stdcall* setCallbacks)(WKUWPPageHandle, const WKUWPPageCallbacks*);
    WKUWPResult (__stdcall* detachCallbacks)(WKUWPPageHandle);
    WKUWPResult (__stdcall* navigate)(WKUWPPageHandle, const WKUWPNavigationRequest*, uint64_t* id);
    WKUWPResult (__stdcall* loadHtml)(WKUWPPageHandle, const WKUWPNavigationRequest*, uint64_t* id);
    WKUWPResult (__stdcall* goBack)(WKUWPPageHandle);
    WKUWPResult (__stdcall* goForward)(WKUWPPageHandle);
    WKUWPResult (__stdcall* reload)(WKUWPPageHandle);
    WKUWPResult (__stdcall* stop)(WKUWPPageHandle);
    WKUWPResult (__stdcall* getState)(WKUWPPageHandle, WKUWPPageState*, uint8_t* storage,
        uint32_t capacityBytes, uint32_t* requiredBytes);
    WKUWPResult (__stdcall* proceedWithCertificateFailure)(WKUWPPageHandle,
        const WKUWPCertificateToken*, uint64_t* retryId);
    WKUWPResult (__stdcall* sendInput)(WKUWPPageHandle, const WKUWPInputEvent*);
    WKUWPResult (__stdcall* setFocus)(WKUWPPageHandle, uint32_t focused);
    WKUWPResult (__stdcall* resize)(WKUWPPageHandle, uint32_t widthCssPixels,
        uint32_t heightCssPixels, double scaleX, double scaleY);
    WKUWPResult (__stdcall* paint)(WKUWPPageHandle, const WKUWPPaintBuffer*);
    WKUWPResult (__stdcall* attachCompositor)(WKUWPPageHandle, const WKUWPCompositorOptions*);
    WKUWPResult (__stdcall* detachCompositor)(WKUWPPageHandle);
    WKUWPResult (__stdcall* renderCompositor)(WKUWPPageHandle, double overscrollYCssPixels);
    WKUWPResult (__stdcall* beginShutdown)(void);
    WKUWPResult (__stdcall* pollShutdown)(void);
} WKUWPEngineAPI;
/* Export, not a table slot: */
WKUWPResult __stdcall WKUWPGetEngineAPI(uint32_t minimumMajor,
    uint32_t minimumMinor, uint64_t requiredCapabilities, WKUWPEngineAPI* outAPI);
```

Discovery reads caller header size/version, requires version1/size>=128, minimumMajor=1 and provider minor>=minimumMinor (V1 minor=0). It checks all required bits, writes no output on rejection and zeroes supported caller extent on success before filling the known prefix. header.structureSize is bytes actually filled; unknown future caller tails stay zero. Provider may append slots in future minor versions; consumer must check filled extent before reading them. No truncation of required slots or partial initialization. No function call before successful init except discovery; shutdown operations are idempotent and available after init.

## ARM32 expected offsets/sizes (pack 4)

| Type | Size | Required anchor offsets |
|---|---|---|
| Header / String / Path | 8 each | members 0,4 |
| CertificateToken | 16 | issuer0, decision8 |
| EngineOptions | 28 | header0, fontDirectory8, fontFingerprint16, services24 |
| PageOptions | 56 | caBundle8, trustFingerprint16, concurrency24, resourceBytes28, bufferBytes36, timeout44, queued48, redirects52 |
| PageState | 92 | pageId8, generation16, activeId24, flags32/36/40, width44/height48, scroll52/56, content60/64, uri68, title76, HTTP84/88 |
| NavigationRequest | 44 | kind8, uri12, html20, token28 |
| PageEvent | 116 | pageId8, sequence16, navigation24, generation32, kind40, content44, outcome48, domain52, code56, HTTP60/64, change68, flags72/76/80, uri84, title92, token100 |
| PageCallbacks | 28 | context8, retain12, release16, starting20, notify24 |
| PlatformServices | 32 | context8, retain12, release16, request20, cancel24, wake28 |
| InputEvent | 52 | kind8, modifiers12, pointer16, key20, scalar24, x28, y36, wheel44/48 |
| PaintBuffer | 32 | data8, capacity12, stride20, width24, height28 |
| CompositorOptions | 28 | panel8, scaleX12, scaleY20 |
| EngineAPI | 128 | major8, minor12, capabilities16, buildId24, initialize32; slot n at32+4n, pollShutdown124 |

Check ALL members with static_assert/offsetof on both ARM compilers, including unaligned-under-pack4 scalar accesses. No compiler output may silently redefine the documented contract. Per-struct incoming minimum equals V1 size; callbacks check payload header before reading. Invalid span/handle/version errors precede side effects. Engine/host serializers are unnecessary; this is in-process ABI.

## Shutdown, dispatcher loss and module pinning

An owner-thread runtime registry, independent of any control, owns page cleanup, platform completions and the service pump. Closing a view does not FreeLibrary. After successful binding/initialization, WebCore.dll remains pinned for PROCESS LIFETIME in M10; hot-unload/global-engine reinitialization is deferred. Per-page resources must still release; process pinning is not permission to retain views.

Normal final release enqueues a native cleanup ticket (no strong CX control cycle). Registry owns ticket before enqueue; failure cannot drop its page/context. If wake posting fails or dispatcher stops, mark registry shutting down, suppress public notifications and invoke controlled owner-thread shutdown while still available. App host shutdown hooks drain this protocol before destroying the dispatcher.

beginShutdown rejects new pages/commands, invalidates decision tokens, cancels workers, detaches page callbacks, cancels/acknowledges platform requests and revokes global wake/clipboard hooks. pollShutdown returns Pending until dispatch, worker, platform and cleanup-ticket counts reach zero; then releases page/GPU/service contexts and returns Ok. Do not block the UI waiting for its own queued callbacks.

If the OS has irreversibly destroyed the owner thread before shutdown can run, retain orphan native tickets in the process runtime registry, suppress all app dispatch and keep module pinned until process termination. Never execute wrong- thread XAML teardown or reuse orphan contexts. This explicit terminal-shutdown fallback is not a normal Close path; report it in diagnostics. During ordinary close/recreate while dispatcher lives, all ticket/page counts return to baseline.

## Conformance before implementation

Task 3.0 freezes the complete schema with ARM compiler layout probes and review of all function/ownership annotations before producing/consuming headers. Verify table negotiation, stale handle/token, strings after dispatch, reentrant detach, exactly-once platform completion, CPU/GPU exclusive attachment, failed wake and controlled shutdown. Public event delivery traces follow public-api.md, not logs.
