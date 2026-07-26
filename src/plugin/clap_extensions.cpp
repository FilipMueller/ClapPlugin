#include "plugin/delay_plugin.h"

#include "plugin/audio_ports.h"
#include "plugin/state_serializer.h"

#include <cstring>

#include <clap/ext/audio-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>

namespace {
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
} // namespace

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

uint32_t DelayPlugin::audioPortsCount(bool isInput) const noexcept {
    return AudioPorts::count(isInput);
}

bool DelayPlugin::audioPortsInfo(uint32_t index, bool isInput, clap_audio_port_info_t* info) const noexcept {
    return AudioPorts::info(index, isInput, info);
}

uint32_t DelayPlugin::paramsCount() const noexcept {
    return ParameterManager::count();
}

bool DelayPlugin::paramsInfo(uint32_t paramIndex, clap_param_info_t* info) const noexcept {
    return parameters_.info(paramIndex, info);
}

bool DelayPlugin::paramsValue(clap_id paramId, double* value) const noexcept {
    return parameters_.value(paramId, value);
}

bool DelayPlugin::paramsValueToText(clap_id paramId, double value, char* display, uint32_t size) const noexcept {
    return parameters_.valueToText(paramId, value, display, size);
}

bool DelayPlugin::paramsTextToValue(clap_id paramId, const char* display, double* value) const noexcept {
    return parameters_.textToValue(paramId, display, value);
}

void DelayPlugin::paramsFlush(const clap_input_events_t* in, const clap_output_events_t*) noexcept {
    parameters_.flush(in);
}

bool DelayPlugin::stateSave(const clap_ostream_t* stream) const noexcept {
    return StateSerializer::save(stream, parameters_);
}

bool DelayPlugin::stateLoad(const clap_istream_t* stream) noexcept {
    return StateSerializer::load(stream, parameters_);
}
