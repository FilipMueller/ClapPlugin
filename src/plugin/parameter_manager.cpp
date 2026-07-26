#include "plugin/parameter_manager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

#include <clap/ext/params.h>

namespace {
constexpr ParameterManager::Definition parameterDefinitions[] {
    {ParameterManager::ParamBypass,
     "Bypass",
     "Global",
     0.0,
     1.0,
     0.0,
     CLAP_PARAM_IS_STEPPED | CLAP_PARAM_IS_BYPASS | CLAP_PARAM_IS_AUTOMATABLE |
         CLAP_PARAM_REQUIRES_PROCESS},
    {ParameterManager::ParamDelayMs,
     "Delay Time",
     "Delay",
     1.0,
     2000.0,
     350.0,
     CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS},
    {ParameterManager::ParamFeedback,
     "Feedback",
     "Delay",
     0.0,
     0.95,
     0.35,
     CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS},
    {ParameterManager::ParamMix,
     "Mix",
     "Delay",
     0.0,
     1.0,
     0.35,
     CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS},
    {ParameterManager::ParamOutputDb,
     "Output",
     "Output",
     -24.0,
     12.0,
     0.0,
     CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS},
    {ParameterManager::ParamDspComplexity,
     "DSP Complexity",
     "Analysis",
     0.0,
     100.0,
     0.0,
     CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_REQUIRES_PROCESS},
};
} // namespace

uint32_t ParameterManager::count() noexcept {
    return static_cast<uint32_t>(std::size(parameterDefinitions));
}

const ParameterManager::Definition* ParameterManager::find(clap_id id) noexcept {
    for (const auto& param : parameterDefinitions) {
        if (param.id == id) {
            return &param;
        }
    }
    return nullptr;
}

double ParameterManager::clamp(clap_id id, double value) noexcept {
    const auto* param = find(id);
    if (!param || !std::isfinite(value)) {
        return 0.0;
    }
    return std::clamp(value, param->minValue, param->maxValue);
}

bool ParameterManager::info(uint32_t index, clap_param_info_t* info) const noexcept {
    if (!info || index >= std::size(parameterDefinitions)) {
        return false;
    }

    const auto& param = parameterDefinitions[index];
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

bool ParameterManager::value(clap_id id, double* value) const noexcept {
    if (!value || !find(id)) {
        return false;
    }
    *value = get(id);
    return true;
}

bool ParameterManager::valueToText(clap_id id, double value, char* display, uint32_t size) const noexcept {
    if (!display || size == 0 || !find(id)) {
        return false;
    }

    const double clamped = clamp(id, value);
    switch (id) {
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

bool ParameterManager::textToValue(clap_id id, const char* display, double* value) const noexcept {
    if (!display || !value || !find(id)) {
        return false;
    }

    if (id == ParamBypass) {
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

    if (id == ParamFeedback || id == ParamMix) {
        *value = clamp(id, parsed > 1.0 ? parsed / 100.0 : parsed);
    } else {
        *value = clamp(id, parsed);
    }

    return true;
}

void ParameterManager::flush(const clap_input_events_t* in) noexcept {
    if (!in) {
        return;
    }

    const uint32_t eventCount = in->size(in);
    for (uint32_t i = 0; i < eventCount; ++i) {
        handleEvent(in->get(in, i));
    }
}

void ParameterManager::handleEvent(const clap_event_header_t* event) noexcept {
    if (!event) {
        return;
    }

    if (event->space_id == CLAP_CORE_EVENT_SPACE_ID &&
        (event->type == CLAP_EVENT_PARAM_VALUE || event->type == CLAP_EVENT_PARAM_MOD)) {
        const auto* paramEvent = reinterpret_cast<const clap_event_param_value_t*>(event);
        set(paramEvent->param_id, paramEvent->value);
    }
}

void ParameterManager::set(clap_id id, double value) noexcept {
    const double clamped = clamp(id, value);

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

double ParameterManager::get(clap_id id) const noexcept {
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

DelayParameters ParameterManager::currentDelayParameters() const noexcept {
    DelayParameters params {};
    params.bypassed = bypass_.load(std::memory_order_relaxed) >= 0.5;
    params.delayMs = delayMs_.load(std::memory_order_relaxed);
    params.feedback = feedback_.load(std::memory_order_relaxed);
    params.mix = mix_.load(std::memory_order_relaxed);
    params.outputDb = outputDb_.load(std::memory_order_relaxed);
    params.dspComplexity = dspComplexity_.load(std::memory_order_relaxed);
    return params;
}

DelayParameterState ParameterManager::state() const noexcept {
    DelayParameterState state {};
    state.bypass = bypass_.load(std::memory_order_relaxed);
    state.delayMs = delayMs_.load(std::memory_order_relaxed);
    state.feedback = feedback_.load(std::memory_order_relaxed);
    state.mix = mix_.load(std::memory_order_relaxed);
    state.outputDb = outputDb_.load(std::memory_order_relaxed);
    state.dspComplexity = dspComplexity_.load(std::memory_order_relaxed);
    return state;
}

void ParameterManager::setState(const DelayParameterState& state) noexcept {
    set(ParamBypass, state.bypass);
    set(ParamDelayMs, state.delayMs);
    set(ParamFeedback, state.feedback);
    set(ParamMix, state.mix);
    set(ParamOutputDb, state.outputDb);
    set(ParamDspComplexity, state.dspComplexity);
}
