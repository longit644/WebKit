#pragma once

#include <array>
#include <cstddef>

namespace WebKit {

// Number of submitted warmed callbacks in one bounded pacing window.
constexpr size_t frameProfileSampleCount = 64;
// Capacity of the retained diagnostic text protocol, including its terminator.
constexpr size_t frameProfileMessageCapacity = 512;
// Seconds-to-milliseconds conversion shared by native frame timing consumers.
constexpr double millisecondsPerSecondW10M = 1000.0;

// CPU submission-stage durations in milliseconds; these are not GPU timestamp queries.
struct FrameTimingW10M {
    // CPU time spent creating/updating the upload texture.
    double uploadMS { 0 };
    // CPU time spent submitting the texture copy.
    double copyMS { 0 };
    // CPU time spent in Present, including any vsync wait.
    double presentMS { 0 };
};

// A copied distribution over a completed bounded sample window, in milliseconds.
struct FrameDistributionW10M {
    // Arithmetic mean of the measured samples.
    double averageMS { 0 };
    // Nearest-rank 95th percentile, with rank rounded upward.
    double p95MS { 0 };
    // Largest measured sample.
    double maximumMS { 0 };
};

// Completed pacing window; callback cadence is not physical display scan-out.
struct FramePacingSummaryW10M {
    // Number of warmed submitted work samples.
    unsigned workCount { 0 };
    // Number of consecutive submitted callback intervals below the idle-gap cutoff.
    unsigned intervalCount { 0 };
    // Whole callback work distribution in milliseconds.
    FrameDistributionW10M work;
    // Callback-start interval distribution in milliseconds.
    FrameDistributionW10M interval;
    // Submitted callbacks exceeding the retained 60-Hz work budget.
    unsigned overBudget { 0 };
    // Valid consecutive intervals exceeding the retained long-gap threshold.
    unsigned longIntervals { 0 };
    // Average scroll-stage CPU time in milliseconds.
    double scrollMS { 0 };
    // Average engine/service-stage CPU time in milliseconds.
    double engineMS { 0 };
    // Average rendering/presentation-stage CPU time in milliseconds.
    double renderMS { 0 };
};

// Completed CPU paint-stage window with averages/maxima in milliseconds.
struct FramePaintSummaryW10M {
    // Submitted paints accumulated across at least one second.
    unsigned count { 0 };
    // Average engine BGRA painting duration.
    double paintMS { 0 };
    // Average upload submission duration.
    double uploadMS { 0 };
    // Average copy submission duration.
    double copyMS { 0 };
    // Average Present call duration.
    double presentMS { 0 };
    // Largest paint plus upload/copy/Present total.
    double maximumMS { 0 };
};

// Instance-owned, allocation-free metrics; input timestamps are monotonic seconds.
class FrameMetricsW10M {
public:
    // Clear navigation-specific pacing samples and submission continuity.
    void resetPacing();
    // Record one callback; return true only when a complete copied window is ready.
    bool recordPacing(double started, double scrollFinished, double engineFinished, double finished, bool submitted, FramePacingSummaryW10M&);
    // Record CPU paint/submission stages; return true after eight paints and one second.
    bool recordPaint(double time, double paintMS, const FrameTimingW10M&, FramePaintSummaryW10M&);
    // Format the historical protocol in microseconds/milli-Hertz with bounded output.
    static bool formatPacing(const FramePacingSummaryW10M&, char* output, size_t capacity);

private:
    // Summarize a copied bounded sample array without reordering live observations.
    static FrameDistributionW10M summarize(std::array<double, frameProfileSampleCount>, unsigned count);
    // Clear one completed pacing window while preserving warmup/continuity.
    void clearPacingWindow();
    // Bounded warmed callback-work samples in milliseconds.
    std::array<double, frameProfileSampleCount> m_work { };
    // Bounded consecutive callback intervals in milliseconds.
    std::array<double, frameProfileSampleCount> m_intervals { };
    // Number of work samples currently collected.
    unsigned m_workCount { 0 };
    // Number of valid intervals currently collected.
    unsigned m_intervalCount { 0 };
    // Initial submitted callbacks omitted from warmed samples.
    unsigned m_warmupCount { 0 };
    // Collected callbacks above the diagnostic work budget.
    unsigned m_overBudget { 0 };
    // Collected valid intervals above the long-gap threshold.
    unsigned m_longIntervals { 0 };
    // Whether the prior callback successfully submitted a frame.
    bool m_previousSubmitted { false };
    // Prior callback-start timestamp in monotonic seconds.
    double m_previousStarted { 0 };
    // Accumulated scroll/engine/render CPU durations in milliseconds.
    double m_scrollTotal { 0 }, m_engineTotal { 0 }, m_renderTotal { 0 };
    // Number of CPU paints accumulated for stage reporting.
    unsigned m_paints { 0 };
    // Timestamp of the first CPU paint in the current stage window.
    double m_paintStarted { 0 };
    // Accumulated CPU paint/upload/copy/Present durations in milliseconds.
    double m_paintTotal { 0 }, m_uploadTotal { 0 }, m_copyTotal { 0 }, m_presentTotal { 0 };
    // Worst total CPU paint/submission duration in this stage window.
    double m_paintMaximum { 0 };
};

} // namespace WebKit

