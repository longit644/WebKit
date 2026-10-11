/*
 * WKW10MNavigation.h - frozen V1 navigation request, event and callback contracts.
 *
 * Companion to WKW10MEngine.h; see that header for the boundary rules. The
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
 *   - a certificate decision is identified by WKW10MCertificateToken, never by a
 *     navigation ID, because two controls can hold equal local IDs.
 */

#ifndef WKW10M_NAVIGATION_H
#define WKW10M_NAVIGATION_H

#include "WKW10MEngine.h"

#pragma pack(push, 4)

/* What kind of document a navigation proposes to display. */
typedef enum {
    /* A remote http(s) document loaded through the normal engine response path. */
    WKW10MContentRemote = 0,

    /* Application-supplied markup loaded as opaque-origin about:blank. */
    WKW10MContentHostHtml = 1
} WKW10MContentKind;

/* Terminal outcome recorded once per started navigation. */
typedef enum {
    WKW10MOutcomeCompleted = 0,
    WKW10MOutcomeFailed = 1,
    WKW10MOutcomeCancelled = 2
} WKW10MOutcome;

/*
 * Error family for a failed navigation.
 *
 * The code is meaningful only inside its domain. Transport codes are CURLcode,
 * Network codes are adapter-local WKW10MNetwork values, Platform carries an
 * HRESULT and Engine identifies engine failures. Numeric equality across domains
 * means nothing, which is why cancellation is identified by this field plus code
 * rather than by the number 1 alone.
 */
typedef enum {
    WKW10MErrorDomainNone = 0,
    WKW10MErrorDomainTransport = 1,
    WKW10MErrorDomainNetwork = 2,
    WKW10MErrorDomainEngine = 3,
    WKW10MErrorDomainPlatform = 4
} WKW10MErrorDomain;

/* How a committed document's URI changed without a new document load. */
typedef enum {
    WKW10MChangeNewDocument = 0,
    WKW10MChangeFragment = 1,
    WKW10MChangePushState = 2,
    WKW10MChangeReplaceState = 3,
    WKW10MChangeHistoryTraversal = 4
} WKW10MChangeKind;

/* Notification kinds delivered through WKW10MPageCallbacks::notify. */
typedef enum {
    WKW10MEventStarting = 0,
    WKW10MEventSourceChanged = 1,
    WKW10MEventHistoryChanged = 2,
    WKW10MEventTitleChanged = 3,
    WKW10MEventCompleted = 4,
    WKW10MEventRepaintRequested = 5
} WKW10MEventKind;

/*
 * One navigation command.
 *
 * navigate requires WKW10MContentRemote with a valid URI, empty html and a zero
 * warningToken. loadHtml requires WKW10MContentHostHtml, an empty URI because the
 * engine selects about:blank, and either a zero warningToken (which invalidates
 * any pending certificate decision) or the exact current warning token, which
 * preserves it. An accepted command allocates an ID and may still end in
 * cancellation from policy; argument or pre-admission failures allocate nothing.
 */
typedef struct WKW10MNavigationRequest {
    WKW10MHeader header;

    /* WKW10MContentKind this command proposes. */
    uint32_t contentKind;

    /* Remote target URI; empty for host HTML. */
    WKW10MString uri;

    /* Application markup; empty for remote navigation. */
    WKW10MString html;

    /* Certificate decision to preserve, or zero/zero to invalidate any. */
    WKW10MCertificateToken warningToken;
} WKW10MNavigationRequest;

/*
 * One page notification.
 *
 * Spans are borrowed for the duration of the call only; a consumer that defers
 * delivery copies them. Members that do not apply to the event kind are zero, and
 * a zero certificateToken means no decision is offered.
 */
typedef struct WKW10MPageEvent {
    WKW10MHeader header;

    /* Page this event belongs to; never a process-global routed instance. */
    uint64_t pageId;

    /* Strictly increasing per page, giving total order across event kinds. */
    uint64_t callbackSequence;

    /* Started navigation, or 0 for notifications with no main navigation. */
    uint64_t navigationId;

    /* Committed document generation, used to classify same-document changes. */
    uint64_t documentGeneration;

    /* WKW10MEventKind for this notification. */
    uint32_t eventKind;

    /* WKW10MContentKind of the related navigation. */
    uint32_t contentKind;

    /* WKW10MOutcome, meaningful for WKW10MEventCompleted. */
    uint32_t outcome;

    /* WKW10MErrorDomain, meaningful for a failed completion. */
    uint32_t errorDomain;

    /* Domain-specific failure code; negative values carry an HRESULT. */
    int32_t errorCode;

    /* HTTP status of the committed response, or 0 when there was none. */
    uint32_t httpStatus;

    /* 1 when httpStatus came from a main-document response. */
    uint32_t hasHttpStatus;

    /* WKW10MChangeKind for source changes; 0 otherwise. */
    uint32_t changeKind;

    /* Engine history availability snapshot carried with the event. */
    uint32_t canGoBack;
    uint32_t canGoForward;

    /* Loading state snapshot carried with the event. */
    uint32_t isLoading;

    /* Related URI; borrowed for the call. */
    WKW10MString uri;

    /* Related document title; borrowed for the call. */
    WKW10MString title;

    /* Certificate decision offered with a certificate failure, else zero. */
    WKW10MCertificateToken certificateToken;
} WKW10MPageEvent;

/*
 * Synchronous navigation policy callback.
 *
 * The provider sets *cancel to 0 before the call. The consumer returns
 * WKW10MResultOK with *cancel set to allow or refuse, or a failure result, which
 * the engine maps to a failed navigation carrying that HRESULT unless
 * cancellation or closing already requires a cancelled outcome.
 */
typedef WKW10MResult(__stdcall* WKW10MStarting)(void* context, const WKW10MPageEvent* event, uint32_t* cancel);

/*
 * Asynchronous notification callback.
 *
 * Delivery is suppressed once the attachment is inactive. The consumer must not
 * throw; a handler failure is diagnostic and must not change the recorded
 * outcome or make the provider retry the same event.
 */
typedef void(__stdcall* WKW10MNotify)(void* context, const WKW10MPageEvent* event);

/*
 * Page callback table.
 *
 * The provider retains context exactly once, on the owner thread, before the
 * table becomes active. detachCallbacks deactivates immediately; releaseContext
 * then runs exactly once as the acknowledgment that host storage is free. The
 * host keeps and releases its own setup reference separately.
 */
typedef struct WKW10MPageCallbacks {
    WKW10MHeader header;

    /* Opaque consumer context; never interpreted by the engine. */
    void* context;

    /* Context lifetime callbacks; required when context is non-null. */
    WKW10MRetainContext retainContext;
    WKW10MReleaseContext releaseContext;

    /* Synchronous policy hook, invoked on the owner thread. */
    WKW10MStarting starting;

    /* Asynchronous notifications, invoked on the owner thread. */
    WKW10MNotify notify;
} WKW10MPageCallbacks;

#pragma pack(pop)

#endif /* WKW10M_NAVIGATION_H */

