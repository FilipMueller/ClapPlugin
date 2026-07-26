#include "plugin/delay_plugin.h"

#include "plugin/event_segments.h"
#include "utils/scoped_process_timer.h"

#include <algorithm>

DelayPlugin::DelayPlugin(const clap_host_t* host) noexcept : host_(host) {
    plugin_.desc = descriptor();
    plugin_.plugin_data = this;
    plugin_.init = clapInit;
    plugin_.destroy = clapDestroy;
    plugin_.activate = clapActivate;
    plugin_.deactivate = clapDeactivate;
    plugin_.start_processing = clapStartProcessing;
    plugin_.stop_processing = clapStopProcessing;
    plugin_.reset = clapReset;
    plugin_.process = clapProcess;
    plugin_.get_extension = clapGetExtension;
    plugin_.on_main_thread = clapOnMainThread;
}

DelayPlugin& DelayPlugin::from(const clap_plugin_t* plugin) noexcept {
    return *static_cast<DelayPlugin*>(plugin->plugin_data);
}

bool DelayPlugin::init() noexcept {
    return true;
}

bool DelayPlugin::activate(double sampleRate, uint32_t, uint32_t maxFrameCount) noexcept {
    if (sampleRate <= 0.0 || maxFrameCount == 0) {
        return false;
    }

    sampleRate_ = sampleRate;
    maxFrameCount_ = maxFrameCount;

    if (!delayProcessor_.prepare(sampleRate_, maxFrameCount_)) {
        active_ = false;
        return false;
    }

    metrics_.reset();
    metrics_.configure(sampleRate_, maxFrameCount_, 2);
    updateMetricsForCurrentParameters();
    metricsLogger_.start(metrics_);

    active_ = true;
    return true;
}

void DelayPlugin::deactivate() noexcept {
    active_ = false;
    metricsLogger_.stop();
    delayProcessor_.release();
    metrics_.reset();
}

bool DelayPlugin::startProcessing() noexcept {
    return active_;
}

void DelayPlugin::stopProcessing() noexcept {}

void DelayPlugin::reset() noexcept {
    delayProcessor_.reset();
}

clap_process_status DelayPlugin::process(const clap_process_t* process) noexcept {
    if (!process ||
        process->audio_inputs_count < 1 ||
        process->audio_outputs_count < 1 ||
        !process->audio_inputs ||
        !process->audio_outputs ||
        !delayProcessor_.isPrepared()) {
        return CLAP_PROCESS_CONTINUE;
    }

    const auto& input = process->audio_inputs[0];
    auto& output = process->audio_outputs[0];

    if (input.channel_count < 1 || output.channel_count < 1) {
        return CLAP_PROCESS_CONTINUE;
    }

    const uint32_t frames = process->frames_count;
    const uint32_t channelCount = std::min<uint32_t>(input.channel_count, output.channel_count);
    metrics_.configure(sampleRate_, maxFrameCount_, channelCount);
    updateMetricsForCurrentParameters();

    if (input.data32 && output.data32) {
        metrics_.updateBlock(frames, 32);
        ScopedProcessTimer timer(metrics_);

        processByEventSegments(
            process,
            [this](const clap_event_header_t* event) noexcept {
                handleEvent(event);
            },
            [this, &input, &output](uint32_t frame) noexcept {
                const float inL = input.data32[0][frame];
                const float inR = input.channel_count > 1 ? input.data32[1][frame] : inL;

                float outL = 0.0f;
                float outR = 0.0f;

                delayProcessor_.processSample(inL, inR, outL, outR, currentDelayParameters());

                output.data32[0][frame] = outL;
                if (output.channel_count > 1) {
                    output.data32[1][frame] = outR;
                }
            }
        );

        return CLAP_PROCESS_CONTINUE;
    }

    if (input.data64 && output.data64) {
        metrics_.updateBlock(frames, 64);
        ScopedProcessTimer timer(metrics_);

        processByEventSegments(
            process,
            [this](const clap_event_header_t* event) noexcept {
                handleEvent(event);
            },
            [this, &input, &output](uint32_t frame) noexcept {
                const double inL = input.data64[0][frame];
                const double inR = input.channel_count > 1 ? input.data64[1][frame] : inL;

                double outL = 0.0;
                double outR = 0.0;

                delayProcessor_.processSample(inL, inR, outL, outR, currentDelayParameters());

                output.data64[0][frame] = outL;
                if (output.channel_count > 1) {
                    output.data64[1][frame] = outR;
                }
            }
        );

        return CLAP_PROCESS_CONTINUE;
    }

    return CLAP_PROCESS_CONTINUE;
}

void DelayPlugin::onMainThread() noexcept {}

DelayParameters DelayPlugin::currentDelayParameters() const noexcept {
    return parameters_.currentDelayParameters();
}

void DelayPlugin::updateMetricsForCurrentParameters() noexcept {
    const DelayParameters params = currentDelayParameters();
    metrics_.updateDelayParameters(
        params.delayMs,
        delayProcessor_.computeDelaySamples(params.delayMs),
        params.feedback,
        params.mix,
        params.dspComplexity
    );
}

void DelayPlugin::handleEvent(const clap_event_header_t* event) noexcept {
    parameters_.handleEvent(event);
}
