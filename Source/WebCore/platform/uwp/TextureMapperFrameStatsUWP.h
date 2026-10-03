#pragma once
#include <cstdint>

namespace WebCore {

// Owner-thread counters covering all TextureMapper graphics-layer tiles,
// including RenderLayer backings that bypass the host's paint callback.
struct TextureMapperFrameStatsUWP {
    uint64_t rasterCalls { 0 };
    uint64_t gpuPaintCalls { 0 };
    uint64_t gpuSubmitCalls { 0 };
    uint64_t rasterPixels { 0 };
    double rasterSeconds { 0 };
    double uploadSeconds { 0 };
    double gpuSubmitSeconds { 0 };
};

inline TextureMapperFrameStatsUWP& textureMapperFrameStatsUWP()
{
    static thread_local TextureMapperFrameStatsUWP stats;
    return stats;
}

}
