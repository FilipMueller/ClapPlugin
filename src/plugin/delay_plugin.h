#pragma once

#include "dsp/delay_parameters.h"
#include "dsp/delay_processor.h"
#include "metrics/metrics_csv_logger.h"
#include "metrics/realtime_metrics.h"
#include "plugin/parameter_manager.h"

#include <cstdint>

#include <clap/clap.h>

class DelayPlugin {
public:
    explicit DelayPlugin(const clap_host_t* host) noexcept;
    ~DelayPlugin() = default;

    DelayPlugin(const DelayPlugin&) = delete;
    DelayPlugin& operator=(const DelayPlugin&) = delete;

    const clap_plugin_t* clapPlugin() const noexcept { return &plugin_; }

    bool init() noexcept;
    bool activate(double sampleRate, uint32_t minFrameCount, uint32_t maxFrameCount) noexcept;
    void deactivate() noexcept;
    bool startProcessing() noexcept;
    void stopProcessing() noexcept;
    void reset() noexcept;
    clap_process_status process(const clap_process_t* process) noexcept;
    const void* getExtension(const char* id) noexcept;
    void onMainThread() noexcept;

    uint32_t audioPortsCount(bool isInput) const noexcept;
    bool audioPortsInfo(uint32_t index, bool isInput, clap_audio_port_info_t* info) const noexcept;

    uint32_t paramsCount() const noexcept;
    bool paramsInfo(uint32_t paramIndex, clap_param_info_t* info) const noexcept;
    bool paramsValue(clap_id paramId, double* value) const noexcept;
    bool paramsValueToText(clap_id paramId, double value, char* display, uint32_t size) const noexcept;
    bool paramsTextToValue(clap_id paramId, const char* display, double* value) const noexcept;
    void paramsFlush(const clap_input_events_t* in, const clap_output_events_t* out) noexcept;

    bool stateSave(const clap_ostream_t* stream) const noexcept;
    bool stateLoad(const clap_istream_t* stream) noexcept;

    static const clap_plugin_descriptor_t* descriptor() noexcept;
    static DelayPlugin& from(const clap_plugin_t* plugin) noexcept;

    static bool CLAP_ABI clapInit(const clap_plugin_t* plugin) noexcept;
    static void CLAP_ABI clapDestroy(const clap_plugin_t* plugin) noexcept;
    static bool CLAP_ABI clapActivate(const clap_plugin_t* plugin,
                                      double sampleRate,
                                      uint32_t minFrameCount,
                                      uint32_t maxFrameCount) noexcept;
    static void CLAP_ABI clapDeactivate(const clap_plugin_t* plugin) noexcept;
    static bool CLAP_ABI clapStartProcessing(const clap_plugin_t* plugin) noexcept;
    static void CLAP_ABI clapStopProcessing(const clap_plugin_t* plugin) noexcept;
    static void CLAP_ABI clapReset(const clap_plugin_t* plugin) noexcept;
    static clap_process_status CLAP_ABI clapProcess(const clap_plugin_t* plugin,
                                                    const clap_process_t* process) noexcept;
    static const void* CLAP_ABI clapGetExtension(const clap_plugin_t* plugin,
                                                 const char* id) noexcept;
    static void CLAP_ABI clapOnMainThread(const clap_plugin_t* plugin) noexcept;

    static uint32_t CLAP_ABI audioPortsCountCallback(const clap_plugin_t* plugin,
                                                     bool isInput) noexcept;
    static bool CLAP_ABI audioPortsInfoCallback(const clap_plugin_t* plugin,
                                                uint32_t index,
                                                bool isInput,
                                                clap_audio_port_info_t* info) noexcept;

    static uint32_t CLAP_ABI paramsCountCallback(const clap_plugin_t* plugin) noexcept;
    static bool CLAP_ABI paramsInfoCallback(const clap_plugin_t* plugin,
                                            uint32_t paramIndex,
                                            clap_param_info_t* info) noexcept;
    static bool CLAP_ABI paramsValueCallback(const clap_plugin_t* plugin,
                                             clap_id paramId,
                                             double* value) noexcept;
    static bool CLAP_ABI paramsValueToTextCallback(const clap_plugin_t* plugin,
                                                   clap_id paramId,
                                                   double value,
                                                   char* display,
                                                   uint32_t size) noexcept;
    static bool CLAP_ABI paramsTextToValueCallback(const clap_plugin_t* plugin,
                                                   clap_id paramId,
                                                   const char* display,
                                                   double* value) noexcept;
    static void CLAP_ABI paramsFlushCallback(const clap_plugin_t* plugin,
                                             const clap_input_events_t* in,
                                             const clap_output_events_t* out) noexcept;

    static bool CLAP_ABI stateSaveCallback(const clap_plugin_t* plugin,
                                           const clap_ostream_t* stream) noexcept;
    static bool CLAP_ABI stateLoadCallback(const clap_plugin_t* plugin,
                                           const clap_istream_t* stream) noexcept;

private:
    DelayParameters currentDelayParameters() const noexcept;
    void updateMetricsForCurrentParameters() noexcept;
    void handleEvent(const clap_event_header_t* event) noexcept;

private:
    clap_plugin_t plugin_{};
    const clap_host_t* host_{};

    double sampleRate_ = 44100.0;
    uint32_t maxFrameCount_ = 0;
    bool active_ = false;

    ParameterManager parameters_;
    DelayProcessor delayProcessor_;
    RealtimeMetrics metrics_;
    MetricsCsvLogger metricsLogger_;
};

const clap_plugin_t* createDelayPlugin(const clap_host_t* host) noexcept;
const clap_plugin_descriptor_t* getDelayPluginDescriptor() noexcept;
