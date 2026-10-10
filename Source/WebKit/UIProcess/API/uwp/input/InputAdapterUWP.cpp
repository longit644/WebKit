#include "InputAdapterUWP.h"

#include <cmath>

namespace WebKit {
namespace {

// Drag threshold in CSS pixels before a press becomes a scroll gesture.
constexpr double tapSlopCssPixels = 10.0;
// Smallest motion delta that refreshes the fling recency timestamp.
constexpr double minimumMotionDeltaCssPixels = 0.2;
// Platform wheel units per notch.
constexpr int wheelDeltaPerNotch = 120;
// CSS pixels scrolled per wheel notch.
constexpr double wheelCssPixelsPerNotch = 80.0;

} // namespace

// Enforce owning-thread access before touching gesture state.
HRESULT InputAdapterUWP::checkThread() const
{
    return GetCurrentThreadId() == m_owner ? S_OK : RPC_E_WRONG_THREAD;
}

// Convert one WHEEL_DELTA-unit rotation into vertical CSS pixels.
double InputAdapterUWP::wheelDeltaToCssPixels(int wheelDelta)
{
    return -static_cast<double>(wheelDelta) * wheelCssPixelsPerNotch / wheelDeltaPerNotch;
}

// Start tracking; clear coasting/springs so a new gesture never inherits motion.
HRESULT InputAdapterUWP::press(double xCssPixels, double yCssPixels, double nowSeconds, ScrollControllerUWP& scroller)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    if (!std::isfinite(xCssPixels) || !std::isfinite(yCssPixels) || !std::isfinite(nowSeconds))
        return E_INVALIDARG;
    HRESULT result = scroller.onPress();
    if (FAILED(result))
        return result;
    m_pointerDown = true;
    m_dragging = false;
    m_lastPointerTime = m_lastMotionTime = nowSeconds;
    m_startX = m_lastX = xCssPixels;
    m_startY = m_lastY = yCssPixels;
    return S_OK;
}

// Extend the gesture: beyond slop becomes a drag whose deltas drive scrolling.
HRESULT InputAdapterUWP::move(double xCssPixels, double yCssPixels, double nowSeconds, ScrollControllerUWP& scroller)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    if (!std::isfinite(xCssPixels) || !std::isfinite(yCssPixels) || !std::isfinite(nowSeconds))
        return E_INVALIDARG;
    if (!m_pointerDown)
        return S_OK;
    m_dragging = m_dragging || std::abs(yCssPixels - m_startY) > tapSlopCssPixels
        || std::abs(xCssPixels - m_startX) > tapSlopCssPixels;
    double elapsed = nowSeconds - m_lastPointerTime;
    double delta = m_lastY - yCssPixels;
    if (m_dragging) {
        HRESULT result = scroller.addDragDelta(0, delta);
        if (FAILED(result))
            return result;
        result = scroller.sampleVelocity(delta, elapsed);
        if (FAILED(result))
            return result;
        if (std::abs(delta) > minimumMotionDeltaCssPixels)
            m_lastMotionTime = nowSeconds;
    }
    m_lastPointerTime = nowSeconds;
    m_lastX = xCssPixels;
    m_lastY = yCssPixels;
    return S_OK;
}

// End tracking: a settled release inside slop taps the engine; a live drag may fling.
HRESULT InputAdapterUWP::release(double xCssPixels, double yCssPixels, double nowSeconds,
    ScrollControllerUWP& scroller, TapEngineUWP& taps, PointerReleaseUWP& outcome)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    if (!std::isfinite(xCssPixels) || !std::isfinite(yCssPixels) || !std::isfinite(nowSeconds))
        return E_INVALIDARG;
    outcome = { };
    if (!m_pointerDown)
        return scroller.stampFrameTime(nowSeconds);
    if (!m_dragging && std::abs(yCssPixels - m_startY) <= tapSlopCssPixels
        && std::abs(xCssPixels - m_startX) <= tapSlopCssPixels) {
        // Tap coordinates ride above held edge displacement, never inside it.
        double tapY = yCssPixels - scroller.overscrollY();
        HRESULT result = taps.tapPress(xCssPixels, tapY);
        if (SUCCEEDED(result))
            result = taps.tapRelease(xCssPixels, tapY);
        if (FAILED(result))
            return result;
        outcome.tapped = true;
    }
    bool inertiaStarted = false;
    HRESULT result = scroller.armRelease(nowSeconds, m_lastMotionTime, m_dragging, inertiaStarted);
    if (FAILED(result))
        return result;
    outcome.inertiaStarted = inertiaStarted;
    m_pointerDown = false;
    return S_OK;
}

// Forget press tracking; physics abort is owned by the caller's controller.
HRESULT InputAdapterUWP::abort(ScrollControllerUWP& scroller)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    m_pointerDown = false;
    m_dragging = false;
    return scroller.abortGesture();
}

} // namespace WebKit
