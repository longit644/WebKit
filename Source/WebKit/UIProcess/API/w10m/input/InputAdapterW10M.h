#pragma once

#include "ScrollControllerW10M.h"

#include <windows.h>

namespace WebKit {

// Narrow tap port behind gesture classification; the engine receives press/release pairs.
class TapEngineW10M {
public:
    // Deliver a tap press at viewport CSS pixels, already offset for overscroll.
    virtual HRESULT tapPress(double xCssPixels, double yCssPixels) = 0;
    // Deliver the matching tap release.
    virtual HRESULT tapRelease(double xCssPixels, double yCssPixels) = 0;
protected:
    // Non-deleting base; ports are borrowed, never owned.
    ~TapEngineW10M() = default;
};

// Outcome of a pointer release for logging and repaint decisions.
struct PointerReleaseW10M {
    // The release completed a tap inside the slop region.
    bool tapped { false };
    // The release armed a kinetic fling.
    bool inertiaStarted { false };
};

// Per-control pointer gesture classification. Owner-thread only; no XAML event
// types cross this boundary, so host tests drive plain coordinates and times.
// Drag deltas and velocity feed the caller's ScrollControllerW10M; taps go to the
// caller's TapEngineW10M. Pointer capture itself stays with the XAML owner.
class InputAdapterW10M {
public:
    // Construct on the thread that will own all mutations.
    InputAdapterW10M() = default;
    // Gesture state has exactly one owner.
    InputAdapterW10M(const InputAdapterW10M&) = delete;
    // Prevent duplicated press/drag bookkeeping.
    InputAdapterW10M& operator=(const InputAdapterW10M&) = delete;

    // Begin press tracking at viewport CSS pixels and monotonic seconds.
    HRESULT press(double xCssPixels, double yCssPixels, double nowSeconds, ScrollControllerW10M&);
    // Classify motion, feed drag deltas/velocity and track the gesture.
    HRESULT move(double xCssPixels, double yCssPixels, double nowSeconds, ScrollControllerW10M&);
    // Finish tracking; emit a tap and/or arm inertia, then report the outcome.
    HRESULT release(double xCssPixels, double yCssPixels, double nowSeconds,
        ScrollControllerW10M&, TapEngineW10M&, PointerReleaseW10M& outcome);
    // Drop press tracking without motion or tap (pointer capture cancel).
    HRESULT abort(ScrollControllerW10M&);
    // Convert a platform wheel delta (120 units per notch) to CSS pixels.
    static double wheelDeltaToCssPixels(int wheelDelta);

    // Check owner affinity before any gesture mutation.
    HRESULT checkThread() const;
    // Whether a press is currently tracked.
    bool pointerDown() const { return m_pointerDown; }
    // Whether motion has exceeded tap slop and become a drag.
    bool dragging() const { return m_dragging; }
    // Press origin X in viewport CSS pixels for release logging.
    double startX() const { return m_startX; }
    // Press origin Y in viewport CSS pixels for release logging.
    double startY() const { return m_startY; }

private:
    // Owner thread for all gesture mutation.
    DWORD m_owner { GetCurrentThreadId() };
    // A press is tracked; a drag is motion beyond tap slop.
    bool m_pointerDown { false }, m_dragging { false };
    // Gesture origin and last motion point in viewport CSS pixels.
    double m_startX { 0 }, m_startY { 0 }, m_lastX { 0 }, m_lastY { 0 };
    // Last sample and last qualifying motion timestamps in monotonic seconds.
    double m_lastPointerTime { 0 }, m_lastMotionTime { 0 };
};
} // namespace WebKit


