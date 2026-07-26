#include "delay_processor.h"

#include <algorithm>
#include <cmath>

bool DelayProcessor::prepare(double sampleRate, uint32_t maxFrameCount) noexcept {
    if (sampleRate <= 0.0 || maxFrameCount == 0) {
        return false;
    }

    sampleRate_ = sampleRate;

    const auto requestedSize =
        static_cast<uint32_t>(std::ceil(sampleRate_ * maxDelaySeconds_))
        + maxFrameCount
        + 1;

    delayBufferSize_ = std::max<uint32_t>(requestedSize, 2);

    try {
        delayBufferFloatL_.assign(delayBufferSize_, 0.0f);
        delayBufferFloatR_.assign(delayBufferSize_, 0.0f);

        delayBufferDoubleL_.assign(delayBufferSize_, 0.0);
        delayBufferDoubleR_.assign(delayBufferSize_, 0.0);
    } catch (...) {
        release();
        return false;
    }

    writePositionFloat_ = 0;
    writePositionDouble_ = 0;
    workloadSink_ = 0.0;

    return true;
}

void DelayProcessor::release() noexcept {
    delayBufferFloatL_.clear();
    delayBufferFloatR_.clear();

    delayBufferDoubleL_.clear();
    delayBufferDoubleR_.clear();

    delayBufferSize_ = 0;
    writePositionFloat_ = 0;
    writePositionDouble_ = 0;
    workloadSink_ = 0.0;
}

void DelayProcessor::reset() noexcept {
    std::fill(delayBufferFloatL_.begin(), delayBufferFloatL_.end(), 0.0f);
    std::fill(delayBufferFloatR_.begin(), delayBufferFloatR_.end(), 0.0f);

    std::fill(delayBufferDoubleL_.begin(), delayBufferDoubleL_.end(), 0.0);
    std::fill(delayBufferDoubleR_.begin(), delayBufferDoubleR_.end(), 0.0);

    writePositionFloat_ = 0;
    writePositionDouble_ = 0;
    workloadSink_ = 0.0;
}

uint32_t DelayProcessor::computeDelaySamples(double delayMs) const noexcept {
    if (delayBufferSize_ < 2) {
        return 1;
    }

    const auto delaySamples =
        std::llround((delayMs / 1000.0) * sampleRate_);

    return static_cast<uint32_t>(
        std::clamp(
            delaySamples,
            1LL,
            static_cast<long long>(delayBufferSize_ - 1)
        )
    );
}

float DelayProcessor::dbToGainFloat(double db) noexcept {
    return static_cast<float>(std::pow(10.0, db / 20.0));
}

double DelayProcessor::dbToGainDouble(double db) noexcept {
    return std::pow(10.0, db / 20.0);
}

void DelayProcessor::runArtificialWorkload(double input, double dspComplexity) noexcept {
    const double clampedComplexity = std::clamp(dspComplexity, 0.0, 100.0);
    const int iterations = static_cast<int>(std::llround(clampedComplexity * 2.0));

    if (iterations <= 0) {
        return;
    }

    // This workload is intentionally independent from the audible output.
    // It is only used to create a controlled increase in processing time
    // for real-time performance measurements.
    double x = input + workloadSink_ * 0.000001;

    for (int i = 0; i < iterations; ++i) {
        x = std::sin(x + static_cast<double>(i) * 0.000001);
    }

    workloadSink_ = x;
}

void DelayProcessor::processSample(float inL,
                                   float inR,
                                   float& outL,
                                   float& outR,
                                   const DelayParameters& params) noexcept {
    if (delayBufferSize_ == 0) {
        outL = inL;
        outR = inR;
        return;
    }

    runArtificialWorkload((static_cast<double>(inL) + static_cast<double>(inR)) * 0.5,
                          params.dspComplexity);

    const uint32_t delaySamples = computeDelaySamples(params.delayMs);

    const uint32_t readPosition =
        (writePositionFloat_ + delayBufferSize_ - delaySamples) % delayBufferSize_;

    const float delayedL = delayBufferFloatL_[readPosition];
    const float delayedR = delayBufferFloatR_[readPosition];

    if (params.bypassed) {
        outL = inL;
        outR = inR;

        delayBufferFloatL_[writePositionFloat_] = inL;
        delayBufferFloatR_[writePositionFloat_] = inR;
    } else {
        const float feedback = static_cast<float>(params.feedback);
        const float mix = static_cast<float>(params.mix);
        const float gain = dbToGainFloat(params.outputDb);

        delayBufferFloatL_[writePositionFloat_] = inL + delayedL * feedback;
        delayBufferFloatR_[writePositionFloat_] = inR + delayedR * feedback;

        outL = ((1.0f - mix) * inL + mix * delayedL) * gain;
        outR = ((1.0f - mix) * inR + mix * delayedR) * gain;
    }

    ++writePositionFloat_;

    if (writePositionFloat_ >= delayBufferSize_) {
        writePositionFloat_ = 0;
    }
}

void DelayProcessor::processSample(double inL,
                                   double inR,
                                   double& outL,
                                   double& outR,
                                   const DelayParameters& params) noexcept {
    if (delayBufferSize_ == 0) {
        outL = inL;
        outR = inR;
        return;
    }

    runArtificialWorkload((inL + inR) * 0.5, params.dspComplexity);

    const uint32_t delaySamples = computeDelaySamples(params.delayMs);

    const uint32_t readPosition =
        (writePositionDouble_ + delayBufferSize_ - delaySamples) % delayBufferSize_;

    const double delayedL = delayBufferDoubleL_[readPosition];
    const double delayedR = delayBufferDoubleR_[readPosition];

    if (params.bypassed) {
        outL = inL;
        outR = inR;

        delayBufferDoubleL_[writePositionDouble_] = inL;
        delayBufferDoubleR_[writePositionDouble_] = inR;
    } else {
        const double gain = dbToGainDouble(params.outputDb);

        delayBufferDoubleL_[writePositionDouble_] = inL + delayedL * params.feedback;
        delayBufferDoubleR_[writePositionDouble_] = inR + delayedR * params.feedback;

        outL = ((1.0 - params.mix) * inL + params.mix * delayedL) * gain;
        outR = ((1.0 - params.mix) * inR + params.mix * delayedR) * gain;
    }

    ++writePositionDouble_;

    if (writePositionDouble_ >= delayBufferSize_) {
        writePositionDouble_ = 0;
    }
}
