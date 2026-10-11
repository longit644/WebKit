// Native-image drag preview for WebKitWebViewW10M. Transforms belong to the
// XAML/GPU presenter; no GDI handles or CPU rescaling are required.
#pragma once

#include "FloatSize.h"
#include "ImageOrientation.h"
#include "NativeImage.h"
#include <wtf/RefCounted.h>

namespace WebCore {

// Drag preview metadata; transforms belong to the XAML/GPU presenter, so no
// GDI handles or CPU rescaling live here.
class DragImageW10M final : public RefCounted<DragImageW10M> {
public:
    // Wraps an image with its drag-time scale, opacity and orientation.
    static Ref<DragImageW10M> create(Ref<NativeImage>&& image, FloatSize scale, float opacity, ImageOrientation orientation)
    {
        return adoptRef(*new DragImageW10M(WTF::move(image), scale, opacity, orientation));
    }

    // Source image shown under the finger.
    NativeImage& image() const { return m_image; }

    // DPI scale applied by the presenter at draw time.
    FloatSize scale() const { return m_scale; }

    // Preview translucency from the drag start event.
    float opacity() const { return m_opacity; }

    // EXIF orientation applied by the presenter at draw time.
    ImageOrientation orientation() const { return m_orientation; }

private:
    // Stores the drag parameters; rendering stays with the presenter.
    DragImageW10M(Ref<NativeImage>&& image, FloatSize scale, float opacity, ImageOrientation orientation)
        : m_image(WTF::move(image))
        , m_scale(scale)
        , m_opacity(opacity)
        , m_orientation(orientation)
    {
    }

    // Source image shown under the finger.
    Ref<NativeImage> m_image;

    // DPI scale applied by the presenter at draw time.
    FloatSize m_scale;

    // Preview translucency from the drag start event.
    float m_opacity;

    // EXIF orientation applied by the presenter at draw time.
    ImageOrientation m_orientation;
};

} // namespace WebCore

