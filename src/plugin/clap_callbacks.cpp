#include "plugin/delay_plugin.h"

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
