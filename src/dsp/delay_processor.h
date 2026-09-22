#pragma once

#include "delay_parameters.h"

#include <cstdint>
#include <vector>

struct DelayCoefficients {
    uint32_t delaySamples = 1;
    int workloadIterations = 0;
    bool bypassed = false;

    float feedbackF = 0.0f;
    float mixF = 0.0f;
    float gainF = 1.0f;

    double feedbackD = 0.0;
    double mixD = 0.0;
    double gainD = 1.0;

    double dspComplexity = 0.0;
};

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

    // Derives the inner-loop coefficients. Call once per event segment.
    DelayCoefficients makeCoefficients(const DelayParameters& params) const noexcept;

    void processSample(float inL,
                       float inR,
                       float& outL,
                       float& outR,
                       const DelayCoefficients& coefficients) noexcept;

    void processSample(double inL,
                       double inR,
                       double& outL,
                       double& outR,
                       const DelayCoefficients& coefficients) noexcept;

private:
    static constexpr double maxDelaySeconds_ = 2.0;

    static uint32_t nextPowerOfTwo(uint32_t value) noexcept;
    static float dbToGainFloat(double db) noexcept;
    static double dbToGainDouble(double db) noexcept;

    void runArtificialWorkload(double input, int iterations) noexcept;

    double sampleRate_ = 44100.0;
    uint32_t delayBufferSize_ = 0;
    uint32_t delayBufferMask_ = 0;

    std::vector<float> delayBufferFloatL_;
    std::vector<float> delayBufferFloatR_;
    uint32_t writePositionFloat_ = 0;

    std::vector<double> delayBufferDoubleL_;
    std::vector<double> delayBufferDoubleR_;
    uint32_t writePositionDouble_ = 0;

    volatile double workloadSink_ = 0.0;
};
