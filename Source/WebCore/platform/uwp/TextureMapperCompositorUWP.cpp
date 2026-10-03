#include "config.h"
#include "TextureMapperCompositorUWP.h"
#include "DocumentView.h"
#include "GLContext.h"
#include "GLDisplay.h"
#include "GraphicsContext.h"
#include "GraphicsLayerTextureMapper.h"
#include "LocalFrame.h"
#include "LocalFrameView.h"
#include "TextureMapper.h"
#include "TextureMapperLayer.h"
#if USE(SKIA)
#include "SkiaGPUContextUWP.h"
#include <skia/gpu/ganesh/GrDirectContext.h>
#include <skia/gpu/ganesh/gl/GrGLDirectContext.h>
#include <skia/gpu/ganesh/gl/GrGLInterface.h>
#include <skia/gpu/ganesh/gl/egl/GrGLMakeEGLInterface.h>
#endif
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <windows.foundation.h>
#include <windows.foundation.collections.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>
#include <cstdio>

namespace WebCore {
using namespace Microsoft::WRL;
using Microsoft::WRL::Wrappers::HStringReference;
namespace Foundation = ABI::Windows::Foundation;
namespace Collections = ABI::Windows::Foundation::Collections;

TextureMapperCompositorUWP::TextureMapperCompositorUWP(LocalFrame& frame, WKPageLogUWP log)
    : m_frame(frame), m_log(log)
{
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    m_frequency = static_cast<double>(frequency.QuadPart);
}

bool TextureMapperCompositorUWP::initialize(void* panel, unsigned width, unsigned height, float scale)
{
    ComPtr<IInspectable> properties;
    ComPtr<Collections::IMap<HSTRING, IInspectable*>> map;
    ComPtr<Foundation::IPropertyValueStatics> values;
    ComPtr<IInspectable> resolution;
    HRESULT result = RoActivateInstance(HStringReference(RuntimeClass_Windows_Foundation_Collections_PropertySet).Get(), &properties);
    if (FAILED(result) || FAILED(result = properties.As(&map))
        || FAILED(result = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Foundation_PropertyValue).Get(), IID_PPV_ARGS(&values)))
        || FAILED(result = values->CreateSingle(scale, &resolution))) {
        if (m_log) m_log("gpu-window-properties", result);
        return false;
    }
    boolean replaced;
    if (FAILED(result = map->Insert(HStringReference(L"EGLNativeWindowTypeProperty").Get(), static_cast<IInspectable*>(panel), &replaced))
        || FAILED(result = map->Insert(HStringReference(L"EGLRenderResolutionScaleProperty").Get(), resolution.Get(), &replaced))) {
        if (m_log) m_log("gpu-window-property-insert", result);
        return false;
    }
    m_windowProperties = properties.Detach();
    EGLint attributes[] = { EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
        EGL_PLATFORM_ANGLE_DEBUG_LAYERS_ENABLED_ANGLE, EGL_FALSE, EGL_NONE };
    EGLDisplay display = eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, attributes);
    if (display == EGL_NO_DISPLAY || !(m_display = GLDisplay::create(display))) {
        if (m_log) m_log("gpu-egl-display-failed", eglGetError());
        return false;
    }
    m_context = GLContext::create(*m_display, GLContext::Target::Default, nullptr, reinterpret_cast<GLNativeWindowType>(m_windowProperties));
    if (!m_context || !makeCurrent()) {
        if (m_log) m_log("gpu-egl-context-failed", eglGetError());
        return false;
    }
#if USE(SKIA)
    m_skiaContext = GrDirectContexts::MakeGL(GrGLInterfaces::MakeEGL());
    if (!m_skiaContext) {
        if (m_log) m_log("gpu-skia-ganesh-context-failed", eglGetError());
        return false;
    }
    m_skiaContext->setResourceCacheLimit(64 * 1024 * 1024);
    currentSkiaGPUContextUWP() = m_skiaContext.get();
    if (m_log) m_log("gpu-paint-backend-skia-ganesh", 1);
#endif
    m_mapper = TextureMapper::create();
    m_viewport = GraphicsLayer::create(nullptr, *this);
    m_viewport->setAnchorPoint({ 0, 0, 0 });
    m_viewport->setMasksToBounds(true);
    m_viewport->setDrawsContent(true);
    m_viewport->setContentsOpaque(true);
    m_offset = GraphicsLayer::create(nullptr, *this);
    m_offset->setAnchorPoint({ 0, 0, 0 });
    m_viewport->addChild(*m_offset);
    resize(width, height, scale);
    if (m_log) {
        m_log("gpu-texturemapper-context-ready", 0);
        if (auto* renderer = glGetString(GL_RENDERER)) m_log(reinterpret_cast<const char*>(renderer), 0);
    }
    return true;
}

bool TextureMapperCompositorUWP::makeCurrent()
{
    if (!m_context || !m_context->makeContextCurrent()) return false;
#if USE(SKIA)
    currentSkiaGPUContextUWP() = m_skiaContext.get();
#endif
    return true;
}

bool TextureMapperCompositorUWP::needsFrame() const
{
    return m_flushRequired || (m_viewport && downcast<GraphicsLayerTextureMapper>(*m_viewport).layer().descendantsOrSelfHaveRunningAnimations());
}

void TextureMapperCompositorUWP::setRootLayer(GraphicsLayer* layer)
{
    m_root = layer;
    if (m_offset) {
        m_offset->removeAllChildren();
        if (m_root) m_offset->addChild(*m_root);
    }
    m_flushRequired = true;
    if (m_log) m_log("gpu-root-layer-attached", layer ? 1 : 0);
}

void TextureMapperCompositorUWP::resize(unsigned width, unsigned height, float scale)
{
    m_width = width; m_height = height; m_scale = scale;
    if (!m_viewport) return;
    m_viewport->setSize({ static_cast<float>(width), static_cast<float>(height) });
    m_offset->setSize(m_viewport->size());
    TransformationMatrix transform;
    transform.scale(scale);
    m_viewport->setTransform(transform);
    m_viewport->setNeedsDisplay();
    m_flushRequired = true;
}

void TextureMapperCompositorUWP::invalidate(const IntRect& rect)
{
    if (m_viewport) m_viewport->setNeedsDisplayInRect(rect);
    m_flushRequired = true;
}

void TextureMapperCompositorUWP::notifyFlushRequired(const GraphicsLayer*) { m_flushRequired = true; }

void TextureMapperCompositorUWP::paintContents(const GraphicsLayer&, GraphicsContext& context, const FloatRect& clip, OptionSet<GraphicsLayerPaintBehavior>)
{
    ++m_paintCalls;
    context.save();
    context.clip(clip);
    if (auto* view = m_frame.view()) view->paint(context, enclosingIntRect(clip));
    context.restore();
}

bool TextureMapperCompositorUWP::render(double overscrollY)
{
    if (!m_root || !makeCurrent()) return false;
    auto clock = [this] { LARGE_INTEGER counter; QueryPerformanceCounter(&counter); return static_cast<double>(counter.QuadPart) / m_frequency; };
    double start = clock();
    if (!m_frames) m_sampleStart = start;
    m_offset->setPosition({ 0, static_cast<float>(overscrollY) });
    auto* view = m_frame.view();
    if (!view || !view->flushCompositingStateIncludingSubframes()) return false;
    // Forced UWP compositing paints the RenderView into its own cached backing
    // store. Do not also rasterize a full viewport copy in the host layer.
    bool paintsIntoHost = !static_cast<const ScrollableArea&>(*view).usesCompositedScrolling();
    if (m_viewport->drawsContent() != paintsIntoHost) {
        m_viewport->setDrawsContent(paintsIntoHost);
        if (m_log) m_log("gpu-document-composited-scrolling", paintsIntoHost ? 0 : 1);
    }
    m_viewport->flushCompositingStateForThisLayerOnly();
    m_offset->flushCompositingStateForThisLayerOnly();
    auto& root = downcast<GraphicsLayerTextureMapper>(*m_viewport);
    textureMapperFrameStatsUWP() = { };
    root.updateBackingStoreIncludingSubLayers(*m_mapper);
#if USE(SKIA)
    if (textureMapperFrameStatsUWP().gpuPaintCalls) {
        auto submitStart = MonotonicTime::now();
        // Native texture allocation/upload may have changed GL state while
        // tiles were recorded. Reset once and submit the whole paint batch.
        m_skiaContext->resetContext();
        m_skiaContext->flushAndSubmit(GrSyncCpu::kNo);
        ++textureMapperFrameStatsUWP().gpuSubmitCalls;
        textureMapperFrameStatsUWP().gpuSubmitSeconds += (MonotonicTime::now() - submitStart).seconds();
    }
#endif
    auto tileStats = textureMapperFrameStatsUWP();
    m_tileStats.rasterCalls += tileStats.rasterCalls;
    m_tileStats.gpuPaintCalls += tileStats.gpuPaintCalls;
    m_tileStats.gpuSubmitCalls += tileStats.gpuSubmitCalls;
    m_tileStats.rasterPixels += tileStats.rasterPixels;
    m_tileStats.rasterSeconds += tileStats.rasterSeconds;
    m_tileStats.uploadSeconds += tileStats.uploadSeconds;
    m_tileStats.gpuSubmitSeconds += tileStats.gpuSubmitSeconds;
    double flushed = clock();
    EGLint pixelWidth = 0, pixelHeight = 0;
    EGLSurface surface = eglGetCurrentSurface(EGL_DRAW);
    if (!eglQuerySurface(m_display->eglDisplay(), surface, EGL_WIDTH, &pixelWidth)
        || !eglQuerySurface(m_display->eglDisplay(), surface, EGL_HEIGHT, &pixelHeight)
        || pixelWidth <= 0 || pixelHeight <= 0) {
        if (m_log) m_log("gpu-surface-query-failed", eglGetError());
        return false;
    }
    // Clear the entire window surface regardless of state left by tile updates
    // or the previous TextureMapper stencil/clip pass.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilMask(0xff);
    glClearStencil(0);
    glViewport(0, 0, pixelWidth, pixelHeight);
    glClearColor(0.929f, 0.953f, 0.980f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    m_mapper->beginPainting();
    root.layer().applyAnimationsRecursively(MonotonicTime::now());
    root.layer().paint(*m_mapper);
    m_mapper->endPainting();
    double composited = clock();
    if (!eglSwapBuffers(m_display->eglDisplay(), surface)) {
        if (m_log) m_log("gpu-swap-failed", eglGetError());
        return false;
    }
    double swapped = clock();
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) { if (m_log) m_log("gpu-gl-error", error); return false; }
    m_flushRequired = false;
    ++m_frames;
    m_flushMS += (flushed - start) * 1000;
    m_compositeMS += (composited - flushed) * 1000;
    m_swapMS += (swapped - composited) * 1000;
    if (m_frames >= 8 && swapped - m_sampleStart >= 1) {
        if (m_log) {
            // One host write per batch instead of opening/closing the log file
            // for every counter on the render thread.
            char summary[512];
            sprintf_s(summary, "gpu-frame-profile n=%u flush-us=%.0f composite-us=%.0f swap-us=%.0f host=%u paint=%llu gpu-paint=%llu cpu-paint=%llu pixels=%llu paint-us=%.0f upload-us=%.0f submits=%llu submit-us=%.0f",
                m_frames, m_flushMS * 1000 / m_frames, m_compositeMS * 1000 / m_frames, m_swapMS * 1000 / m_frames,
                m_paintCalls, m_tileStats.rasterCalls, m_tileStats.gpuPaintCalls, m_tileStats.rasterCalls - m_tileStats.gpuPaintCalls,
                m_tileStats.rasterPixels / m_frames, m_tileStats.rasterSeconds * 1000000 / m_frames, m_tileStats.uploadSeconds * 1000000 / m_frames,
                m_tileStats.gpuSubmitCalls, m_tileStats.gpuSubmitSeconds * 1000000 / m_frames);
            m_log(summary, 0);
        }
        m_frames = m_paintCalls = 0;
        m_tileStats = { };
        m_flushMS = m_compositeMS = m_swapMS = 0;
    }
    return true;
}

TextureMapperCompositorUWP::~TextureMapperCompositorUWP()
{
    makeCurrent();
    m_root = nullptr; m_offset = nullptr; m_viewport = nullptr;
    m_mapper.reset();
#if USE(SKIA)
    if (currentSkiaGPUContextUWP() == m_skiaContext.get()) currentSkiaGPUContextUWP() = nullptr;
    if (m_skiaContext) m_skiaContext->releaseResourcesAndAbandonContext();
    m_skiaContext.reset();
#endif
    if (m_context) m_context->unmakeContextCurrent();
    m_context.reset();
    if (m_windowProperties) m_windowProperties->Release();
}
}
