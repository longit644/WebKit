#include "config.h"
#include "DragImage.h"

#if PLATFORM(W10M)

#include "DragImageW10M.h"
#include "Image.h"
#include "NotImplemented.h"
#include <cmath>

namespace WebCore {

// Scaled preview size for hit-testing the drag image.
IntSize dragImageSize(DragImageRef image)
{
    if (!image)
        return {};
    auto size = image->image().size();
    return { static_cast<int>(size.width() * image->scale().width()), static_cast<int>(size.height() * image->scale().height()) };
}

// No-op: RefPtr owns both preview metadata and the retained NativeImage.
void deleteDragImage(DragImageRef)
{
}

// Returns a preview with the scales multiplied; null on non-finite input.
DragImageRef scaleDragImage(DragImageRef image, FloatSize scale)
{
    if (!image)
        return nullptr;
    FloatSize combined { image->scale().width() * scale.width(), image->scale().height() * scale.height() };
    if (!std::isfinite(combined.width()) || !std::isfinite(combined.height()) || combined.width() <= 0 || combined.height() <= 0)
        return nullptr;
    return DragImageW10M::create(Ref { image->image() }, combined, image->opacity(), image->orientation());
}

// Fades the preview toward transparent by the given fraction.
DragImageRef dissolveDragImageToFraction(DragImageRef image, float fraction)
{
    if (!image || !std::isfinite(fraction))
        return nullptr;
    return DragImageW10M::create(Ref { image->image() }, image->scale(), image->opacity() * std::clamp(fraction, 0.0f, 1.0f), image->orientation());
}

// Wraps a live image at unit scale and full opacity for dragging.
DragImageRef createDragImageFromImage(Image* image, ImageOrientation orientation, GraphicsClient*, float)
{
    if (!image)
        return nullptr;
    auto nativeImage = image->currentNativeImage();
    if (!nativeImage)
        return nullptr;
    return DragImageW10M::create(nativeImage.releaseNonNull(), { 1, 1 }, 1, orientation);
}

// Shell file icons need a StorageFile/XAML visual supplied by the host.
DragImageRef createDragImageIconForCachedImageFilename(const String&)
{
    return nullptr;
}

// Link-label visuals belong to the XAML host.
DragImageData createDragImageForLink(Element&, URL&, const String&, float)
{
    notImplemented();
    return { nullptr, nullptr };
}

// Color swatch drags are not implemented on this port.
DragImageRef createDragImageForColor(const Color&, const FloatRect&, float, Path&)
{
    notImplemented();
    return nullptr;
}

} // namespace WebCore

#endif // PLATFORM(W10M)

