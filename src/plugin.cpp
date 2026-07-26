#include "plugin.h"

#include "scoped_process_timer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <new>

#include <clap/ext/audio-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/events.h>
#include <clap/plugin-features.h>
#include <clap/version.h>

namespace {
constexpr uint32_t AudioPortId = 0;
constexpr uint32_t StateMagic = 0x46444C59; // 'FDLY'
constexpr uint32_t StateVersion = 2;

struct DelayState {
    uint32_t magic;
    uint32_t version;
    double bypass;
    double delayMs;
    double feedback;
    double mix;
    double outputDb;
    double dspComplexity;
};

const clap_plugin_audio_ports_t audioPortsExtension {
    DelayPlugin::audioPortsCountCallback,
    DelayPlugin::audioPortsInfoCallback,
};

const clap_plugin_params_t paramsExtension {
    DelayPlugin::paramsCountCallback,
    DelayPlugin::paramsInfoCallback,
    DelayPlugin::paramsValueCallback,
    DelayPlugin::paramsValueToTextCallback,
    DelayPlugin::paramsTextToValueCallback,
    DelayPlugin::paramsFlushCallback,
};

const clap_plugin_state_t stateExtension {
    DelayPlugin::stateSaveCallback,
    DelayPlugin::stateLoadCallback,
};

constexpr DelayPlugin::ParameterDefinition parameterDefinitions[] {
    {DelayPlugin::ParamBypass,
     "Bypass",
     "Global",
     0.0,
     1.0,
     0.0,
     CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_BYPASS | CLAP_PARAM_IS_AUTOMATABLE |
         CLAP_PARAM_REQUIRES_PROCESS},
    {DelayPlugin::ParamDelayMs,
     "Delay Time",
     "Delay",
     1.0,
     2000.0,
     350.0,
     CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS},
    {DelayPlugin::ParamFeedback,
     "Feedback",
     "Delay",
     0.0,
     0.95,
     0.35,
     CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS},
    {DelayPlugin::ParamMix,
     "Mix",
     "Delay",
     0.0,
     1.0,
     0.35,
     CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS},
    {DelayPlugin::ParamOutputDb,
     "Output",
     "Output",
     -24.0,
     12.0,
     0.0,
     CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS},
    {DelayPlugin::ParamDspComplexity,
     "DSP Complexity",
     "Analysis",
     0.0,
     100.0,
     0.0,
     CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS},
};

bool writeBytes(const clap_ostream_t* stream, const void* data, uint64_t size) noexcept {
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint64_t totalWritten = 0;

    while (totalWritten < size) {
        const int64_t written = stream->write(stream, bytes + totalWritten, size - totalWritten);
        if (written <= 0) {
            return false;
        }
        totalWritten += static_cast<uint64_t>(written);
    }

    return true;
}

bool readBytes(const clap_istream_t* stream, void* data, uint64_t size) noexcept {
    auto* bytes = static_cast<uint8_t*>(data);
    uint64_t totalRead = 0;

    while (totalRead < size) {
        const int64_t read = stream->read(stream, bytes + totalRead, size - totalRead);
        if (read <= 0) {
            return false;
        }
        totalRead += static_cast<uint64_t>(read);
    }

    return true;
}

template <typename HandleEvent, typename ProcessFrame>
void processByEventSegments(const clap_process_t* process,
                            HandleEvent&& handleEvent,
                            ProcessFrame&& processFrame) noexcept {
    const uint32_t frames = process->frames_count;
    const uint32_t eventCount = process->in_events ? process->in_events->size(process->in_events) : 0;
    uint32_t eventIndex = 0;
    uint32_t nextEventFrame = 0;

    while (eventIndex < eventCount) {
        const clap_event_header_t* event = process->in_events->get(process->in_events, eventIndex);
        if (event && event->time == 0) {
            handleEvent(event);
            ++eventIndex;
        } else {
            nextEventFrame = event ? std::min<uint32_t>(event->time, frames) : frames;
            break;
        }
    }

    if (eventIndex >= eventCount) {
        nextEventFrame = frames;
    }

    for (uint32_t frame = 0; frame < frames;) {
        while (eventIndex < eventCount && nextEventFrame == frame) {
            const clap_event_header_t* event = process->in_events->get(process->in_events, eventIndex);
            if (!event) {
                ++eventIndex;
                continue;
            }

            if (event->time != frame) {
                nextEventFrame = std::min<uint32_t>(event->time, frames);
                break;
            }

            handleEvent(event);
            ++eventIndex;

            if (eventIndex < eventCount) {
                const clap_event_header_t* nextEvent = process->in_events->get(process->in_events, eventIndex);
                nextEventFrame = nextEvent ? std::min<uint32_t>(nextEvent->time, frames) : frames;
            } else {
                nextEventFrame = frames;
            }
        }

        const uint32_t endFrame = std::max(frame + 1, nextEventFrame);
        for (; frame < endFrame && frame < frames; ++frame) {
            processFrame(frame);
        }
    }
}
} // namespace

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

const clap_plugin_descriptor_t* DelayPlugin::descriptor() noexcept {
    static const char* features[] = {
        CLAP_PLUGIN_FEATURE_AUDIO_EFFECT,
        CLAP_PLUGIN_FEATURE_DELAY,
        CLAP_PLUGIN_FEATURE_STEREO,
        nullptr,
    };

    static const clap_plugin_descriptor_t desc {
        CLAP_VERSION_INIT,
        "de.filipmueller.filip-delay",
        "Filip Delay",
        "Filip Mueller",
        "",
        "",
        "",
#ifdef FILIP_DELAY_VERSION
        FILIP_DELAY_VERSION,
#else
        "0.1.0",
#endif
        "Simple real-time stereo delay effect built with the CLAP plugin API.",
        features,
    };

    return &desc;
}

const DelayPlugin::ParameterDefinition* DelayPlugin::findParameter(clap_id id) noexcept {
    for (const auto& param : parameterDefinitions) {
        if (param.id == id) {
            return &param;
        }
    }
    return nullptr;
}

double DelayPlugin::clampToParameterRange(clap_id id, double value) noexcept {
    const auto* param = findParameter(id);
    if (!param || !std::isfinite(value)) {
        return 0.0;
    }
    return std::clamp(value, param->minValue, param->maxValue);
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

    const DelayParameters params = currentDelayParameters();
    metrics_.updateDelayParameters(
        params.delayMs,
        delayProcessor_.computeDelaySamples(params.delayMs),
        params.feedback,
        params.mix,
        params.dspComplexity
    );

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

    const DelayParameters paramsForMetrics = currentDelayParameters();
    metrics_.updateDelayParameters(
        paramsForMetrics.delayMs,
        delayProcessor_.computeDelaySamples(paramsForMetrics.delayMs),
        paramsForMetrics.feedback,
        paramsForMetrics.mix,
        paramsForMetrics.dspComplexity
    );

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

                delayProcessor_.processSample(
                    inL,
                    inR,
                    outL,
                    outR,
                    currentDelayParameters()
                );

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

                delayProcessor_.processSample(
                    inL,
                    inR,
                    outL,
                    outR,
                    currentDelayParameters()
                );

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

const void* DelayPlugin::getExtension(const char* id) noexcept {
    if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS)) {
        return &audioPortsExtension;
    }
    if (!std::strcmp(id, CLAP_EXT_PARAMS)) {
        return &paramsExtension;
    }
    if (!std::strcmp(id, CLAP_EXT_STATE)) {
        return &stateExtension;
    }
    return nullptr;
}

void DelayPlugin::onMainThread() noexcept {}

uint32_t DelayPlugin::audioPortsCount(bool) const noexcept {
    return 1;
}

bool DelayPlugin::audioPortsInfo(uint32_t index, bool isInput, clap_audio_port_info_t* info) const noexcept {
    if (!info || index != 0) {
        return false;
    }

    std::memset(info, 0, sizeof(*info));
    info->id = AudioPortId;
    std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Stereo In" : "Stereo Out");
    info->channel_count = 2;
    info->flags = CLAP_AUDIO_PORT_IS_MAIN
                | CLAP_AUDIO_PORT_SUPPORTS_64BITS
                | CLAP_AUDIO_PORT_PREFERS_64BITS;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

uint32_t DelayPlugin::paramsCount() const noexcept {
    return static_cast<uint32_t>(std::size(parameterDefinitions));
}

bool DelayPlugin::paramsInfo(uint32_t paramIndex, clap_param_info_t* info) const noexcept {
    if (!info || paramIndex >= std::size(parameterDefinitions)) {
        return false;
    }

    const auto& param = parameterDefinitions[paramIndex];
    std::memset(info, 0, sizeof(*info));
    info->id = param.id;
    info->flags = param.flags;
    info->cookie = nullptr;
    std::snprintf(info->name, sizeof(info->name), "%s", param.name);
    std::snprintf(info->module, sizeof(info->module), "%s", param.module);
    info->min_value = param.minValue;
    info->max_value = param.maxValue;
    info->default_value = param.defaultValue;
    return true;
}

bool DelayPlugin::paramsValue(clap_id paramId, double* value) const noexcept {
    if (!value || !findParameter(paramId)) {
        return false;
    }
    *value = getParameter(paramId);
    return true;
}

bool DelayPlugin::paramsValueToText(clap_id paramId, double value, char* display, uint32_t size) const noexcept {
    if (!display || size == 0 || !findParameter(paramId)) {
        return false;
    }

    const double clamped = clampToParameterRange(paramId, value);
    switch (paramId) {
        case ParamBypass:
            std::snprintf(display, size, "%s", clamped >= 0.5 ? "On" : "Off");
            return true;
        case ParamDelayMs:
            std::snprintf(display, size, "%.1f ms", clamped);
            return true;
        case ParamFeedback:
        case ParamMix:
            std::snprintf(display, size, "%.1f %%", clamped * 100.0);
            return true;
        case ParamOutputDb:
            std::snprintf(display, size, "%.1f dB", clamped);
            return true;
        case ParamDspComplexity:
            std::snprintf(display, size, "%.1f %%", clamped);
            return true;
        default:
            return false;
    }
}

bool DelayPlugin::paramsTextToValue(clap_id paramId, const char* display, double* value) const noexcept {
    if (!display || !value || !findParameter(paramId)) {
        return false;
    }

    if (paramId == ParamBypass) {
        if (!std::strcmp(display, "On") || !std::strcmp(display, "on") || !std::strcmp(display, "1")) {
            *value = 1.0;
            return true;
        }
        if (!std::strcmp(display, "Off") || !std::strcmp(display, "off") || !std::strcmp(display, "0")) {
            *value = 0.0;
            return true;
        }
    }

    char* end = nullptr;
    const double parsed = std::strtod(display, &end);
    if (end == display) {
        return false;
    }

    if (paramId == ParamFeedback || paramId == ParamMix) {
        *value = clampToParameterRange(paramId, parsed > 1.0 ? parsed / 100.0 : parsed);
    } else {
        *value = clampToParameterRange(paramId, parsed);
    }

    return true;
}

void DelayPlugin::paramsFlush(const clap_input_events_t* in, const clap_output_events_t*) noexcept {
    if (!in) {
        return;
    }

    const uint32_t eventCount = in->size(in);
    for (uint32_t i = 0; i < eventCount; ++i) {
        handleEvent(in->get(in, i));
    }
}

bool DelayPlugin::stateSave(const clap_ostream_t* stream) const noexcept {
    if (!stream) {
        return false;
    }

    const DelayState state {
        StateMagic,
        StateVersion,
        bypass_.load(std::memory_order_relaxed),
        delayMs_.load(std::memory_order_relaxed),
        feedback_.load(std::memory_order_relaxed),
        mix_.load(std::memory_order_relaxed),
        outputDb_.load(std::memory_order_relaxed),
        dspComplexity_.load(std::memory_order_relaxed),
    };

    return writeBytes(stream, &state, sizeof(state));
}

bool DelayPlugin::stateLoad(const clap_istream_t* stream) noexcept {
    if (!stream) {
        return false;
    }

    DelayState state {};
    if (!readBytes(stream, &state, sizeof(state)) || state.magic != StateMagic || state.version != StateVersion) {
        return false;
    }

    setParameter(ParamBypass, state.bypass);
    setParameter(ParamDelayMs, state.delayMs);
    setParameter(ParamFeedback, state.feedback);
    setParameter(ParamMix, state.mix);
    setParameter(ParamOutputDb, state.outputDb);
    setParameter(ParamDspComplexity, state.dspComplexity);
    return true;
}

void DelayPlugin::handleEvent(const clap_event_header_t* event) noexcept {
    if (!event) {
        return;
    }

    if (event->space_id == CLAP_CORE_EVENT_SPACE_ID &&
        (event->type == CLAP_EVENT_PARAM_VALUE || event->type == CLAP_EVENT_PARAM_MOD)) {
        const auto* paramEvent = reinterpret_cast<const clap_event_param_value_t*>(event);
        setParameter(paramEvent->param_id, paramEvent->value);
    }
}

void DelayPlugin::setParameter(clap_id id, double value) noexcept {
    const double clamped = clampToParameterRange(id, value);

    switch (id) {
        case ParamBypass:
            bypass_.store(clamped, std::memory_order_relaxed);
            break;
        case ParamDelayMs:
            delayMs_.store(clamped, std::memory_order_relaxed);
            break;
        case ParamFeedback:
            feedback_.store(clamped, std::memory_order_relaxed);
            break;
        case ParamMix:
            mix_.store(clamped, std::memory_order_relaxed);
            break;
        case ParamOutputDb:
            outputDb_.store(clamped, std::memory_order_relaxed);
            break;
        case ParamDspComplexity:
            dspComplexity_.store(clamped, std::memory_order_relaxed);
            break;
        default:
            break;
    }
}

double DelayPlugin::getParameter(clap_id id) const noexcept {
    switch (id) {
        case ParamBypass:
            return bypass_.load(std::memory_order_relaxed);
        case ParamDelayMs:
            return delayMs_.load(std::memory_order_relaxed);
        case ParamFeedback:
            return feedback_.load(std::memory_order_relaxed);
        case ParamMix:
            return mix_.load(std::memory_order_relaxed);
        case ParamOutputDb:
            return outputDb_.load(std::memory_order_relaxed);
        case ParamDspComplexity:
            return dspComplexity_.load(std::memory_order_relaxed);
        default:
            return 0.0;
    }
}

DelayParameters DelayPlugin::currentDelayParameters() const noexcept {
    DelayParameters params {};

    params.bypassed = bypass_.load(std::memory_order_relaxed) >= 0.5;
    params.delayMs = delayMs_.load(std::memory_order_relaxed);
    params.feedback = feedback_.load(std::memory_order_relaxed);
    params.mix = mix_.load(std::memory_order_relaxed);
    params.outputDb = outputDb_.load(std::memory_order_relaxed);
    params.dspComplexity = dspComplexity_.load(std::memory_order_relaxed);

    return params;
}

bool DelayPlugin::clapInit(const clap_plugin_t* plugin) noexcept {
    return from(plugin).init();
}

void DelayPlugin::clapDestroy(const clap_plugin_t* plugin) noexcept {
    delete &from(plugin);
}

bool DelayPlugin::clapActivate(const clap_plugin_t* plugin,
                               double sampleRate,
                               uint32_t minFrameCount,
                               uint32_t maxFrameCount) noexcept {
    return from(plugin).activate(sampleRate, minFrameCount, maxFrameCount);
}

void DelayPlugin::clapDeactivate(const clap_plugin_t* plugin) noexcept {
    from(plugin).deactivate();
}

bool DelayPlugin::clapStartProcessing(const clap_plugin_t* plugin) noexcept {
    return from(plugin).startProcessing();
}

void DelayPlugin::clapStopProcessing(const clap_plugin_t* plugin) noexcept {
    from(plugin).stopProcessing();
}

void DelayPlugin::clapReset(const clap_plugin_t* plugin) noexcept {
    from(plugin).reset();
}

clap_process_status DelayPlugin::clapProcess(const clap_plugin_t* plugin, const clap_process_t* process) noexcept {
    return from(plugin).process(process);
}

const void* DelayPlugin::clapGetExtension(const clap_plugin_t* plugin, const char* id) noexcept {
    return from(plugin).getExtension(id);
}

void DelayPlugin::clapOnMainThread(const clap_plugin_t* plugin) noexcept {
    from(plugin).onMainThread();
}

uint32_t DelayPlugin::audioPortsCountCallback(const clap_plugin_t* plugin, bool isInput) noexcept {
    return from(plugin).audioPortsCount(isInput);
}

bool DelayPlugin::audioPortsInfoCallback(const clap_plugin_t* plugin,
                                         uint32_t index,
                                         bool isInput,
                                         clap_audio_port_info_t* info) noexcept {
    return from(plugin).audioPortsInfo(index, isInput, info);
}

uint32_t DelayPlugin::paramsCountCallback(const clap_plugin_t* plugin) noexcept {
    return from(plugin).paramsCount();
}

bool DelayPlugin::paramsInfoCallback(const clap_plugin_t* plugin,
                                     uint32_t paramIndex,
                                     clap_param_info_t* info) noexcept {
    return from(plugin).paramsInfo(paramIndex, info);
}

bool DelayPlugin::paramsValueCallback(const clap_plugin_t* plugin, clap_id paramId, double* value) noexcept {
    return from(plugin).paramsValue(paramId, value);
}

bool DelayPlugin::paramsValueToTextCallback(const clap_plugin_t* plugin,
                                            clap_id paramId,
                                            double value,
                                            char* display,
                                            uint32_t size) noexcept {
    return from(plugin).paramsValueToText(paramId, value, display, size);
}

bool DelayPlugin::paramsTextToValueCallback(const clap_plugin_t* plugin,
                                            clap_id paramId,
                                            const char* display,
                                            double* value) noexcept {
    return from(plugin).paramsTextToValue(paramId, display, value);
}

void DelayPlugin::paramsFlushCallback(const clap_plugin_t* plugin,
                                      const clap_input_events_t* in,
                                      const clap_output_events_t* out) noexcept {
    from(plugin).paramsFlush(in, out);
}

bool DelayPlugin::stateSaveCallback(const clap_plugin_t* plugin, const clap_ostream_t* stream) noexcept {
    return from(plugin).stateSave(stream);
}

bool DelayPlugin::stateLoadCallback(const clap_plugin_t* plugin, const clap_istream_t* stream) noexcept {
    return from(plugin).stateLoad(stream);
}

const clap_plugin_t* createDelayPlugin(const clap_host_t* host) noexcept {
    auto* plugin = new (std::nothrow) DelayPlugin(host);
    return plugin ? plugin->clapPlugin() : nullptr;
}

const clap_plugin_descriptor_t* getDelayPluginDescriptor() noexcept {
    return DelayPlugin::descriptor();
}
