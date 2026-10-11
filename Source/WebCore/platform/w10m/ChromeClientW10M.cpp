// Chrome client forwarding invalidation, scroll and layer events to the host.
// Moved unchanged from PageW10M.cpp (M10 task 4.2); see ChromeClientW10M.h.
#include "config.h"
#include "ChromeClientW10M.h"

namespace WebCore {

// Binds the chrome client to its owning host.
ChromeClientW10M::ChromeClientW10M(PageHostW10M& owner)
    : m_owner(owner)
{
}

// Marks the view dirty; the wrapper repaints on the next tick.
void ChromeClientW10M::invalidateRootView(const IntRect&)
{
    m_owner.dirty = true;
}

// Slow scroll falls back to a full invalidation (see scroll below).
void ChromeClientW10M::invalidateContentsForSlowScroll(const IntRect& rect)
{
    invalidateContentsAndRootView(rect);
}

// Repaints the whole scrolled area; the host keeps no blittable backing store.
void ChromeClientW10M::scroll(const IntSize&, const IntRect& rect, const IntRect& clip)
{
    // The TextureMapper host doesn't blit its non-composited backing store.
    // Repaint the complete scrolled area rather than only the exposed strip.
    auto dirtyRect = rect;
    dirtyRect.intersect(clip);
    invalidateContentsAndRootView(dirtyRect);
}

// Marks dirty and forwards the rect to the compositor when active.
void ChromeClientW10M::invalidateContentsAndRootView(const IntRect& rect)
{
    m_owner.dirty = true;
    if (m_owner.compositorActive)
        m_owner.compositor->invalidate(rect);
}

// Installs the layer tree in the compositor and marks dirty.
void ChromeClientW10M::attachRootGraphicsLayer(LocalFrame&, GraphicsLayer* layer)
{
    if (m_owner.compositor)
        m_owner.compositor->setRootLayer(layer);
    m_owner.dirty = true;
}

// Marks dirty; the wrapper owns the frame loop, so nothing schedules here.
void ChromeClientW10M::triggerRenderingUpdate()
{
    m_owner.dirty = true;
}

// Marks dirty and claims the update; the wrapper pumps frames on Rendering.
bool ChromeClientW10M::scheduleRenderingUpdate()
{
    m_owner.dirty = true;
    return true;
}

WTF_MAKE_TZONE_ALLOCATED_IMPL(ChromeClientW10M);

} // namespace WebCore

