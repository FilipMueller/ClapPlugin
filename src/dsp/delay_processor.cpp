#include "delay_processor.h"

#include <algorithm>
#include <cmath>

uint32_t DelayProcessor::nextPowerOfTwo(uint32_t value) noexcept {
    if (value <= 2u) {
        return 2u;
    }

    --value;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    ++value;

    return value;
}

bool DelayProcessor::prepare(double sampleRate, uint32_t maxFrameCount) noexcept {
    if (sampleRate <= 0.0 || maxFrameCount == 0) {
        return false;
    }

    sampleRate_ = sampleRate;

    const auto requestedSize =
        static_cast<uint32_t>(std::ceil(sampleRate_ * maxDelaySeconds_)) +
        maxFrameCount + 1;

    delayBufferSize_ = nextPowerOfTwo(requestedSize);
    delayBufferMask_ = delayBufferSize_ - 1;

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
    delayBufferMask_ = 0;
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

    const auto delaySamples = std::llround((delayMs / 1000.0) * sampleRate_);

    return static_cast<uint32_t>(
        std::clamp(delaySamples, 1LL, static_cast<long long>(delayBufferSize_ - 1)));
}

float DelayProcessor::dbToGainFloat(double db) noexcept {
    return static_cast<float>(std::pow(10.0, db / 20.0));
}

double DelayProcessor::dbToGainDouble(double db) noexcept {
    return std::pow(10.0, db / 20.0);
}

DelayCoefficients DelayProcessor::makeCoefficients(
    const DelayParameters& params) const noexcept {
    DelayCoefficients coefficients;

    coefficients.bypassed = params.bypassed;
    coefficients.delaySamples = computeDelaySamples(params.delayMs);
    coefficients.dspComplexity = std::clamp(params.dspComplexity, 0.0, 100.0);
    coefficients.workloadIterations =
        static_cast<int>(std::llround(coefficients.dspComplexity * 2.0));

    const double gain = dbToGainDouble(params.outputDb);

    coefficients.feedbackD = params.feedback;
    coefficients.mixD = params.mix;
    coefficients.gainD = gain;

    coefficients.feedbackF = static_cast<float>(params.feedback);
    coefficients.mixF = static_cast<float>(params.mix);
    coefficients.gainF = static_cast<float>(gain);

    return coefficients;
}

void DelayProcessor::runArtificialWorkload(double input, int iterations) noexcept {
    if (iterations <= 0) {
        return;
    }

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
                                   const DelayCoefficients& coefficients) noexcept {
    if (delayBufferSize_ == 0) {
        outL = inL;
        outR = inR;
        return;
    }

    runArtificialWorkload((static_cast<double>(inL) + static_cast<double>(inR)) * 0.5,
                          coefficients.workloadIterations);

    const uint32_t readPosition =
        (writePositionFloat_ - coefficients.delaySamples) & delayBufferMask_;

    const float delayedL = delayBufferFloatL_[readPosition];
    const float delayedR = delayBufferFloatR_[readPosition];

    if (coefficients.bypassed) {
        outL = inL;
        outR = inR;

        delayBufferFloatL_[writePositionFloat_] = inL;
        delayBufferFloatR_[writePositionFloat_] = inR;
    } else {
        delayBufferFloatL_[writePositionFloat_] = inL + delayedL * coefficients.feedbackF;
        delayBufferFloatR_[writePositionFloat_] = inR + delayedR * coefficients.feedbackF;

        outL = ((1.0f - coefficients.mixF) * inL + coefficients.mixF * delayedL) *
               coefficients.gainF;
        outR = ((1.0f - coefficients.mixF) * inR + coefficients.mixF * delayedR) *
               coefficients.gainF;
    }

    writePositionFloat_ = (writePositionFloat_ + 1) & delayBufferMask_;
}

void DelayProcessor::processSample(double inL,
                                   double inR,
                                   double& outL,
                                   double& outR,
                                   const DelayCoefficients& coefficients) noexcept {
    if (delayBufferSize_ == 0) {
        outL = inL;
        outR = inR;
        return;
    }

    runArtificialWorkload((inL + inR) * 0.5, coefficients.workloadIterations);

    const uint32_t readPosition =
        (writePositionDouble_ - coefficients.delaySamples) & delayBufferMask_;

    const double delayedL = delayBufferDoubleL_[readPosition];
    const double delayedR = delayBufferDoubleR_[readPosition];

    if (coefficients.bypassed) {
        outL = inL;
        outR = inR;

        delayBufferDoubleL_[writePositionDouble_] = inL;
        delayBufferDoubleR_[writePositionDouble_] = inR;
    } else {
        delayBufferDoubleL_[writePositionDouble_] =
            inL + delayedL * coefficients.feedbackD;
        delayBufferDoubleR_[writePositionDouble_] =
            inR + delayedR * coefficients.feedbackD;

        outL = ((1.0 - coefficients.mixD) * inL + coefficients.mixD * delayedL) *
               coefficients.gainD;
        outR = ((1.0 - coefficients.mixD) * inR + coefficients.mixD * delayedR) *
               coefficients.gainD;
    }

    writePositionDouble_ = (writePositionDouble_ + 1) & delayBufferMask_;
}
