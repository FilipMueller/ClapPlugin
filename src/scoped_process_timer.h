#pragma once

#include "realtime_metrics.h"

#include <chrono>

class ScopedProcessTimer {
public:
    explicit ScopedProcessTimer(RealtimeMetrics& metrics) noexcept
        : metrics_(metrics),
          startTime_(Clock::now()) {
    }

    ScopedProcessTimer(const ScopedProcessTimer&) = delete;
    ScopedProcessTimer& operator=(const ScopedProcessTimer&) = delete;

    ~ScopedProcessTimer() noexcept {
        const auto endTime = Clock::now();

        const double elapsedMs =
            std::chrono::duration<double, std::milli>(
                endTime - startTime_
            ).count();

        metrics_.recordProcessTime(elapsedMs);
    }

private:
    using Clock = std::chrono::high_resolution_clock;

    RealtimeMetrics& metrics_;
    Clock::time_point startTime_;
};
