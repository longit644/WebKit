#pragma once

#include "EmptyClients.h"
#include "PageHostW10M.h"

// Chrome client for one page. Moved from PageW10M.cpp in task 4.2.
namespace WebCore {

// Forwards invalidation, scroll and layer events to the host; the wrapper owns
// the frame loop, so nothing schedules work here. Unchanged from PageW10M.cpp
// as PageChromeW10M so rendering keeps its baseline before any behavioral
// change; the migration map's planned ChromeClientW10M name applies from this
// move.
class ChromeClientW10M final : public EmptyChromeClient {
    WTF_MAKE_TZONE_ALLOCATED(ChromeClientW10M);

public:
    // Binds the chrome client to its owning host.
    explicit ChromeClientW10M(PageHostW10M& owner);

private:
    // Marks the view dirty; the wrapper repaints on the next tick.
    void invalidateRootView(const IntRect&) final;

    // Slow scroll falls back to a full invalidation (see scroll below).
    void invalidateContentsForSlowScroll(const IntRect& rect) final;

    // Repaints the whole scrolled area; the host keeps no blittable backing store.
    void scroll(const IntSize&, const IntRect& rect, const IntRect& clip) final;

    // Marks dirty and forwards the rect to the compositor when active.
    void invalidateContentsAndRootView(const IntRect& rect) final;

    // Installs the layer tree in the compositor and marks dirty.
    void attachRootGraphicsLayer(LocalFrame&, GraphicsLayer* layer) final;

    // Marks dirty; the wrapper owns the frame loop, so nothing schedules here.
    void triggerRenderingUpdate() final;

    // Marks dirty and claims the update; the wrapper pumps frames on Rendering.
    bool scheduleRenderingUpdate() final;

    // Owning host holding the page and compositor.
    PageHostW10M& m_owner;
};

} // namespace WebCore

