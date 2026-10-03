#pragma once

class GrDirectContext;

namespace WebCore {

// The compositor publishes its Ganesh context only while its matching EGL
// context is current on the owner thread. No ownership crosses this slot.
inline GrDirectContext*& currentSkiaGPUContextUWP()
{
    static thread_local GrDirectContext* context { nullptr };
    return context;
}

}
