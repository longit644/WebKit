#include "ScrollControllerUWP.h"

#include <algorithm>
#include <cmath>

namespace WebKit {
namespace {

// Stable spring integration step in seconds across variable frame times.
constexpr double springSubstepSeconds = 1.0 / 120.0;
// Long stalls (suspend/resume) invalidate fling state.
constexpr double stallResetIntervalSeconds = 0.25;
// Largest integrated frame step in seconds for stable springs.
constexpr double maximumFrameStepSeconds = 0.05;
// Exponential fling friction factor.
constexpr double flingFriction = 4.2;
// Velocity below which coasting stops, in CSS pixels per second.
constexpr double flingStopVelocity = 20.0;
// Damped edge spring stiffness.
constexpr double springStiffness = 256.0;
// Damped edge spring damping.
constexpr double springDamping = 32.0;
// Edge snap distance below which the overscroll is considered settled.
constexpr double edgeSnapDistanceCssPixels = 0.15;
// Edge snap velocity below which the overscroll is considered settled.
constexpr double edgeSnapVelocityCssPixelsPerSecond = 2.0;
// Minimum velocity that qualifies a release as a fling, in CSS pixels per second.
constexpr double minimumFlingVelocityCssPixelsPerSecond = 60.0;
// Recency window in seconds that qualifies a release as a fling.
constexpr double flingRecencyWindowSeconds = 0.1;
// Fling boost factor applied at release.
constexpr double flingBoostFactor = 1.12;
// Clamp for boosted fling velocity, in CSS pixels per second.
constexpr double maximumBoostedVelocityCssPixelsPerSecond = 5500.0;
// Velocity estimator window in seconds.
constexpr double velocitySampleWindowSeconds = 0.2;
// Clamp for sampled velocity, in CSS pixels per second.
constexpr double maximumSampledVelocityCssPixelsPerSecond = 5000.0;
// Velocity blend time constant in seconds.
constexpr double velocityBlendTauSeconds = 0.035;
// Edge displacement below which overscroll is considered settled.
constexpr double edgeRestThresholdCssPixels = 0.1;
// Kinetic flings meet a stiffer edge than slow drags.
constexpr double resistanceKinetic = 0.2;
// Static edge resistance base, growing with held displacement.
constexpr double resistanceStaticBase = 0.4;
// Falloff distance for static edge resistance, in CSS pixels.
constexpr double resistanceFalloffCssPixels = 36.0;
// Maximum held edge displacement, in CSS pixels.
constexpr double maximumOverscrollCssPixels = 56.0;
// Fraction of fling velocity transferred into the edge spring on impact.
constexpr double kineticSpringTransfer = 0.2;

} // namespace

// Enforce owning-thread access before touching physics state.
HRESULT ScrollControllerUWP::checkThread() const
{
    return GetCurrentThreadId() == m_owner ? S_OK : RPC_E_WRONG_THREAD;
}

// Emit one stage; the sink must not throw (native no-exception domain).
void ScrollControllerUWP::report(const char* stage)
{
    if (m_report)
        m_report(stage);
}

// Clear pending deltas, velocities, springs and inertia for a new document.
HRESULT ScrollControllerUWP::reset(double nowSeconds)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    if (!std::isfinite(nowSeconds))
        return E_INVALIDARG;
    m_pendingScrollX = m_pendingScrollY = 0;
    m_velocityY = m_overscrollY = m_springVelocity = 0;
    m_inertia = m_pendingElastic = false;
    m_lastFrameTime = nowSeconds;
    return S_OK;
}

// Queue caller drag intent; elasticity is evaluated when the frame applies it.
HRESULT ScrollControllerUWP::addDragDelta(double dxCssPixels, double dyCssPixels)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    if (!std::isfinite(dxCssPixels) || !std::isfinite(dyCssPixels))
        return E_INVALIDARG;
    m_pendingScrollX += dxCssPixels;
    m_pendingScrollY += dyCssPixels;
    m_pendingElastic = true;
    return S_OK;
}

// Queue wheel intent as plain document motion, never elastic overscroll.
HRESULT ScrollControllerUWP::addWheelDelta(double dyCssPixels)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    if (!std::isfinite(dyCssPixels))
        return E_INVALIDARG;
    m_pendingScrollY += dyCssPixels;
    m_inertia = false;
    m_velocityY = 0;
    m_pendingElastic = false;
    return S_OK;
}

// Blend one recent motion sample toward the fling velocity estimate.
HRESULT ScrollControllerUWP::sampleVelocity(double deltaCssPixels, double elapsedSeconds)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    if (!std::isfinite(deltaCssPixels) || !std::isfinite(elapsedSeconds))
        return E_INVALIDARG;
    if (elapsedSeconds <= 0 || elapsedSeconds >= velocitySampleWindowSeconds)
        return S_OK;
    double sample = std::clamp(deltaCssPixels / elapsedSeconds,
        -maximumSampledVelocityCssPixelsPerSecond, maximumSampledVelocityCssPixelsPerSecond);
    double blend = 1 - std::exp(-elapsedSeconds / velocityBlendTauSeconds);
    m_velocityY += blend * (sample - m_velocityY);
    return S_OK;
}

// A new press cancels coasting and held springs before tracking begins.
HRESULT ScrollControllerUWP::onPress()
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    m_inertia = false;
    m_velocityY = 0;
    m_springVelocity = 0;
    return S_OK;
}

// Decide fling release: live drag, settled edge, enough velocity, recent motion.
HRESULT ScrollControllerUWP::armRelease(double nowSeconds, double lastMotionSeconds, bool dragging, bool& inertiaStarted)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    if (!std::isfinite(nowSeconds) || !std::isfinite(lastMotionSeconds))
        return E_INVALIDARG;
    inertiaStarted = dragging && std::abs(m_overscrollY) < edgeRestThresholdCssPixels
        && std::abs(m_velocityY) >= minimumFlingVelocityCssPixelsPerSecond
        && nowSeconds - lastMotionSeconds < flingRecencyWindowSeconds;
    m_inertia = inertiaStarted;
    if (inertiaStarted) {
        m_velocityY = std::clamp(m_velocityY * flingBoostFactor,
            -maximumBoostedVelocityCssPixelsPerSecond, maximumBoostedVelocityCssPixelsPerSecond);
        report("control-inertia-start");
    } else
        m_velocityY = 0;
    m_lastFrameTime = nowSeconds;
    return S_OK;
}

// Keep the frame clock fresh on events that carry no motion of their own.
HRESULT ScrollControllerUWP::stampFrameTime(double nowSeconds)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    if (!std::isfinite(nowSeconds))
        return E_INVALIDARG;
    m_lastFrameTime = nowSeconds;
    return S_OK;
}

// Drop pointer tracking and queued motion without scrolling the document.
HRESULT ScrollControllerUWP::abortGesture()
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    m_pendingScrollX = m_pendingScrollY = 0;
    m_inertia = false;
    m_velocityY = 0;
    return S_OK;
}

// Reverse motion first consumes held edge displacement before moving the document.
HRESULT ScrollControllerUWP::applyPending(ScrollEngineUWP& engine, bool kinetic, bool& didScroll)
{
    didScroll = false;
    int dx = static_cast<int>(m_pendingScrollX);
    int dy = static_cast<int>(m_pendingScrollY);
    m_pendingScrollX -= dx;
    m_pendingScrollY -= dy;
    if (!dx && !dy)
        return S_OK;
    if (m_overscrollY * dy > 0) {
        double consumed = std::min(std::abs(static_cast<double>(dy)), std::abs(m_overscrollY));
        consumed = std::copysign(consumed, m_overscrollY);
        m_overscrollY -= consumed;
        dy -= static_cast<int>(consumed);
    }
    ScrollSnapshotUWP before { };
    HRESULT result = engine.snapshot(before);
    if (FAILED(result))
        return result;
    // Only drag motion (or an active fling) may stretch the edge; wheel and
    // programmatic deltas clamp without overscroll, exactly like the wrapper.
    bool elastic = m_pendingElastic || kinetic;
    int maximumY = std::max(0, before.contentHeight - before.viewportHeight);
    int targetY = static_cast<int>(std::clamp<long long>(
        static_cast<long long>(before.scrollY) + dy, 0, static_cast<long long>(maximumY)));
    if (dx || targetY != before.scrollY) {
        result = engine.scroll(dx, targetY - before.scrollY);
        if (FAILED(result))
            return result;
        ScrollSnapshotUWP after { };
        result = engine.snapshot(after);
        if (FAILED(result))
            return result;
        didScroll = true;
        int unused = dy - (after.scrollY - before.scrollY);
        if (unused && elastic) {
            double resistance = kinetic ? resistanceKinetic
                : resistanceStaticBase / (1 + std::abs(m_overscrollY) / resistanceFalloffCssPixels);
            bool firstEdge = std::abs(m_overscrollY) < edgeRestThresholdCssPixels;
            m_overscrollY = std::clamp(m_overscrollY - unused * resistance,
                -maximumOverscrollCssPixels, maximumOverscrollCssPixels);
            if (firstEdge)
                report("control-edge-hit");
            if (kinetic) {
                m_springVelocity = -m_velocityY * kineticSpringTransfer;
                m_inertia = false;
                m_velocityY = 0;
            }
        }
    } else {
        int unused = dy;
        if (unused && elastic) {
            double resistance = kinetic ? resistanceKinetic
                : resistanceStaticBase / (1 + std::abs(m_overscrollY) / resistanceFalloffCssPixels);
            bool firstEdge = std::abs(m_overscrollY) < edgeRestThresholdCssPixels;
            m_overscrollY = std::clamp(m_overscrollY - unused * resistance,
                -maximumOverscrollCssPixels, maximumOverscrollCssPixels);
            if (firstEdge)
                report("control-edge-hit");
            if (kinetic) {
                m_springVelocity = -m_velocityY * kineticSpringTransfer;
                m_inertia = false;
                m_velocityY = 0;
            }
        }
    }
    return S_OK;
}

// Integrate fling/spring clocks, then apply one frame of queued document motion.
HRESULT ScrollControllerUWP::advance(double nowSeconds, bool pointerDown, ScrollEngineUWP& engine, bool& didScroll)
{
    auto owner = checkThread();
    if (FAILED(owner))
        return owner;
    if (!std::isfinite(nowSeconds))
        return E_INVALIDARG;
    didScroll = false;
    double elapsed = nowSeconds - m_lastFrameTime;
    m_lastFrameTime = nowSeconds;
    if (elapsed > stallResetIntervalSeconds) {
        // A suspend/resume gap must not resume an old fling or spring.
        m_inertia = false;
        m_velocityY = m_overscrollY = m_springVelocity = 0;
    }
    double step = std::clamp(elapsed, 0.0, maximumFrameStepSeconds);
    if (m_inertia && !pointerDown) {
        double next = m_velocityY * std::exp(-flingFriction * step);
        m_pendingScrollY += (m_velocityY - next) / flingFriction;
        m_velocityY = next;
        if (std::abs(m_velocityY) < flingStopVelocity) {
            m_inertia = false;
            m_velocityY = 0;
            report("control-inertia-stop");
        }
    }
    bool kinetic = m_inertia;
    HRESULT result = applyPending(engine, kinetic, didScroll);
    if (FAILED(result))
        return result;
    m_pendingElastic = false;
    if (!pointerDown && !m_inertia && std::abs(m_overscrollY) > 0) {
        // Small substeps keep the spring stable across variable frame times.
        for (double remaining = step; remaining > 0;) {
            double substep = std::min(remaining, springSubstepSeconds);
            m_springVelocity += (-springStiffness * m_overscrollY - springDamping * m_springVelocity) * substep;
            m_overscrollY += m_springVelocity * substep;
            remaining -= substep;
        }
        if (std::abs(m_overscrollY) < edgeSnapDistanceCssPixels
            && std::abs(m_springVelocity) < edgeSnapVelocityCssPixelsPerSecond) {
            m_overscrollY = m_springVelocity = 0;
            report("control-edge-returned");
        }
    }
    return S_OK;
}

} // namespace WebKit
