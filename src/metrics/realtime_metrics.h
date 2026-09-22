#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

// ---------------------------------------------------------------------------
// RealtimeMetrics
//
// Collects per-block processing-time statistics from the audio thread without
// allocating, locking, or performing I/O.
//
// Design notes relevant to the measurement methodology:
//
//  * Every block is recorded into a fixed, log-spaced histogram. This is what
//    makes percentiles (p50 / p90 / p99 / p99.9) computable. Sampling a
//    running average at a fixed interval cannot produce them, because the
//    tail of the distribution is exactly what gets missed.
//
//  * A warm-up window is discarded before statistics start. The first blocks
//    after activation are dominated by cold caches, page faults and lazy
//    symbol resolution, and would otherwise permanently contaminate the
//    maximum. The window is expressed in frames so that it covers the same
//    amount of audio regardless of block size.
//
//  * A run is identified by runId(). Bumping it (via beginRun) resets all
//    statistics without deactivating the plugin, so repeated runs at one
//    configuration do not require reloading the plugin.
//
// Histogram layout: kDecades decades starting at kMinMs, with kBinsPerDecade
// bins per decade. The default covers 10 ns to 1 s, which spans every block
// duration that can occur in practice. Relative bin width is
// 10^(1/64) - 1 ~= 3.6 %, which bounds the resolution of reported percentiles.
// ---------------------------------------------------------------------------

struct RealtimeMetricsSnapshot {
    static constexpr int kBinsPerDecade = 64;
    static constexpr int kDecades = 8;
    static constexpr int kBinCount = kBinsPerDecade * kDecades;

    uint32_t runId = 0;
    uint32_t runMarker = 0;

    double sampleRate = 0.0;
    uint32_t maxBlockSize = 0;
    uint32_t currentBlockSize = 0;
    uint32_t channelCount = 0;

    int audioFormatBits = 0; // 32 = float, 64 = double -- what the host delivered

    // What the Sample Format parameter asked for. A mismatch with
    // audioFormatBits means the host declined the request, which is a result
    // in its own right rather than a measurement error.
    int requestedFormatBits = 0;

    double availableBlockTimeMs = 0.0;

    // Live values, useful for a meter but not for statistics.
    double lastProcessTimeMs = 0.0;

    // Statistics over the measured window (warm-up excluded).
    double minProcessTimeMs = 0.0;
    double averageProcessTimeMs = 0.0;
    double maxProcessTimeMs = 0.0;
    double realtimeLoadPercent = 0.0;

    uint64_t blocksTotal = 0;      // including warm-up
    uint64_t blocksMeasured = 0;   // excluding warm-up
    uint64_t framesTotal = 0;
    uint64_t framesMeasured = 0;
    uint64_t overloadCount = 0;

    uint64_t warmupFrames = 0;
    bool warmupComplete = false;

    uint64_t histogramUnderflow = 0;
    uint64_t histogramOverflow = 0;
    std::array<uint32_t, kBinCount> bins {};

    double delayMs = 0.0;
    uint32_t delaySamples = 0;
    double feedback = 0.0;
    double mix = 0.0;
    double outputDb = 0.0;
    double dspComplexity = 0.0;

    // ---- helpers evaluated off the audio thread -------------------------

    static double binLowerEdgeMs(int index) noexcept {
        return kMinMs * std::pow(10.0, static_cast<double>(index) /
                                           static_cast<double>(kBinsPerDecade));
    }

    static double binUpperEdgeMs(int index) noexcept {
        return binLowerEdgeMs(index + 1);
    }

    // Upper bound of the requested percentile, in milliseconds.
    // Returns the upper edge of the bin containing the percentile, so the
    // value is conservative (never understates the tail).
    double percentileMs(double fraction) const noexcept {
        if (blocksMeasured == 0) {
            return 0.0;
        }

        const double target = static_cast<double>(blocksMeasured) * fraction;
        uint64_t cumulative = histogramUnderflow;

        if (static_cast<double>(cumulative) >= target) {
            return kMinMs;
        }

        for (int i = 0; i < kBinCount; ++i) {
            cumulative += bins[i];
            if (static_cast<double>(cumulative) >= target) {
                // The bin's upper edge over-estimates by at most one bin
                // width. The exactly tracked maximum is a tighter bound for
                // the high percentiles, so never report more than that.
                const double edge = binUpperEdgeMs(i);
                return edge < maxProcessTimeMs ? edge : maxProcessTimeMs;
            }
        }

        return maxProcessTimeMs;
    }

    double loadPercent(double timeMs) const noexcept {
        return availableBlockTimeMs > 0.0 ? timeMs / availableBlockTimeMs * 100.0
                                          : 0.0;
    }

    double overloadRatePercent() const noexcept {
        return blocksMeasured > 0 ? static_cast<double>(overloadCount) /
                                        static_cast<double>(blocksMeasured) * 100.0
                                  : 0.0;
    }

    static constexpr double kMinMs = 1e-5; // 10 ns
};

class RealtimeMetrics {
public:
    static constexpr int kBinsPerDecade = RealtimeMetricsSnapshot::kBinsPerDecade;
    static constexpr int kDecades = RealtimeMetricsSnapshot::kDecades;
    static constexpr int kBinCount = RealtimeMetricsSnapshot::kBinCount;
    static constexpr double kMinMs = RealtimeMetricsSnapshot::kMinMs;

    // Audio discarded before statistics begin, in seconds.
    static constexpr double kDefaultWarmupSeconds = 2.0;

    RealtimeMetrics() = default;

    RealtimeMetrics(const RealtimeMetrics&) = delete;
    RealtimeMetrics& operator=(const RealtimeMetrics&) = delete;

    // [main-thread] Called from activate().
    void configure(double sampleRate,
                   uint32_t maxBlockSize,
                   uint32_t channelCount) noexcept {
        sampleRate_.store(sampleRate, std::memory_order_relaxed);
        maxBlockSize_.store(maxBlockSize, std::memory_order_relaxed);
        channelCount_.store(channelCount, std::memory_order_relaxed);

        const auto warmup = sampleRate > 0.0
                                ? static_cast<uint64_t>(sampleRate * warmupSeconds_)
                                : 0ULL;
        warmupFrames_.store(warmup, std::memory_order_relaxed);
    }

    void setWarmupSeconds(double seconds) noexcept {
        warmupSeconds_ = seconds < 0.0 ? 0.0 : seconds;
        const double sr = sampleRate_.load(std::memory_order_relaxed);
        warmupFrames_.store(
            sr > 0.0 ? static_cast<uint64_t>(sr * warmupSeconds_) : 0ULL,
            std::memory_order_relaxed);
    }

    double warmupSeconds() const noexcept { return warmupSeconds_; }

    // Clears every statistic and starts a new run.
    // Safe to call from the audio thread: no allocation, no locking, no I/O.
    // runId_ is stored last with release ordering so that a reader which
    // observes the new id is guaranteed to see the cleared counters.
    void beginRun(uint32_t marker) noexcept {
        lastProcessTimeMs_.store(0.0, std::memory_order_relaxed);
        minProcessTimeMs_.store(0.0, std::memory_order_relaxed);
        averageProcessTimeMs_.store(0.0, std::memory_order_relaxed);
        maxProcessTimeMs_.store(0.0, std::memory_order_relaxed);
        realtimeLoadPercent_.store(0.0, std::memory_order_relaxed);

        blocksTotal_.store(0, std::memory_order_relaxed);
        blocksMeasured_.store(0, std::memory_order_relaxed);
        framesTotal_.store(0, std::memory_order_relaxed);
        framesMeasured_.store(0, std::memory_order_relaxed);
        overloadCount_.store(0, std::memory_order_relaxed);

        histogramUnderflow_.store(0, std::memory_order_relaxed);
        histogramOverflow_.store(0, std::memory_order_relaxed);

        for (auto& bin : bins_) {
            bin.store(0, std::memory_order_relaxed);
        }

        runMarker_.store(marker, std::memory_order_relaxed);
        runId_.fetch_add(1, std::memory_order_release);
    }

    void reset() noexcept {
        sampleRate_.store(0.0, std::memory_order_relaxed);
        maxBlockSize_.store(0, std::memory_order_relaxed);
        currentBlockSize_.store(0, std::memory_order_relaxed);
        channelCount_.store(0, std::memory_order_relaxed);
        audioFormatBits_.store(0, std::memory_order_relaxed);
        requestedFormatBits_.store(0, std::memory_order_relaxed);
        availableBlockTimeMs_.store(0.0, std::memory_order_relaxed);
        warmupFrames_.store(0, std::memory_order_relaxed);

        delayMs_.store(0.0, std::memory_order_relaxed);
        delaySamples_.store(0, std::memory_order_relaxed);
        feedback_.store(0.0, std::memory_order_relaxed);
        mix_.store(0.0, std::memory_order_relaxed);
        outputDb_.store(0.0, std::memory_order_relaxed);
        dspComplexity_.store(0.0, std::memory_order_relaxed);

        beginRun(0);
    }

    // [audio-thread]
    void updateBlock(uint32_t currentBlockSize,
                     int audioFormatBits,
                     int requestedFormatBits) noexcept {
        currentBlockSize_.store(currentBlockSize, std::memory_order_relaxed);
        audioFormatBits_.store(audioFormatBits, std::memory_order_relaxed);
        requestedFormatBits_.store(requestedFormatBits, std::memory_order_relaxed);

        const double sr = sampleRate_.load(std::memory_order_relaxed);
        availableBlockTimeMs_.store(
            sr > 0.0 ? static_cast<double>(currentBlockSize) / sr * 1000.0 : 0.0,
            std::memory_order_relaxed);
    }

    // [audio-thread]
    void updateDelayParameters(double delayMs,
                               uint32_t delaySamples,
                               double feedback,
                               double mix,
                               double outputDb,
                               double dspComplexity) noexcept {
        delayMs_.store(delayMs, std::memory_order_relaxed);
        delaySamples_.store(delaySamples, std::memory_order_relaxed);
        feedback_.store(feedback, std::memory_order_relaxed);
        mix_.store(mix, std::memory_order_relaxed);
        outputDb_.store(outputDb, std::memory_order_relaxed);
        dspComplexity_.store(dspComplexity, std::memory_order_relaxed);
    }

    // [audio-thread] One call per process() invocation.
    void recordBlock(uint32_t frames, double processTimeMs) noexcept {
        lastProcessTimeMs_.store(processTimeMs, std::memory_order_relaxed);

        blocksTotal_.fetch_add(1, std::memory_order_relaxed);
        const uint64_t framesSoFar =
            framesTotal_.fetch_add(frames, std::memory_order_relaxed) + frames;

        // Discard the warm-up window entirely.
        if (framesSoFar <= warmupFrames_.load(std::memory_order_relaxed)) {
            return;
        }

        const uint64_t measured =
            blocksMeasured_.fetch_add(1, std::memory_order_relaxed) + 1;
        framesMeasured_.fetch_add(frames, std::memory_order_relaxed);

        recordHistogram(processTimeMs);
        updateMin(processTimeMs, measured);
        updateMax(processTimeMs);
        updateAverage(processTimeMs, measured);
        updateLoad(processTimeMs);
        updateOverload(processTimeMs);
    }

    uint32_t runId() const noexcept {
        return runId_.load(std::memory_order_acquire);
    }

    // [any thread] Cheap enough to call a few times per second.
    RealtimeMetricsSnapshot snapshot() const noexcept {
        RealtimeMetricsSnapshot s;

        // Acquire pairs with the release in beginRun(): observing this id
        // guarantees the counters below belong to that run.
        s.runId = runId_.load(std::memory_order_acquire);
        s.runMarker = runMarker_.load(std::memory_order_relaxed);

        s.sampleRate = sampleRate_.load(std::memory_order_relaxed);
        s.maxBlockSize = maxBlockSize_.load(std::memory_order_relaxed);
        s.currentBlockSize = currentBlockSize_.load(std::memory_order_relaxed);
        s.channelCount = channelCount_.load(std::memory_order_relaxed);
        s.audioFormatBits = audioFormatBits_.load(std::memory_order_relaxed);
        s.requestedFormatBits = requestedFormatBits_.load(std::memory_order_relaxed);
        s.availableBlockTimeMs = availableBlockTimeMs_.load(std::memory_order_relaxed);

        s.lastProcessTimeMs = lastProcessTimeMs_.load(std::memory_order_relaxed);
        s.minProcessTimeMs = minProcessTimeMs_.load(std::memory_order_relaxed);
        s.averageProcessTimeMs = averageProcessTimeMs_.load(std::memory_order_relaxed);
        s.maxProcessTimeMs = maxProcessTimeMs_.load(std::memory_order_relaxed);
        s.realtimeLoadPercent = realtimeLoadPercent_.load(std::memory_order_relaxed);

        s.blocksTotal = blocksTotal_.load(std::memory_order_relaxed);
        s.blocksMeasured = blocksMeasured_.load(std::memory_order_relaxed);
        s.framesTotal = framesTotal_.load(std::memory_order_relaxed);
        s.framesMeasured = framesMeasured_.load(std::memory_order_relaxed);
        s.overloadCount = overloadCount_.load(std::memory_order_relaxed);

        s.warmupFrames = warmupFrames_.load(std::memory_order_relaxed);
        s.warmupComplete = s.framesTotal > s.warmupFrames;

        s.histogramUnderflow = histogramUnderflow_.load(std::memory_order_relaxed);
        s.histogramOverflow = histogramOverflow_.load(std::memory_order_relaxed);

        for (int i = 0; i < kBinCount; ++i) {
            s.bins[i] = bins_[i].load(std::memory_order_relaxed);
        }

        s.delayMs = delayMs_.load(std::memory_order_relaxed);
        s.delaySamples = delaySamples_.load(std::memory_order_relaxed);
        s.feedback = feedback_.load(std::memory_order_relaxed);
        s.mix = mix_.load(std::memory_order_relaxed);
        s.outputDb = outputDb_.load(std::memory_order_relaxed);
        s.dspComplexity = dspComplexity_.load(std::memory_order_relaxed);

        return s;
    }

private:
    // One std::log10 per block. At the smallest usable block size (8 frames
    // at 192 kHz, i.e. 24000 blocks/s) this is well under 0.1 % of the audio
    // thread's budget, and it is outside the timed region in any case.
    void recordHistogram(double processTimeMs) noexcept {
        if (!(processTimeMs > 0.0)) {
            histogramUnderflow_.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        const double decades = std::log10(processTimeMs / kMinMs);
        const int index =
            static_cast<int>(std::floor(decades * static_cast<double>(kBinsPerDecade)));

        if (index < 0) {
            histogramUnderflow_.fetch_add(1, std::memory_order_relaxed);
        } else if (index >= kBinCount) {
            histogramOverflow_.fetch_add(1, std::memory_order_relaxed);
        } else {
            bins_[index].fetch_add(1, std::memory_order_relaxed);
        }
    }

    void updateMin(double processTimeMs, uint64_t measuredCount) noexcept {
        if (measuredCount == 1) {
            minProcessTimeMs_.store(processTimeMs, std::memory_order_relaxed);
            return;
        }

        double oldMin = minProcessTimeMs_.load(std::memory_order_relaxed);
        while (processTimeMs < oldMin) {
            if (minProcessTimeMs_.compare_exchange_weak(oldMin, processTimeMs,
                                                        std::memory_order_relaxed)) {
                break;
            }
        }
    }

    void updateMax(double processTimeMs) noexcept {
        double oldMax = maxProcessTimeMs_.load(std::memory_order_relaxed);
        while (processTimeMs > oldMax) {
            if (maxProcessTimeMs_.compare_exchange_weak(oldMax, processTimeMs,
                                                        std::memory_order_relaxed)) {
                break;
            }
        }
    }

    void updateAverage(double processTimeMs, uint64_t measuredCount) noexcept {
        double oldAverage = averageProcessTimeMs_.load(std::memory_order_relaxed);
        while (true) {
            const double newAverage =
                oldAverage + (processTimeMs - oldAverage) /
                                 static_cast<double>(measuredCount);
            if (averageProcessTimeMs_.compare_exchange_weak(oldAverage, newAverage,
                                                            std::memory_order_relaxed)) {
                break;
            }
        }
    }

    void updateLoad(double processTimeMs) noexcept {
        const double availableMs = availableBlockTimeMs_.load(std::memory_order_relaxed);
        realtimeLoadPercent_.store(
            availableMs > 0.0 ? processTimeMs / availableMs * 100.0 : 0.0,
            std::memory_order_relaxed);
    }

    void updateOverload(double processTimeMs) noexcept {
        const double availableMs = availableBlockTimeMs_.load(std::memory_order_relaxed);
        if (availableMs > 0.0 && processTimeMs > availableMs) {
            overloadCount_.fetch_add(1, std::memory_order_relaxed);
        }
    }

private:
    std::atomic<uint32_t> runId_ {0};
    std::atomic<uint32_t> runMarker_ {0};

    std::atomic<double> sampleRate_ {0.0};
    std::atomic<uint32_t> maxBlockSize_ {0};
    std::atomic<uint32_t> currentBlockSize_ {0};
    std::atomic<uint32_t> channelCount_ {0};
    std::atomic<int> audioFormatBits_ {0};
    std::atomic<int> requestedFormatBits_ {0};
    std::atomic<double> availableBlockTimeMs_ {0.0};

    std::atomic<double> lastProcessTimeMs_ {0.0};
    std::atomic<double> minProcessTimeMs_ {0.0};
    std::atomic<double> averageProcessTimeMs_ {0.0};
    std::atomic<double> maxProcessTimeMs_ {0.0};
    std::atomic<double> realtimeLoadPercent_ {0.0};

    std::atomic<uint64_t> blocksTotal_ {0};
    std::atomic<uint64_t> blocksMeasured_ {0};
    std::atomic<uint64_t> framesTotal_ {0};
    std::atomic<uint64_t> framesMeasured_ {0};
    std::atomic<uint64_t> overloadCount_ {0};

    std::atomic<uint64_t> warmupFrames_ {0};
    double warmupSeconds_ = kDefaultWarmupSeconds;

    std::atomic<uint64_t> histogramUnderflow_ {0};
    std::atomic<uint64_t> histogramOverflow_ {0};
    std::atomic<uint32_t> bins_[kBinCount] {};

    std::atomic<double> delayMs_ {0.0};
    std::atomic<uint32_t> delaySamples_ {0};
    std::atomic<double> feedback_ {0.0};
    std::atomic<double> mix_ {0.0};
    std::atomic<double> outputDb_ {0.0};
    std::atomic<double> dspComplexity_ {0.0};
};
