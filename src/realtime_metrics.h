#pragma once

#include <atomic>
#include <cstdint>

struct RealtimeMetricsSnapshot {
    double sampleRate = 0.0;
    uint32_t maxBlockSize = 0;
    uint32_t currentBlockSize = 0;
    uint32_t channelCount = 0;

    int audioFormatBits = 0; // 32 = float, 64 = double

    double availableBlockTimeMs = 0.0;

    double lastProcessTimeMs = 0.0;
    double averageProcessTimeMs = 0.0;
    double maxProcessTimeMs = 0.0;
    double realtimeLoadPercent = 0.0;

    uint64_t processedBlocks = 0;
    uint64_t overloadCount = 0;

    double delayMs = 0.0;
    uint32_t delaySamples = 0;
    double feedback = 0.0;
    double mix = 0.0;
};

class RealtimeMetrics {
public:
    RealtimeMetrics() = default;

    RealtimeMetrics(const RealtimeMetrics&) = delete;
    RealtimeMetrics& operator=(const RealtimeMetrics&) = delete;

    void reset() noexcept {
        sampleRate_.store(0.0);
        maxBlockSize_.store(0);
        currentBlockSize_.store(0);
        channelCount_.store(0);
        audioFormatBits_.store(0);

        availableBlockTimeMs_.store(0.0);

        lastProcessTimeMs_.store(0.0);
        averageProcessTimeMs_.store(0.0);
        maxProcessTimeMs_.store(0.0);
        realtimeLoadPercent_.store(0.0);

        processedBlocks_.store(0);
        overloadCount_.store(0);

        delayMs_.store(0.0);
        delaySamples_.store(0);
        feedback_.store(0.0);
        mix_.store(0.0);
    }

    void configure(double sampleRate,
                   uint32_t maxBlockSize,
                   uint32_t channelCount) noexcept {
        sampleRate_.store(sampleRate);
        maxBlockSize_.store(maxBlockSize);
        channelCount_.store(channelCount);
    }

    void updateBlock(uint32_t currentBlockSize,
                     int audioFormatBits) noexcept {
        currentBlockSize_.store(currentBlockSize);
        audioFormatBits_.store(audioFormatBits);

        const double sr = sampleRate_.load();

        if (sr > 0.0) {
            const double availableMs =
                static_cast<double>(currentBlockSize) / sr * 1000.0;

            availableBlockTimeMs_.store(availableMs);
        } else {
            availableBlockTimeMs_.store(0.0);
        }
    }

    void updateDelayParameters(double delayMs,
                               uint32_t delaySamples,
                               double feedback,
                               double mix) noexcept {
        delayMs_.store(delayMs);
        delaySamples_.store(delaySamples);
        feedback_.store(feedback);
        mix_.store(mix);
    }

    void recordProcessTime(double processTimeMs) noexcept {
        lastProcessTimeMs_.store(processTimeMs);

        const uint64_t newBlockCount =
            processedBlocks_.fetch_add(1) + 1;

        updateAverageProcessTime(processTimeMs, newBlockCount);
        updateMaxProcessTime(processTimeMs);
        updateRealtimeLoad(processTimeMs);
        updateOverloadCounter(processTimeMs);
    }

    RealtimeMetricsSnapshot snapshot() const noexcept {
        RealtimeMetricsSnapshot s;

        s.sampleRate = sampleRate_.load();
        s.maxBlockSize = maxBlockSize_.load();
        s.currentBlockSize = currentBlockSize_.load();
        s.channelCount = channelCount_.load();

        s.audioFormatBits = audioFormatBits_.load();

        s.availableBlockTimeMs = availableBlockTimeMs_.load();

        s.lastProcessTimeMs = lastProcessTimeMs_.load();
        s.averageProcessTimeMs = averageProcessTimeMs_.load();
        s.maxProcessTimeMs = maxProcessTimeMs_.load();
        s.realtimeLoadPercent = realtimeLoadPercent_.load();

        s.processedBlocks = processedBlocks_.load();
        s.overloadCount = overloadCount_.load();

        s.delayMs = delayMs_.load();
        s.delaySamples = delaySamples_.load();
        s.feedback = feedback_.load();
        s.mix = mix_.load();

        return s;
    }

private:
    void updateAverageProcessTime(double processTimeMs,
                                  uint64_t blockCount) noexcept {
        double oldAverage = averageProcessTimeMs_.load();

        while (true) {
            const double newAverage =
                oldAverage +
                (processTimeMs - oldAverage) / static_cast<double>(blockCount);

            if (averageProcessTimeMs_.compare_exchange_weak(
                    oldAverage,
                    newAverage)) {
                break;
            }
        }
    }

    void updateMaxProcessTime(double processTimeMs) noexcept {
        double oldMax = maxProcessTimeMs_.load();

        while (processTimeMs > oldMax) {
            if (maxProcessTimeMs_.compare_exchange_weak(
                    oldMax,
                    processTimeMs)) {
                break;
            }
        }
    }

    void updateRealtimeLoad(double processTimeMs) noexcept {
        const double availableMs = availableBlockTimeMs_.load();

        if (availableMs > 0.0) {
            const double load =
                processTimeMs / availableMs * 100.0;

            realtimeLoadPercent_.store(load);
        } else {
            realtimeLoadPercent_.store(0.0);
        }
    }

    void updateOverloadCounter(double processTimeMs) noexcept {
        const double availableMs = availableBlockTimeMs_.load();

        if (availableMs > 0.0 && processTimeMs > availableMs) {
            overloadCount_.fetch_add(1);
        }
    }

private:
    std::atomic<double> sampleRate_ {0.0};
    std::atomic<uint32_t> maxBlockSize_ {0};
    std::atomic<uint32_t> currentBlockSize_ {0};
    std::atomic<uint32_t> channelCount_ {0};

    std::atomic<int> audioFormatBits_ {0};

    std::atomic<double> availableBlockTimeMs_ {0.0};

    std::atomic<double> lastProcessTimeMs_ {0.0};
    std::atomic<double> averageProcessTimeMs_ {0.0};
    std::atomic<double> maxProcessTimeMs_ {0.0};
    std::atomic<double> realtimeLoadPercent_ {0.0};

    std::atomic<uint64_t> processedBlocks_ {0};
    std::atomic<uint64_t> overloadCount_ {0};

    std::atomic<double> delayMs_ {0.0};
    std::atomic<uint32_t> delaySamples_ {0};
    std::atomic<double> feedback_ {0.0};
    std::atomic<double> mix_ {0.0};
};
