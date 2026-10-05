/*
 * WKUWPNavigation.h - frozen V1 navigation request, event and callback contracts.
 *
 * Companion to WKUWPEngine.h; see that header for the boundary rules. The
 * machine-readable authority is
 * openspec/changes/wkview-m10-uwp-cppcx-restructure/artifact/native-abi.md.
 *
 * Identity model, which the public contract depends on:
 *
 *   - navigationId is allocated by the loader, is nonzero, increases monotonically
 *     per page, survives redirects, and is never reused or wrapped.
 *   - documentGeneration changes only when a new document commits, so
 *     same-document navigation never fabricates a main-navigation ID.
 *   - callbackSequence increases strictly per page and gives total order to
 *     notifications without comparing URIs or reading a mutable current ID.
 *   - a certificate decision is identified by WKUWPCertificateToken, never by a
 *     navigation ID, because two controls can hold equal local IDs.
 */

#ifndef WKUWP_NAVIGATION_H
#define WKUWP_NAVIGATION_H

#include "WKUWPEngine.h"

#pragma pack(push, 4)

/* What kind of document a navigation proposes to display. */
typedef enum {
    /* A remote http(s) document loaded through the normal engine response path. */
    WKUWPContentRemote = 0,

    /* Application-supplied markup loaded as opaque-origin about:blank. */
    WKUWPContentHostHtml = 1
} WKUWPContentKind;

/* Terminal outcome recorded once per started navigation. */
typedef enum {
    WKUWPOutcomeCompleted = 0,
    WKUWPOutcomeFailed = 1,
    WKUWPOutcomeCancelled = 2
} WKUWPOutcome;

/*
 * Error family for a failed navigation.
 *
 * The code is meaningful only inside its domain. Transport codes are CURLcode,
 * Network codes are adapter-local WKUWPNetwork values, Platform carries an
 * HRESULT and Engine identifies engine failures. Numeric equality across domains
 * means nothing, which is why cancellation is identified by this field plus code
 * rather than by the number 1 alone.
 */
typedef enum {
    WKUWPErrorDomainNone = 0,
    WKUWPErrorDomainTransport = 1,
    WKUWPErrorDomainNetwork = 2,
    WKUWPErrorDomainEngine = 3,
    WKUWPErrorDomainPlatform = 4
} WKUWPErrorDomain;

/* How a committed document's URI changed without a new document load. */
typedef enum {
    WKUWPChangeNewDocument = 0,
    WKUWPChangeFragment = 1,
    WKUWPChangePushState = 2,
    WKUWPChangeReplaceState = 3,
    WKUWPChangeHistoryTraversal = 4
} WKUWPChangeKind;

/* Notification kinds delivered through WKUWPPageCallbacks::notify. */
typedef enum {
    WKUWPEventStarting = 0,
    WKUWPEventSourceChanged = 1,
    WKUWPEventHistoryChanged = 2,
    WKUWPEventTitleChanged = 3,
    WKUWPEventCompleted = 4,
    WKUWPEventRepaintRequested = 5
} WKUWPEventKind;

/*
 * One navigation command.
 *
 * navigate requires WKUWPContentRemote with a valid URI, empty html and a zero
 * warningToken. loadHtml requires WKUWPContentHostHtml, an empty URI because the
 * engine selects about:blank, and either a zero warningToken (which invalidates
 * any pending certificate decision) or the exact current warning token, which
 * preserves it. An accepted command allocates an ID and may still end in
 * cancellation from policy; argument or pre-admission failures allocate nothing.
 */
typedef struct {
    WKUWPHeader header;

    /* WKUWPContentKind this command proposes. */
    uint32_t contentKind;

    /* Remote target URI; empty for host HTML. */
    WKUWPString uri;

    /* Application markup; empty for remote navigation. */
    WKUWPString html;

    /* Certificate decision to preserve, or zero/zero to invalidate any. */
    WKUWPCertificateToken warningToken;
} WKUWPNavigationRequest;

/*
 * One page notification.
 *
 * Spans are borrowed for the duration of the call only; a consumer that defers
 * delivery copies them. Members that do not apply to the event kind are zero, and
 * a zero certificateToken means no decision is offered.
 */
typedef struct {
    WKUWPHeader header;

    /* Page this event belongs to; never a process-global routed instance. */
    uint64_t pageId;

    /* Strictly increasing per page, giving total order across event kinds. */
    uint64_t callbackSequence;

    /* Started navigation, or 0 for notifications with no main navigation. */
    uint64_t navigationId;

    /* Committed document generation, used to classify same-document changes. */
    uint64_t documentGeneration;

    /* WKUWPEventKind for this notification. */
    uint32_t eventKind;

    /* WKUWPContentKind of the related navigation. */
    uint32_t contentKind;

    /* WKUWPOutcome, meaningful for WKUWPEventCompleted. */
    uint32_t outcome;

    /* WKUWPErrorDomain, meaningful for a failed completion. */
    uint32_t errorDomain;

    /* Domain-specific failure code; negative values carry an HRESULT. */
    int32_t errorCode;

    /* HTTP status of the committed response, or 0 when there was none. */
    uint32_t httpStatus;

    /* 1 when httpStatus came from a main-document response. */
    uint32_t hasHttpStatus;

    /* WKUWPChangeKind for source changes; 0 otherwise. */
    uint32_t changeKind;

    /* Engine history availability snapshot carried with the event. */
    uint32_t canGoBack;
    uint32_t canGoForward;

    /* Loading state snapshot carried with the event. */
    uint32_t isLoading;

    /* Related URI; borrowed for the call. */
    WKUWPString uri;

    /* Related document title; borrowed for the call. */
    WKUWPString title;

    /* Certificate decision offered with a certificate failure, else zero. */
    WKUWPCertificateToken certificateToken;
} WKUWPPageEvent;

/*
 * Synchronous navigation policy callback.
 *
 * The provider sets *cancel to 0 before the call. The consumer returns
 * WKUWPResultOk with *cancel set to allow or refuse, or a failure result, which
 * the engine maps to a failed navigation carrying that HRESULT unless
 * cancellation or closing already requires a cancelled outcome.
 */
typedef WKUWPResult(__stdcall* WKUWPStarting)(void* context, const WKUWPPageEvent* event, uint32_t* cancel);

/*
 * Asynchronous notification callback.
 *
 * Delivery is suppressed once the attachment is inactive. The consumer must not
 * throw; a handler failure is diagnostic and must not change the recorded
 * outcome or make the provider retry the same event.
 */
typedef void(__stdcall* WKUWPNotify)(void* context, const WKUWPPageEvent* event);

/*
 * Page callback table.
 *
 * The provider retains context exactly once, on the owner thread, before the
 * table becomes active. detachCallbacks deactivates immediately; releaseContext
 * then runs exactly once as the acknowledgment that host storage is free. The
 * host keeps and releases its own setup reference separately.
 */
typedef struct {
    WKUWPHeader header;

    /* Opaque consumer context; never interpreted by the engine. */
    void* context;

    /* Context lifetime callbacks; required when context is non-null. */
    WKUWPRetainContext retainContext;
    WKUWPReleaseContext releaseContext;

    /* Synchronous policy hook, invoked on the owner thread. */
    WKUWPStarting starting;

    /* Asynchronous notifications, invoked on the owner thread. */
    WKUWPNotify notify;
} WKUWPPageCallbacks;

#pragma pack(pop)

#endif /* WKUWP_NAVIGATION_H */