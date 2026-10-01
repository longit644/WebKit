// Native-image drag preview for WebKitWebViewUWP. Transforms belong to the
// XAML/GPU presenter; no GDI handles or CPU rescaling are required.
#pragma once

#include "FloatSize.h"
#include "ImageOrientation.h"
#include "NativeImage.h"
#include <wtf/RefCounted.h>

namespace WebCore {

class DragImageUWP final : public RefCounted<DragImageUWP> {
public:
    static Ref<DragImageUWP> create(Ref<NativeImage>&& image, FloatSize scale, float opacity, ImageOrientation orientation)
    {
        return adoptRef(*new DragImageUWP(WTF::move(image), scale, opacity, orientation));
    }

    NativeImage& image() const { return m_image; }
    FloatSize scale() const { return m_scale; }
    float opacity() const { return m_opacity; }
    ImageOrientation orientation() const { return m_orientation; }

private:
    DragImageUWP(Ref<NativeImage>&& image, FloatSize scale, float opacity, ImageOrientation orientation)
        : m_image(WTF::move(image))
        , m_scale(scale)
        , m_opacity(opacity)
        , m_orientation(orientation)
    {
    }

    Ref<NativeImage> m_image;
    FloatSize m_scale;
    float m_opacity;
    ImageOrientation m_orientation;
};

} // namespace WebCore
