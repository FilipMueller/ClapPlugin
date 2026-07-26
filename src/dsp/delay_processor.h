#pragma once

#include "delay_parameters.h"

#include <cstdint>
#include <vector>

class DelayProcessor {
public:
    DelayProcessor() = default;

    DelayProcessor(const DelayProcessor&) = delete;
    DelayProcessor& operator=(const DelayProcessor&) = delete;

    bool prepare(double sampleRate, uint32_t maxFrameCount) noexcept;
    void release() noexcept;
    void reset() noexcept;

    bool isPrepared() const noexcept { return delayBufferSize_ > 0; }
    double sampleRate() const noexcept { return sampleRate_; }
    uint32_t delayBufferSize() const noexcept { return delayBufferSize_; }

    uint32_t computeDelaySamples(double delayMs) const noexcept;

    void processSample(float inL,
                       float inR,
                       float& outL,
                       float& outR,
                       const DelayParameters& params) noexcept;

    void processSample(double inL,
                       double inR,
                       double& outL,
                       double& outR,
                       const DelayParameters& params) noexcept;

private:
    static constexpr double maxDelaySeconds_ = 5.0;

    static float dbToGainFloat(double db) noexcept;
    static double dbToGainDouble(double db) noexcept;

    void runArtificialWorkload(double input, double dspComplexity) noexcept;

    double sampleRate_ = 44100.0;
    uint32_t delayBufferSize_ = 0;

    std::vector<float> delayBufferFloatL_;
    std::vector<float> delayBufferFloatR_;
    uint32_t writePositionFloat_ = 0;

    std::vector<double> delayBufferDoubleL_;
    std::vector<double> delayBufferDoubleR_;
    uint32_t writePositionDouble_ = 0;

    // Used only to make sure the artificial workload cannot be optimized away.
    volatile double workloadSink_ = 0.0;
};
