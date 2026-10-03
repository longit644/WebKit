#pragma once
#include "GraphicsLayerClient.h"
#include "PageUWP.h"
#include "TextureMapperFrameStatsUWP.h"
#include <wtf/RefPtr.h>
#include <memory>
#include <windows.h>
#include <inspectable.h>
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

class TextureMapperCompositorUWP final : public GraphicsLayerClient {
public:
    explicit TextureMapperCompositorUWP(LocalFrame&, WKPageLogUWP);
    ~TextureMapperCompositorUWP();
    bool initialize(void* panel, unsigned width, unsigned height, float scale);
    bool makeCurrent();
    void setRootLayer(GraphicsLayer*);
    void resize(unsigned width, unsigned height, float scale);
    void invalidate(const IntRect&);
    bool render(double overscrollY);
    bool needsFrame() const;
private:
    void notifyFlushRequired(const GraphicsLayer*) final;
    void paintContents(const GraphicsLayer&, GraphicsContext&, const FloatRect&, OptionSet<GraphicsLayerPaintBehavior>) final;
    float deviceScaleFactor() const final { return m_scale; }
    LocalFrame& m_frame;
    WKPageLogUWP m_log;
    RefPtr<GLDisplay> m_display;
    std::unique_ptr<GLContext> m_context;
    std::unique_ptr<TextureMapper> m_mapper;
#if USE(SKIA)
    sk_sp<GrDirectContext> m_skiaContext;
#endif
    RefPtr<GraphicsLayer> m_viewport;
    RefPtr<GraphicsLayer> m_offset;
    RefPtr<GraphicsLayer> m_root;
    IInspectable* m_windowProperties { nullptr };
    unsigned m_width { 0 }, m_height { 0 };
    float m_scale { 1 };
    bool m_flushRequired { true };
    unsigned m_paintCalls { 0 }, m_frames { 0 };
    TextureMapperFrameStatsUWP m_tileStats;
    double m_frequency { 1 }, m_flushMS { 0 }, m_compositeMS { 0 }, m_swapMS { 0 }, m_sampleStart { 0 };
};
}
