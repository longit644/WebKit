#pragma once

#include <functional>
#include <windows.h>

namespace WebKit {

// Vertical document/engine snapshot used to clamp scroll targets without WebCore types.
struct ScrollSnapshotUWP {
    // Current vertical scroll offset in CSS pixels.
    int scrollY { 0 };
    // Full document height in CSS pixels.
    int contentHeight { 0 };
    // Viewport height in CSS pixels.
    int viewportHeight { 0 };
};

// Narrow engine port behind scroll physics; implemented by the wrapper's bound table.
class ScrollEngineUWP {
public:
    // Scroll by CSS pixels; negative values move up or left.
    virtual HRESULT scroll(int dx, int dy) = 0;
    // Snapshot the current offset, document and viewport heights.
    virtual HRESULT snapshot(ScrollSnapshotUWP&) = 0;
protected:
    // Non-deleting base; ports are borrowed, never owned.
    ~ScrollEngineUWP() = default;
};

// Per-control kinetic/edge scroll physics. Owner-thread only; no XAML, engine or
// allocation behavior beyond the caller's engine port. All timestamps are
// monotonic seconds supplied by the caller, so host tests control time exactly.
class ScrollControllerUWP {
public:
    // Construct on the thread that will own all mutations.
    ScrollControllerUWP() = default;
    // Physics state has exactly one owner.
    ScrollControllerUWP(const ScrollControllerUWP&) = delete;
    // Prevent duplicated inertia/overscroll bookkeeping.
    ScrollControllerUWP& operator=(const ScrollControllerUWP&) = delete;

    // Install the diagnostic sink; null silences edge/inertia stage reports.
    void setReportSink(std::function<void(const char*)> sink) { m_report = std::move(sink); }

    // Clear gesture/physics state on navigation; preserves no velocity or edge spring.
    HRESULT reset(double nowSeconds);
    // Accumulate a drag delta for the next frame; arms elastic edge handling.
    HRESULT addDragDelta(double dxCssPixels, double dyCssPixels);
    // Accumulate a wheel delta without elastic arming; clears inertia/velocity.
    HRESULT addWheelDelta(double dyCssPixels);
    // Blend one motion sample into the fling velocity estimate.
    HRESULT sampleVelocity(double deltaCssPixels, double elapsedSeconds);
    // A press starts tracking: no inertia, velocity or held spring.
    HRESULT onPress();
    // Arm or cancel inertia on release using drag/edge/velocity/recency rules.
    HRESULT armRelease(double nowSeconds, double lastMotionSeconds, bool dragging, bool& inertiaStarted);
    // Refresh the frame clock without motion, matching release-event stamping.
    HRESULT stampFrameTime(double nowSeconds);
    // Abort pointer tracking without inventing motion (capture cancel).
    HRESULT abortGesture();
    // Integrate inertia/spring, apply pending deltas and report whether scrolled.
    HRESULT advance(double nowSeconds, bool pointerDown, ScrollEngineUWP&, bool& didScroll);

    // Check owner affinity before any state access or engine call.
    HRESULT checkThread() const;
    // Current edge overscroll in CSS pixels; presentation-only, never document state.
    double overscrollY() const { return m_overscrollY; }
    // Whether a fling is currently coasting.
    bool hasInertia() const { return m_inertia; }

private:
    // Apply accumulated deltas through overscroll consumption and engine clamping.
    HRESULT applyPending(ScrollEngineUWP&, bool kinetic, bool& didScroll);
    // Emit one diagnostic stage without letting sink failures escape.
    void report(const char* stage);

    // Owner thread for all physics mutation.
    DWORD m_owner { GetCurrentThreadId() };
    // Optional diagnostic sink for edge/inertia stages.
    std::function<void(const char*)> m_report;
    // Scroll delta accumulated since the last engine tick, in CSS pixels.
    double m_pendingScrollX { 0 }, m_pendingScrollY { 0 };
    // Vertical fling velocity in CSS pixels per second.
    double m_velocityY { 0 };
    // Held edge displacement and its spring-back velocity, in CSS pixels(units/s).
    double m_overscrollY { 0 }, m_springVelocity { 0 };
    // Fling coasting and elastic edge armed for the next apply.
    bool m_inertia { false }, m_pendingElastic { false };
    // Last physics timestamp in monotonic seconds.
    double m_lastFrameTime { 0 };
};
} // namespace WebKit
