#include "plugin/delay_plugin.h"

#include "plugin/audio_ports.h"
#include "plugin/event_segments.h"
#include "utils/denormal_guard.h"
#include "utils/scoped_process_timer.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

DelayPlugin::DelayPlugin(const clap_host_t* host) noexcept : host_(host) {
    if (const char* env = std::getenv("DELAY_SAMPLE_FORMAT")) {
        if (std::strcmp(env, "32") == 0) {
            sampleFormatPinned_ = true;
            pinnedAdvertise64Bits_ = false;
        } else if (std::strcmp(env, "64") == 0) {
            sampleFormatPinned_ = true;
            pinnedAdvertise64Bits_ = true;
        }
    }

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
    // Host extensions are only guaranteed to be available from init() onwards.
    if (host_ && host_->get_extension) {
        hostAudioPorts_ = static_cast<const clap_host_audio_ports_t*>(
            host_->get_extension(host_, CLAP_EXT_AUDIO_PORTS));
    }

    lastAdvertise64Bits_ = advertise64Bits();
    return true;
}

bool DelayPlugin::advertise64Bits() const noexcept {
    if (sampleFormatPinned_) {
        return pinnedAdvertise64Bits_;
    }
    return parameters_.get(ParameterManager::ParamSampleFormat) < 0.5;
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

    lastRunMarker_ = static_cast<uint32_t>(
        parameters_.get(ParameterManager::ParamRunMarker));
    metrics_.beginRun(lastRunMarker_);

    metricsLogger_.start(metrics_);

    active_ = true;
    return true;
}

void DelayPlugin::deactivate() noexcept {
    active_ = false;
    metricsLogger_.stop();
    delayProcessor_.release();
    metrics_.reset();

    if (portFlagsChangePending_.exchange(false, std::memory_order_acq_rel)) {
        lastAdvertise64Bits_ = advertise64Bits();

        if (hostAudioPorts_ && hostAudioPorts_->rescan &&
            hostAudioPorts_->is_rescan_flag_supported &&
            hostAudioPorts_->is_rescan_flag_supported(host_,
                                                      CLAP_AUDIO_PORTS_RESCAN_FLAGS)) {
            hostAudioPorts_->rescan(host_, CLAP_AUDIO_PORTS_RESCAN_FLAGS);
        }
    }
}

bool DelayPlugin::startProcessing() noexcept {
    return active_;
}

void DelayPlugin::stopProcessing() noexcept {}

void DelayPlugin::reset() noexcept {
    delayProcessor_.reset();
}

template <typename SampleType>
void DelayPlugin::processBuffers(const clap_process_t* process,
                                 const clap_audio_buffer_t& input,
                                 clap_audio_buffer_t& output,
                                 SampleType** inputData,
                                 SampleType** outputData) noexcept {
    DelayCoefficients coefficients =
        delayProcessor_.makeCoefficients(currentDelayParameters());

    bool coefficientsDirty = false;
    const bool stereoIn = input.channel_count > 1;
    const bool stereoOut = output.channel_count > 1;

    processByEventSegments(
        process,
        [this, &coefficientsDirty](const clap_event_header_t* event) noexcept {
            handleEvent(event);
            coefficientsDirty = true;
        },
        [&](uint32_t begin, uint32_t end) noexcept {
            if (coefficientsDirty) {
                coefficients =
                    delayProcessor_.makeCoefficients(currentDelayParameters());
                coefficientsDirty = false;
            }

            for (uint32_t frame = begin; frame < end; ++frame) {
#ifdef DELAY_NAIVE_INNER_LOOP
                // Reference path for the build-configuration comparison: the
                // coefficients are rebuilt for every single sample.
                coefficients =
                    delayProcessor_.makeCoefficients(currentDelayParameters());
#endif
                const SampleType inL = inputData[0][frame];
                const SampleType inR = stereoIn ? inputData[1][frame] : inL;

                SampleType outL = static_cast<SampleType>(0);
                SampleType outR = static_cast<SampleType>(0);

                delayProcessor_.processSample(inL, inR, outL, outR, coefficients);

                outputData[0][frame] = outL;
                if (stereoOut) {
                    outputData[1][frame] = outR;
                }
            }
        });
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

    const DenormalGuard denormalGuard;

    syncRunMarker();

    const uint32_t frames = process->frames_count;
    const uint32_t channelCount =
        std::min<uint32_t>(input.channel_count, output.channel_count);

    metrics_.configure(sampleRate_, maxFrameCount_, channelCount);
    updateMetricsForCurrentParameters();

    const int requestedBits = advertise64Bits() ? 64 : 32;

    if (input.data32 && output.data32) {
        metrics_.updateBlock(frames, 32, requestedBits);
        {
            ScopedProcessTimer timer(metrics_, frames);
            processBuffers<float>(process, input, output, input.data32, output.data32);
        }
    } else if (input.data64 && output.data64) {
        metrics_.updateBlock(frames, 64, requestedBits);
        {
            ScopedProcessTimer timer(metrics_, frames);
            processBuffers<double>(process, input, output, input.data64, output.data64);
        }
    }

    syncSampleFormat();

    return CLAP_PROCESS_CONTINUE;
}

void DelayPlugin::onMainThread() noexcept {}

void DelayPlugin::syncRunMarker() noexcept {
    const auto marker =
        static_cast<uint32_t>(parameters_.get(ParameterManager::ParamRunMarker));

    if (marker != lastRunMarker_) {
        lastRunMarker_ = marker;
        metrics_.beginRun(marker);
    }
}

void DelayPlugin::syncSampleFormat() noexcept {
    if (advertise64Bits() == lastAdvertise64Bits_) {
        return;
    }

    if (!portFlagsChangePending_.exchange(true, std::memory_order_acq_rel)) {
        if (host_ && host_->request_restart) {
            host_->request_restart(host_);
        }
    }
}

void DelayPlugin::applySampleFormatIfInactive() noexcept {
    if (active_ || advertise64Bits() == lastAdvertise64Bits_) {
        return;
    }

    lastAdvertise64Bits_ = advertise64Bits();
    portFlagsChangePending_.store(false, std::memory_order_release);

    if (hostAudioPorts_ && hostAudioPorts_->rescan &&
        hostAudioPorts_->is_rescan_flag_supported &&
        hostAudioPorts_->is_rescan_flag_supported(host_,
                                                  CLAP_AUDIO_PORTS_RESCAN_FLAGS)) {
        hostAudioPorts_->rescan(host_, CLAP_AUDIO_PORTS_RESCAN_FLAGS);
    }
}

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
        params.outputDb,
        params.dspComplexity
    );
}

void DelayPlugin::handleEvent(const clap_event_header_t* event) noexcept {
    parameters_.handleEvent(event);
}

uint32_t DelayPlugin::audioPortsCount(bool isInput) const noexcept {
    return AudioPorts::count(isInput);
}

bool DelayPlugin::audioPortsInfo(uint32_t index,
                                 bool isInput,
                                 clap_audio_port_info_t* info) const noexcept {

    return AudioPorts::info(index, isInput, lastAdvertise64Bits_, info);
}
