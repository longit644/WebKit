#include "config.h"
#include "TextureMapperCompositorW10M.h"
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
#include "SkiaGPUContextW10M.h"
#include <skia/gpu/ganesh/GrDirectContext.h>
#include <skia/gpu/ganesh/gl/GrGLDirectContext.h>
#include <skia/gpu/ganesh/gl/GrGLInterface.h>
#include <skia/gpu/ganesh/gl/egl/GrGLMakeEGLInterface.h>
#endif
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <cstdio>
#include <cmath>
#include <windows.foundation.collections.h>
#include <windows.foundation.h>
#include <wrl.h>
#include <wrl/wrappers/corewrappers.h>

namespace WebCore {
using namespace Microsoft::WRL;
using Microsoft::WRL::Wrappers::HStringReference;
namespace Foundation = ABI::Windows::Foundation;
namespace Collections = ABI::Windows::Foundation::Collections;

// GPU-cache budget and profile-window settings tuned for the ARM phone target.
constexpr size_t kSkiaResourceCacheBytes = 64 * 1024 * 1024;
// Minimum submitted frames per aggregate GPU diagnostic window.
constexpr unsigned kProfileMinFrames = 8;
// Minimum wall duration of an aggregate GPU diagnostic window, in seconds.
constexpr double kProfileMinSeconds = 1.0;
// Conversion from monotonic elapsed seconds to millisecond submission costs.
constexpr double kMillisecondsPerSecond = 1000.0;
// Conversion from monotonic elapsed seconds to diagnostic microseconds.
constexpr double kMicrosecondsPerSecond = 1000000.0;
// Capacity of one bounded GPU aggregate/resize diagnostic line.
constexpr size_t kGpuProfileMessageCapacity = 512;
// Allow panel event propagation across a few frames before persistent geometry failure recovers.
constexpr unsigned maximumSurfaceResizeAttempts = 4;
// Tolerance for ceil versus ANGLE nearest rounding at fractional panel/DPI edges.
constexpr int surfacePixelRoundingTolerance = 1;

// Eight-bit stencil mask and the default framebuffer binding required before clear.
constexpr GLuint kAllStencilBits = 0xff;
// Window EGL rendering binds the default framebuffer at index zero.
constexpr GLuint kDefaultFramebuffer = 0;
// Initial stencil value used for a complete window-surface clear.
constexpr GLint kClearStencilValue = 0;

// Snapshot background color (#EDF3FA) used for pixels not painted by a layer.
constexpr GLfloat kSnapshotBackgroundRed = 0.929f;
// Green component of the opaque snapshot background.
constexpr GLfloat kSnapshotBackgroundGreen = 0.953f;
// Blue component of the opaque snapshot background.
constexpr GLfloat kSnapshotBackgroundBlue = 0.980f;
// Alpha component for an opaque window clear.
constexpr GLfloat kOpaqueAlpha = 1.0f;

// Constructs an unattached compositor for the frame and its host log.
TextureMapperCompositorW10M::TextureMapperCompositorW10M(LocalFrame& frame, WKPageLogW10M log)
    : m_frame(frame)
    , m_log(log)
{
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    m_frequency = static_cast<double>(frequency.QuadPart);
}

// Creates the ANGLE display, GL context, Ganesh context and layer tree on the panel.
bool TextureMapperCompositorW10M::initialize(void* panel, unsigned width, unsigned height, float scale)
{
    ComPtr<IInspectable> properties;
    ComPtr<Collections::IMap<HSTRING, IInspectable*>> map;
    ComPtr<Foundation::IPropertyValueStatics> values;
    ComPtr<IInspectable> resolution;
    HRESULT result = RoActivateInstance(HStringReference(RuntimeClass_Windows_Foundation_Collections_PropertySet).Get(), &properties);
    if (FAILED(result) || FAILED(result = properties.As(&map))
        || FAILED(result = RoGetActivationFactory(HStringReference(RuntimeClass_Windows_Foundation_PropertyValue).Get(), IID_PPV_ARGS(&values)))
        || FAILED(result = values->CreateSingle(scale, &resolution))) {
        if (m_log)
            m_log("gpu-window-properties", result);
        return false;
    }
    boolean replaced;
    if (FAILED(result = map->Insert(HStringReference(L"EGLNativeWindowTypeProperty").Get(), static_cast<IInspectable*>(panel), &replaced))
        || FAILED(result = map->Insert(HStringReference(L"EGLRenderResolutionScaleProperty").Get(), resolution.Get(), &replaced))) {
        if (m_log)
            m_log("gpu-window-property-insert", result);
        return false;
    }
    m_windowProperties = properties.Detach();
    EGLint attributes[] = { EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
        EGL_PLATFORM_ANGLE_DEBUG_LAYERS_ENABLED_ANGLE, EGL_FALSE, EGL_NONE };
    EGLDisplay display = eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, attributes);
    if (display == EGL_NO_DISPLAY || !(m_display = GLDisplay::create(display))) {
        if (m_log)
            m_log("gpu-egl-display-failed", eglGetError());
        return false;
    }
    m_context = GLContext::create(*m_display, GLContext::Target::Default, nullptr, reinterpret_cast<GLNativeWindowType>(m_windowProperties));
    if (!m_context || !makeCurrent()) {
        if (m_log)
            m_log("gpu-egl-context-failed", eglGetError());
        return false;
    }
#if USE(SKIA)
    m_skiaContext = GrDirectContexts::MakeGL(GrGLInterfaces::MakeEGL());
    if (!m_skiaContext) {
        if (m_log)
            m_log("gpu-skia-ganesh-context-failed", eglGetError());
        return false;
    }

    // Ganesh resource budget for the phone GPU; tiles above this spill to CPU raster.
    m_skiaContext->setResourceCacheLimit(kSkiaResourceCacheBytes);
    currentSkiaGPUContextW10M() = m_skiaContext.get();
    if (m_log)
        m_log("gpu-paint-backend-skia-ganesh", WKPageLogTrueW10M);
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
        m_log("gpu-texturemapper-context-ready", WKPageLogFalseW10M);
        if (auto* renderer = glGetString(GL_RENDERER))
            m_log(reinterpret_cast<const char*>(renderer), WKPageLogFalseW10M);
    }
    return true;
}

// Makes the EGL context current and republishes its matching Ganesh context.
bool TextureMapperCompositorW10M::makeCurrent()
{
    if (!m_context || !m_context->makeContextCurrent())
        return false;
#if USE(SKIA)
    currentSkiaGPUContextW10M() = m_skiaContext.get();
#endif
    return true;
}

// Requests another frame for dirty layers or running animations.
bool TextureMapperCompositorW10M::needsFrame() const
{
    return m_flushRequired || m_surfaceResizePending || (m_viewport && downcast<GraphicsLayerTextureMapper>(*m_viewport).layer().descendantsOrSelfHaveRunningAnimations());
}

// Installs the page root under the offset layer used for edge overscroll.
void TextureMapperCompositorW10M::setRootLayer(GraphicsLayer* layer)
{
    m_root = layer;
    if (m_offset) {
        m_offset->removeAllChildren();
        if (m_root)
            m_offset->addChild(*m_root);
    }
    m_flushRequired = true;
    if (m_log)
        m_log("gpu-root-layer-attached", layer ? WKPageLogTrueW10M : WKPageLogFalseW10M);
}

// Rebuilds viewport geometry and scale-dependent layers for the new extent.
void TextureMapperCompositorW10M::resize(unsigned width, unsigned height, float scale)
{
    if (width != m_width || height != m_height || scale != m_scale) {
        ++m_surfaceRevision;
        m_surfaceResizeAttempts = 0;
        m_surfaceResizePending = true;
    }
    m_width = width;
    m_height = height;
    m_scale = scale;
    if (!m_viewport)
        return;
    m_viewport->setSize({ static_cast<float>(width), static_cast<float>(height) });
    m_offset->setSize(m_viewport->size());
    TransformationMatrix transform;
    transform.scale(scale);
    m_viewport->setTransform(transform);
    m_viewport->setNeedsDisplay();
    // Cached document tiles are rasterized at the old width; without an explicit
    // invalidation they survive as portrait-width content in the landscape frame
    // until a later scroll repaints them. Repaint the offset subtree and the page
    // root at the new geometry now. The viewport itself draws no content in the
    // composited-scrolling path, so invalidating it alone cannot refresh tiles.
    m_offset->setNeedsDisplay();
    if (m_root)
        m_root->setNeedsDisplay();
    m_flushRequired = true;
}

// EGL reports backing-buffer pixels, independently of the host's CSS viewport/clip values.
bool TextureMapperCompositorW10M::querySurfaceSize(int& width, int& height) const
{
    EGLSurface surface = eglGetCurrentSurface(EGL_DRAW);
    return surface != EGL_NO_SURFACE
        && eglQuerySurface(m_display->eglDisplay(), surface, EGL_WIDTH, &width)
        && eglQuerySurface(m_display->eglDisplay(), surface, EGL_HEIGHT, &height)
        && width > 0 && height > 0;
}

// ANGLE SurfaceD3D applies client resize after swap; synchronize before painting the new layout.
bool TextureMapperCompositorW10M::synchronizeSurface()
{
    int beforeWidth = 0, beforeHeight = 0;
    if (!querySurfaceSize(beforeWidth, beforeHeight)) return false;
    int targetWidth = static_cast<int>(std::ceil(m_width * m_scale));
    int targetHeight = static_cast<int>(std::ceil(m_height * m_scale));
    bool matches = std::abs(beforeWidth - targetWidth) <= surfacePixelRoundingTolerance
        && std::abs(beforeHeight - targetHeight) <= surfacePixelRoundingTolerance;
    if (matches) {
        m_surfaceWidthPixels = beforeWidth;
        m_surfaceHeightPixels = beforeHeight;
        m_surfaceResizePending = false;
        return true;
    }
    m_surfaceResizePending = true;
    if (++m_surfaceResizeAttempts > maximumSurfaceResizeAttempts) {
        if (m_log) m_log("gpu-surface-resize-unresolved", WKPageLogTrueW10M);
        return false;
    }

    // Presenting once lets ANGLE consume its owner panel event and rebuild the
    // engine-owned buffers. Caller pixels/layout are not drawn at stale extents.
    if (!eglSwapBuffers(m_display->eglDisplay(), eglGetCurrentSurface(EGL_DRAW))) return false;
    int afterWidth = 0, afterHeight = 0;
    if (!querySurfaceSize(afterWidth, afterHeight)) return false;
    matches = std::abs(afterWidth - targetWidth) <= surfacePixelRoundingTolerance
        && std::abs(afterHeight - targetHeight) <= surfacePixelRoundingTolerance;
    if (m_log) {
        char report[kGpuProfileMessageCapacity];
        sprintf_s(report, "gpu-surface-resize revision=%llu css=%ux%u scale=%.3f target=%dx%d before=%dx%d after=%dx%d attempt=%u ready=%u",
            m_surfaceRevision, m_width, m_height, m_scale, targetWidth, targetHeight,
            beforeWidth, beforeHeight, afterWidth, afterHeight, m_surfaceResizeAttempts, matches);
        m_log(report, WKPageLogFalseW10M);
    }
    m_surfaceWidthPixels = afterWidth;
    m_surfaceHeightPixels = afterHeight;
    m_surfaceResizePending = !matches;
    return true;
}

// Marks the given document region dirty in TextureMapper.
void TextureMapperCompositorW10M::invalidate(const IntRect& rect)
{
    if (m_viewport)
        m_viewport->setNeedsDisplayInRect(rect);
    m_flushRequired = true;
}

// Marks the compositor dirty when WebCore schedules a layer flush.
void TextureMapperCompositorW10M::notifyFlushRequired(const GraphicsLayer*) { m_flushRequired = true; }

// Counts host paint callbacks and preserves WebCore's normal layer painting.
void TextureMapperCompositorW10M::paintContents(const GraphicsLayer&, GraphicsContext& context, const FloatRect& clip, OptionSet<GraphicsLayerPaintBehavior>)
{
    ++m_paintCalls;
    context.save();
    context.clip(clip);
    if (auto* view = m_frame.view())
        view->paint(context, enclosingIntRect(clip));
    context.restore();
}

// Flushes dirty layers, applies overscroll and swaps; false means recover via software.
bool TextureMapperCompositorW10M::render(double overscrollY)
{
    if (!m_root || !makeCurrent())
        return false;
    if (!synchronizeSurface()) return false;
    // Owner panel notification may still be pending; retain frame demand and
    // draw only after both target axes belong to the same actual backing.
    if (m_surfaceResizePending) return true;
    auto clock = [this] { LARGE_INTEGER counter; QueryPerformanceCounter(&counter); return static_cast<double>(counter.QuadPart) / m_frequency; };
    double start = clock();
    if (!m_frames)
        m_sampleStart = start;
    m_offset->setPosition({ 0, static_cast<float>(overscrollY) });
    auto* view = m_frame.view();
    if (!view || !view->flushCompositingStateIncludingSubframes())
        return false;
    // Forced W10M compositing paints the RenderView into its own cached backing
    // store. Do not also rasterize a full viewport copy in the host layer.
    bool paintsIntoHost = !static_cast<const ScrollableArea&>(*view).usesCompositedScrolling();
    if (m_viewport->drawsContent() != paintsIntoHost) {
        m_viewport->setDrawsContent(paintsIntoHost);
        if (m_log)
            m_log("gpu-document-composited-scrolling", paintsIntoHost ? WKPageLogFalseW10M : WKPageLogTrueW10M);
    }
    m_viewport->flushCompositingStateForThisLayerOnly();
    m_offset->flushCompositingStateForThisLayerOnly();
    auto& root = downcast<GraphicsLayerTextureMapper>(*m_viewport);
    textureMapperFrameStatsW10M() = {};
    root.updateBackingStoreIncludingSubLayers(*m_mapper);
#if USE(SKIA)
    if (textureMapperFrameStatsW10M().gpuPaintCalls) {
        auto submitStart = MonotonicTime::now();
        // Native texture allocation/upload may have changed GL state while
        // tiles were recorded. Reset once and submit the whole paint batch.
        m_skiaContext->resetContext();
        m_skiaContext->flushAndSubmit(GrSyncCpu::kNo);
        ++textureMapperFrameStatsW10M().gpuSubmitCalls;
        textureMapperFrameStatsW10M().gpuSubmitSeconds += (MonotonicTime::now() - submitStart).seconds();
    }
#endif
    auto tileStats = textureMapperFrameStatsW10M();
    m_tileStats.rasterCalls += tileStats.rasterCalls;
    m_tileStats.gpuPaintCalls += tileStats.gpuPaintCalls;
    m_tileStats.gpuSubmitCalls += tileStats.gpuSubmitCalls;
    m_tileStats.rasterPixels += tileStats.rasterPixels;
    m_tileStats.rasterSeconds += tileStats.rasterSeconds;
    m_tileStats.uploadSeconds += tileStats.uploadSeconds;
    m_tileStats.gpuSubmitSeconds += tileStats.gpuSubmitSeconds;
    double flushed = clock();
    int pixelWidth = 0, pixelHeight = 0;
    EGLSurface surface = eglGetCurrentSurface(EGL_DRAW);
    if (!querySurfaceSize(pixelWidth, pixelHeight)) {
        if (m_log)
            m_log("gpu-surface-query-failed", eglGetError());
        return false;
    }

    // Clear the entire window surface regardless of state left by tile updates
    // or the previous TextureMapper stencil/clip pass.
    glBindFramebuffer(GL_FRAMEBUFFER, kDefaultFramebuffer);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glStencilMask(kAllStencilBits);
    glClearStencil(kClearStencilValue);
    glViewport(0, 0, pixelWidth, pixelHeight);
    glClearColor(kSnapshotBackgroundRed, kSnapshotBackgroundGreen, kSnapshotBackgroundBlue, kOpaqueAlpha);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    m_mapper->beginPainting();
    root.layer().applyAnimationsRecursively(MonotonicTime::now());
    root.layer().paint(*m_mapper);
    m_mapper->endPainting();
    double composited = clock();
    if (!eglSwapBuffers(m_display->eglDisplay(), surface)) {
        if (m_log)
            m_log("gpu-swap-failed", eglGetError());
        return false;
    }
    int swappedWidth = 0, swappedHeight = 0;
    if (!querySurfaceSize(swappedWidth, swappedHeight)) return false;
    // A notification arriving during swap requires another complete repaint,
    // even when the document/layers otherwise report no visual dirtiness.
    m_surfaceResizePending = swappedWidth != pixelWidth || swappedHeight != pixelHeight;
    m_surfaceWidthPixels = swappedWidth;
    m_surfaceHeightPixels = swappedHeight;
    double swapped = clock();
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        if (m_log)
            m_log("gpu-gl-error", error);
        return false;
    }
    m_flushRequired = m_surfaceResizePending;
    ++m_frames;
    m_flushMS += (flushed - start) * kMillisecondsPerSecond;
    m_compositeMS += (composited - flushed) * kMillisecondsPerSecond;
    m_swapMS += (swapped - composited) * kMillisecondsPerSecond;
    // Report once the warmed sample has enough frames and spans a second.
    if (m_frames >= kProfileMinFrames && swapped - m_sampleStart >= kProfileMinSeconds) {
        if (m_log) {
            // One host write per batch instead of opening/closing the log file
            // for every counter on the render thread.
            char summary[kGpuProfileMessageCapacity];
            sprintf_s(summary, "gpu-frame-profile n=%u flush-us=%.0f composite-us=%.0f swap-us=%.0f host=%u paint=%llu gpu-paint=%llu cpu-paint=%llu pixels=%llu paint-us=%.0f upload-us=%.0f submits=%llu submit-us=%.0f",
                m_frames, m_flushMS * kMicrosecondsPerSecond / m_frames, m_compositeMS * kMicrosecondsPerSecond / m_frames, m_swapMS * kMicrosecondsPerSecond / m_frames,
                m_paintCalls, m_tileStats.rasterCalls, m_tileStats.gpuPaintCalls, m_tileStats.rasterCalls - m_tileStats.gpuPaintCalls,
                m_tileStats.rasterPixels / m_frames, m_tileStats.rasterSeconds * kMicrosecondsPerSecond / m_frames, m_tileStats.uploadSeconds * kMicrosecondsPerSecond / m_frames,
                m_tileStats.gpuSubmitCalls, m_tileStats.gpuSubmitSeconds * kMicrosecondsPerSecond / m_frames);
            m_log(summary, WKPageLogFalseW10M);
        }
        m_frames = m_paintCalls = 0;
        m_tileStats = {};
        m_flushMS = m_compositeMS = m_swapMS = 0;
    }
    return true;
}

// Release layer/context resources on the same engine-owned rendering context.
TextureMapperCompositorW10M::~TextureMapperCompositorW10M()
{
    makeCurrent();
    m_root = nullptr;
    m_offset = nullptr;
    m_viewport = nullptr;
    m_mapper.reset();
#if USE(SKIA)
    if (currentSkiaGPUContextW10M() == m_skiaContext.get())
        currentSkiaGPUContextW10M() = nullptr;
    if (m_skiaContext)
        m_skiaContext->releaseResourcesAndAbandonContext();
    m_skiaContext.reset();
#endif
    if (m_context)
        m_context->unmakeContextCurrent();
    m_context.reset();
    if (m_windowProperties)
        m_windowProperties->Release();
}
}

