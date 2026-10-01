#include "config.h"
#include "DragImage.h"

#if PLATFORM(UWP)

#include "DragImageUWP.h"
#include "Image.h"
#include "NotImplemented.h"
#include <cmath>

namespace WebCore {

IntSize dragImageSize(DragImageRef image)
{
    if (!image)
        return { };
    auto size = image->image().size();
    return { static_cast<int>(size.width() * image->scale().width()), static_cast<int>(size.height() * image->scale().height()) };
}

void deleteDragImage(DragImageRef)
{
    // RefPtr owns both preview metadata and the retained NativeImage.
}

DragImageRef scaleDragImage(DragImageRef image, FloatSize scale)
{
    if (!image)
        return nullptr;
    FloatSize combined { image->scale().width() * scale.width(), image->scale().height() * scale.height() };
    if (!std::isfinite(combined.width()) || !std::isfinite(combined.height()) || combined.width() <= 0 || combined.height() <= 0)
        return nullptr;
    return DragImageUWP::create(Ref { image->image() }, combined, image->opacity(), image->orientation());
}

DragImageRef dissolveDragImageToFraction(DragImageRef image, float fraction)
{
    if (!image || !std::isfinite(fraction))
        return nullptr;
    return DragImageUWP::create(Ref { image->image() }, image->scale(), image->opacity() * std::clamp(fraction, 0.0f, 1.0f), image->orientation());
}

DragImageRef createDragImageFromImage(Image* image, ImageOrientation orientation, GraphicsClient*, float)
{
    if (!image)
        return nullptr;
    auto nativeImage = image->currentNativeImage();
    if (!nativeImage)
        return nullptr;
    return DragImageUWP::create(nativeImage.releaseNonNull(), { 1, 1 }, 1, orientation);
}

DragImageRef createDragImageIconForCachedImageFilename(const String&)
{
    // Shell file icons need a StorageFile/XAML visual supplied by the host.
    return nullptr;
}

DragImageData createDragImageForLink(Element&, URL&, const String&, float)
{
    notImplemented(); // Link-label visuals belong to the XAML host.
    return { nullptr, nullptr };
}

DragImageRef createDragImageForColor(const Color&, const FloatRect&, float, Path&)
{
    notImplemented();
    return nullptr;
}

} // namespace WebCore

#endif // PLATFORM(UWP)
