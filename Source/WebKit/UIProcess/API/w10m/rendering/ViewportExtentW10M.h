#pragma once

#include <cmath>

namespace WebKit {

// The frozen page table has integer CSS extents. Enclose the validated fractional
// XAML layout slot so page paint covers the complete native backing buffer.
// Invalid, non-positive or oversized extents saturate instead of casting UB.
inline unsigned enclosingCssViewportExtentW10M(double logicalExtent)
{
    if (!std::isfinite(logicalExtent) || logicalExtent <= 0)
        return 0;
    // Keep one coherent ceiling with the page-table validation limits.
    constexpr double maxCssExtent = 2048.0;
    if (logicalExtent > maxCssExtent)
        return 2048;
    return static_cast<unsigned>(std::ceil(logicalExtent));
}

} // namespace WebKit
