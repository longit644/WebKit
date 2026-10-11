/*
 * WKW10MPage.h - frozen V1 page creation, configuration and state contracts.
 *
 * Companion to WKW10MEngine.h; see that header for the boundary rules that apply
 * to every structure here. The machine-readable authority is
 * openspec/changes/wkview-m10-uwp-cppcx-restructure/artifact/native-abi.md.
 */

#ifndef WKW10M_PAGE_H
#define WKW10M_PAGE_H

#include "WKW10MEngine.h"

#pragma pack(push, 4)

/*
 * Per-page creation options.
 *
 * Every field is immutable for the life of the page. The provider validates
 * ranges before creating anything, and copies caBundle so the caller may free
 * its buffer immediately. Trust policy is per session: no process-wide SSL
 * setting is read or written, and TLS or connection reuse never crosses
 * sessions, because reuse would silently apply another page's trust decision.
 */
typedef struct WKW10MPageOptions {
    WKW10MHeader header;

    /* PEM bytes copied at creation; replaces native OS trust for this page. */
    WKW10MString caBundle;

    /* ASCII hex SHA-256 of caBundle, verified so trust policy is provable. */
    WKW10MString trustFingerprint;

    /* Accepted range 1..16 simultaneous transfers; default 8. */
    uint32_t maximumConcurrentRequests;

    /* Accepted range 64 KiB..64 MiB decoded body bytes; default 32 MiB. */
    uint64_t maximumResourceBytes;

    /* Accepted range 1 MiB..32 MiB undelivered backlog; default 16 MiB. */
    uint64_t maximumBufferedBytes;

    /* Accepted range 1000..120000 milliseconds; default 30000. */
    uint32_t requestTimeoutMilliseconds;

    /* Requests allowed to wait for a transfer slot; V1 value 128. */
    uint32_t maximumQueuedRequests;

    /* Redirect hops allowed per request; V1 value 10. */
    uint32_t maximumRedirects;
} WKW10MPageOptions;

/*
 * Snapshot of page state.
 *
 * uri and title never point into engine-owned storage. getState writes them into
 * the caller's buffer and the spans stay valid until the caller changes or frees
 * that buffer, so a title can never change under a consumer that still holds an
 * earlier snapshot.
 */
typedef struct WKW10MPageState {
    WKW10MHeader header;

    /* This page's identity; unique for the process lifetime. */
    uint64_t pageId;

    /* Increments on every committed new document, never on same-document change. */
    uint64_t documentGeneration;

    /* Navigation currently loading, or 0 when none is outstanding. */
    uint64_t activeNavigationId;

    /* Engine history availability, reported as 0 or 1. */
    uint32_t canGoBack;
    uint32_t canGoForward;

    /* 1 while a main navigation is outstanding. */
    uint32_t isLoading;

    /* Viewport in CSS pixels. */
    uint32_t widthCssPixels;
    uint32_t heightCssPixels;

    /* Scroll offset in CSS pixels. */
    int32_t scrollX;
    int32_t scrollY;

    /* Full document extent in CSS pixels; larger than the viewport when scrollable. */
    int32_t contentWidth;
    int32_t contentHeight;

    /* Committed main-document URI, written into the caller's storage. */
    WKW10MString uri;

    /* Committed document title, empty when the document reported none. */
    WKW10MString title;

    /* 1 when a main-document response supplied httpStatus. */
    uint32_t hasHttpStatus;

    /* HTTP status of the committed main-document response; 0 when absent. */
    uint32_t httpStatus;
} WKW10MPageState;

#pragma pack(pop)

#endif /* WKW10M_PAGE_H */

