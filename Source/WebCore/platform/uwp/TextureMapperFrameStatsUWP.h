#pragma once
#include <cstdint>

namespace WebCore {

// Owner-thread counters covering all TextureMapper graphics-layer tiles,
// including RenderLayer backings that bypass the host's paint callback.
struct TextureMapperFrameStatsUWP {
    // Tiles rasterized into backing stores this window.
    uint64_t rasterCalls { 0 };

    // Tiles rasterized on the GPU rather than the CPU.
    uint64_t gpuPaintCalls { 0 };

    // GPU command submissions this window.
    uint64_t gpuSubmitCalls { 0 };

    // Pixels rasterized this window.
    uint64_t rasterPixels { 0 };

    // CPU raster time in seconds this window.
    double rasterSeconds { 0 };

    // Texture upload time in seconds this window.
    double uploadSeconds { 0 };

    // GPU submission time in seconds this window.
    double gpuSubmitSeconds { 0 };
};

// Owner-thread stats slot read by the compositor's profile report.
inline TextureMapperFrameStatsUWP& textureMapperFrameStatsUWP()
{
    static thread_local TextureMapperFrameStatsUWP stats;
    return stats;
}

}
