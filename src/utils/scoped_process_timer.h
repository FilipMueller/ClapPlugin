#pragma once

#include "metrics/realtime_metrics.h"

#include <chrono>
#include <cstdint>

// Measures the wall-clock duration of one process() call and hands it to
// RealtimeMetrics.
//
// steady_clock is used deliberately rather than high_resolution_clock: the
// latter is permitted to alias a non-monotonic system clock, which can step
// backwards when the wall clock is adjusted and would produce negative or
// absurd durations. steady_clock is guaranteed monotonic.

class ScopedProcessTimer {
public:
    ScopedProcessTimer(RealtimeMetrics& metrics, uint32_t frames) noexcept
        : metrics_(metrics),
          frames_(frames),
          startTime_(Clock::now()) {
    }

    ScopedProcessTimer(const ScopedProcessTimer&) = delete;
    ScopedProcessTimer& operator=(const ScopedProcessTimer&) = delete;

    ~ScopedProcessTimer() noexcept {
        const auto endTime = Clock::now();

        const double elapsedMs =
            std::chrono::duration<double, std::milli>(endTime - startTime_).count();

        metrics_.recordBlock(frames_, elapsedMs);
    }

private:
    using Clock = std::chrono::steady_clock;

    RealtimeMetrics& metrics_;
    uint32_t frames_;
    Clock::time_point startTime_;
};
