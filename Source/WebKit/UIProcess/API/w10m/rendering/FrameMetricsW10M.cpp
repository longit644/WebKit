#include "FrameMetricsW10M.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace WebKit {
namespace {

// Submitted cold shader/tile callbacks excluded from warmed pacing statistics.
constexpr unsigned warmupFrames = 8;
// Legacy diagnostic work budget; scheduling/refresh discovery is owned separately.
constexpr double displayHertz = 60.0;
// Idle intervals at or above this duration do not measure continuous presentation.
constexpr double maximumIntervalMS = 250.0;
// Long callback-start interval threshold used by the preserved report protocol.
constexpr double longIntervalMS = 25.0;
// Conversion from millisecond measurements to diagnostic microseconds.
constexpr double microsecondsPerMillisecond = 1000.0;
// Millisecond interval conversion into milli-Hertz callback cadence.
constexpr double milliHertzPerMillisecond = 1000000.0;
// Minimum CPU paint count before a stage window can be reported.
constexpr unsigned minimumPaintFrames = 8;
// Minimum wall duration of a CPU paint-stage window in monotonic seconds.
constexpr double minimumPaintSeconds = 1.0;
// Integer nearest-rank percentile numerator.
constexpr unsigned p95Percentile = 95;
// Integer percentile denominator and upward-rounding basis.
constexpr unsigned percentileBase = 100;

} // namespace

// Clear only the bounded pacing window after its copied summary is delivered.
void FrameMetricsW10M::clearPacingWindow()
{
    m_workCount = m_intervalCount = m_overBudget = m_longIntervals = 0;
    m_scrollTotal = m_engineTotal = m_renderTotal = 0;
}

// Start fresh navigation pacing without mixing stage totals with old work samples.
void FrameMetricsW10M::resetPacing()
{
    clearPacingWindow();
    m_warmupCount = 0;
    m_previousSubmitted = false;
    m_previousStarted = 0;
}

// Preserve nearest-rank p95 and calculate milliseconds over copied samples.
FrameDistributionW10M FrameMetricsW10M::summarize(std::array<double, frameProfileSampleCount> samples, unsigned count)
{
    FrameDistributionW10M result;
    if (!count) return result;
    for (unsigned i = 0; i < count; ++i) result.averageMS += samples[i];
    result.averageMS /= count;
    std::sort(samples.begin(), samples.begin() + count);
    result.p95MS = samples[(count * p95Percentile + percentileBase - 1) / percentileBase - 1];
    result.maximumMS = samples[count - 1];
    return result;
}

// Reject invalid clocks before mutation and collect only warmed submitted work.
bool FrameMetricsW10M::recordPacing(double started, double scrollFinished, double engineFinished, double finished, bool submitted, FramePacingSummaryW10M& output)
{
    if (!std::isfinite(started) || !std::isfinite(scrollFinished) || !std::isfinite(engineFinished) || !std::isfinite(finished)
        || started > scrollFinished || scrollFinished > engineFinished || engineFinished > finished
        || (m_previousStarted && started < m_previousStarted))
        return false;
    bool consecutive = m_previousSubmitted && submitted;
    double intervalMS = (started - m_previousStarted) * millisecondsPerSecondW10M;
    m_previousSubmitted = submitted;
    m_previousStarted = started;
    if (!submitted) return false;
    if (m_warmupCount < warmupFrames) { ++m_warmupCount; return false; }
    double workMS = (finished - started) * millisecondsPerSecondW10M;
    m_work[m_workCount++] = workMS;
    m_scrollTotal += (scrollFinished - started) * millisecondsPerSecondW10M;
    m_engineTotal += (engineFinished - scrollFinished) * millisecondsPerSecondW10M;
    m_renderTotal += (finished - engineFinished) * millisecondsPerSecondW10M;
    if (workMS > millisecondsPerSecondW10M / displayHertz) ++m_overBudget;
    if (consecutive && intervalMS > 0 && intervalMS < maximumIntervalMS) {
        m_intervals[m_intervalCount++] = intervalMS;
        if (intervalMS > longIntervalMS) ++m_longIntervals;
    }
    if (m_workCount < m_work.size()) return false;
    output = { m_workCount, m_intervalCount, summarize(m_work, m_workCount), summarize(m_intervals, m_intervalCount),
        m_overBudget, m_longIntervals, m_scrollTotal / m_workCount, m_engineTotal / m_workCount, m_renderTotal / m_workCount };
    clearPacingWindow();
    return true;
}

// Accumulate CPU submission duration independently of warmed callback cadence.
bool FrameMetricsW10M::recordPaint(double time, double paintMS, const FrameTimingW10M& timing, FramePaintSummaryW10M& output)
{
    if (!std::isfinite(time) || !std::isfinite(paintMS) || !std::isfinite(timing.uploadMS) || !std::isfinite(timing.copyMS)
        || !std::isfinite(timing.presentMS) || paintMS < 0 || timing.uploadMS < 0 || timing.copyMS < 0 || timing.presentMS < 0
        || (m_paints && time < m_paintStarted))
        return false;
    if (!m_paints) m_paintStarted = time;
    ++m_paints;
    m_paintTotal += paintMS;
    m_uploadTotal += timing.uploadMS;
    m_copyTotal += timing.copyMS;
    m_presentTotal += timing.presentMS;
    m_paintMaximum = std::max(m_paintMaximum, paintMS + timing.uploadMS + timing.copyMS + timing.presentMS);
    if (m_paints < minimumPaintFrames || time - m_paintStarted < minimumPaintSeconds) return false;
    output = { m_paints, m_paintTotal / m_paints, m_uploadTotal / m_paints, m_copyTotal / m_paints, m_presentTotal / m_paints, m_paintMaximum };
    m_paints = 0;
    m_paintTotal = m_uploadTotal = m_copyTotal = m_presentTotal = m_paintMaximum = 0;
    return true;
}

// Keep microsecond/milli-Hertz log units separate from the millisecond value types.
bool FrameMetricsW10M::formatPacing(const FramePacingSummaryW10M& value, char* output, size_t capacity)
{
    if (!output || !capacity) return false;
    auto length = std::snprintf(output, capacity,
        "frame-profile n=%u work-us=%.0f/%.0f/%.0f interval-n=%u interval-us=%.0f/%.0f/%.0f callback-mHz=%.0f over-budget=%u long-intervals=%u scroll-us=%.0f engine-us=%.0f render-us=%.0f",
        value.workCount, value.work.averageMS * microsecondsPerMillisecond, value.work.p95MS * microsecondsPerMillisecond, value.work.maximumMS * microsecondsPerMillisecond,
        value.intervalCount, value.interval.averageMS * microsecondsPerMillisecond, value.interval.p95MS * microsecondsPerMillisecond, value.interval.maximumMS * microsecondsPerMillisecond,
        value.interval.averageMS > 0 ? milliHertzPerMillisecond / value.interval.averageMS : 0, value.overBudget, value.longIntervals,
        value.scrollMS * microsecondsPerMillisecond, value.engineMS * microsecondsPerMillisecond, value.renderMS * microsecondsPerMillisecond);
    return length >= 0 && static_cast<size_t>(length) < capacity;
}

} // namespace WebKit

