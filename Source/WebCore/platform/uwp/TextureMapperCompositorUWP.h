#pragma once
#include "GraphicsLayerClient.h"
#include "PageUWP.h"
#include "TextureMapperFrameStatsUWP.h"
#include <inspectable.h>
#include <memory>
#include <windows.h>
#include <wtf/RefPtr.h>
#if USE(SKIA)
#include <skia/core/SkRefCnt.h>
class GrDirectContext;
#endif

namespace WebCore {
class GLContext;
class GLDisplay;
class GraphicsLayer;
class LocalFrame;
class TextureMapper;

// GPU compositor behind the SwapChainPanel: ANGLE D3D11 display, GL context,
// Ganesh GrDirectContext and a TextureMapper viewport layer. Engine thread only.
class TextureMapperCompositorUWP final : public GraphicsLayerClient {
public:
    // Attaches to the frame; creates nothing until initialize runs.
    explicit TextureMapperCompositorUWP(LocalFrame&, WKPageLogUWP);

    // Releases GL resources on the owning context before teardown.
    ~TextureMapperCompositorUWP();

    // Builds the ANGLE display, GL context, Ganesh context and layer tree on the panel.
    bool initialize(void* panel, unsigned width, unsigned height, float scale);

    // Makes the GL context current on the calling thread.
    bool makeCurrent();

    // Installs the page's root graphics layer under the overscroll offset layer.
    void setRootLayer(GraphicsLayer*);

    // Rebuilds the viewport and offset layers at the new size and scale.
    void resize(unsigned width, unsigned height, float scale);

    // Marks a document region dirty for the next render.
    void invalidate(const IntRect&);

    // Paints dirty layers and swaps with the given overscroll offset; reports false on GL loss.
    bool render(double overscrollY);

    // Whether animations or dirty layers need another frame.
    bool needsFrame() const;

private:
    // Read the current EGL backing extent on the owning context without guessed dimensions.
    bool querySurfaceSize(int& widthPixels, int& heightPixels) const;
    // Bring ANGLE's delayed swap-chain resize current before any viewport-dependent drawing.
    bool synchronizeSurface();
    // Flags the next render; layers repaint through paintContents.
    void notifyFlushRequired(const GraphicsLayer*) final;

    // Rasterizes one layer's contents into the mapper.
    void paintContents(const GraphicsLayer&, GraphicsContext&, const FloatRect&, OptionSet<GraphicsLayerPaintBehavior>) final;

    // Device scale applied to all layer geometry.
    float deviceScaleFactor() const final { return m_scale; }

    // Owning frame supplying the layer tree.
    LocalFrame& m_frame;

    // Diagnostic sink for GPU bring-up and per-window profiles.
    WKPageLogUWP m_log;

    // ANGLE D3D11 display behind the GL context.
    RefPtr<GLDisplay> m_display;

    // GL context current on the engine thread while compositing.
    std::unique_ptr<GLContext> m_context;

    // TextureMapper painting into the GL default framebuffer.
    std::unique_ptr<TextureMapper> m_mapper;

#if USE(SKIA)
    // Ganesh context shared with Skia raster; published while current.
    sk_sp<GrDirectContext> m_skiaContext;
#endif

    // Clipping viewport layer at the control bounds.
    RefPtr<GraphicsLayer> m_viewport;

    // Child layer shifted for edge overscroll without repainting.
    RefPtr<GraphicsLayer> m_offset;

    // Page root layer parented under the offset layer.
    RefPtr<GraphicsLayer> m_root;

    // Native window properties backing the ANGLE surface; released on teardown.
    IInspectable* m_windowProperties { nullptr };

    // Viewport size in CSS pixels; backing dimensions are queried from EGL separately.
    unsigned m_width { 0 }, m_height { 0 };

    // Device scale applied to all layer geometry.
    float m_scale { 1 };

    // Set by notifyFlushRequired; cleared after a successful render.
    bool m_flushRequired { true };
    // Keep frame demand until the actual EGL backing matches the new CSS/device geometry.
    bool m_surfaceResizePending { true };
    // Monotonic host geometry revision for batched resize/surface diagnostics.
    uint64_t m_surfaceRevision { 0 };
    // Bounded pending resize swaps; persistent mismatch reports recovery rather than spinning.
    unsigned m_surfaceResizeAttempts { 0 };
    // Actual physical extent used for the last successfully painted framebuffer.
    int m_surfaceWidthPixels { 0 }, m_surfaceHeightPixels { 0 };

    // Paint calls and presented frames in the current profile window.
    unsigned m_paintCalls { 0 }, m_frames { 0 };

    // Tile raster/upload counters summarized per profile window.
    TextureMapperFrameStatsUWP m_tileStats;

    // Performance-counter frequency plus accumulated flush/composite/swap cost and window start.
    double m_frequency { 1 }, m_flushMS { 0 }, m_compositeMS { 0 }, m_swapMS { 0 }, m_sampleStart { 0 };
};
}
