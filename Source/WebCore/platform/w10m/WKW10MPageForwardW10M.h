#pragma once

#include "WKW10MNavigation.h"
#include "WKW10MPage.h"
#include "WKW10MRendering.h"

namespace WebCore {

struct PageHostW10M;

// Dispatch one synchronous host-page proposal through the retained V1 policy callback.
WKW10MResult startEngineNavigationW10M(PageHostW10M&, uint64_t navigationId,
    uint32_t contentKind, const char* uri, uint32_t* cancel);

// Publish the actual new-document commit as a structured borrowed-span event.
void notifyEngineSourceChangedW10M(PageHostW10M&, uint32_t contentKind);

// Publish an exactly-once navigation terminal to the retained V1 callback table.
void notifyEngineNavigationCompletedW10M(PageHostW10M&, uint32_t contentKind,
    uint32_t outcome, uint32_t errorDomain = WKW10MErrorDomainNone, int32_t errorCode = 0);

// Release the retained host context when callbacks are detached or the page closes.
void releaseEngineCallbacksW10M(PageHostW10M&);

// Initialize process fonts and create/close the first negotiated page-table pages.
WKW10MResult initializeEnginePagesW10M(const WKW10MEngineOptions*);
// Create one engine page and return its monotonic opaque identity.
WKW10MResult createEnginePageW10M(const WKW10MPageOptions*, WKW10MPageHandle*);
// Close one page registered by createEnginePageW10M.
WKW10MResult closeEnginePageW10M(WKW10MPageHandle);
// Attach a retained owner-thread policy/notification table to the page.
WKW10MResult attachEngineCallbacksW10M(WKW10MPageHandle, const WKW10MPageCallbacks*);
// Detach the page callbacks and release the retained context.
WKW10MResult detachEngineCallbacksW10M(WKW10MPageHandle);
// Load borrowed explicit-length HostHtml as history-hidden substitute data.
WKW10MResult loadEngineHTMLW10M(WKW10MPageHandle, const WKW10MNavigationRequest*, uint64_t*);

// Apply checked CSS viewport/device geometry without allocating a host paint surface.
WKW10MResult resizeEnginePageW10M(WKW10MPageHandle, uint32_t, uint32_t, double, double);
// Attach one engine-owned GPU surface, retaining the panel's required native interface.
WKW10MResult attachEngineCompositorW10M(WKW10MPageHandle, const WKW10MCompositorOptions*);
// Release the engine surface and its panel reference before another attachment may start.
WKW10MResult detachEngineCompositorW10M(WKW10MPageHandle);
// Render dirty engine layers into the attached panel on the owner thread.
WKW10MResult renderEngineCompositorW10M(WKW10MPageHandle, double);

} // namespace WebCore
