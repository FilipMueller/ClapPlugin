#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

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

    enum ParamId : clap_id {
        ParamBypass = 0,
        ParamDelayMs = 1,
        ParamFeedback = 2,
        ParamMix = 3,
        ParamOutputDb = 4,
    };

    struct ParameterDefinition {
        clap_id id;
        const char* name;
        const char* module;
        double minValue;
        double maxValue;
        double defaultValue;
        clap_param_info_flags flags;
    };

    static const clap_plugin_descriptor_t* descriptor() noexcept;
    static const ParameterDefinition* findParameter(clap_id id) noexcept;
    static double clampToParameterRange(clap_id id, double value) noexcept;
    static float dbToGain(double db) noexcept;

    void handleEvent(const clap_event_header_t* event) noexcept;
    void setParameter(clap_id id, double value) noexcept;
    double getParameter(clap_id id) const noexcept;
    void clearDelayBuffer() noexcept;
    void processSample(float inL, float inR, float& outL, float& outR) noexcept;

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
    clap_plugin_t plugin_{};
    const clap_host_t* host_{};

    double sampleRate_ = 44100.0;
    uint32_t maxFrameCount_ = 0;
    bool active_ = false;

    static constexpr double maxDelaySeconds_ = 5.0;
    std::vector<float> delayBufferL_;
    std::vector<float> delayBufferR_;
    uint32_t delayBufferSize_ = 0;
    uint32_t writePosition_ = 0;

    std::atomic<double> bypass_ {0.0};
    std::atomic<double> delayMs_ {350.0};
    std::atomic<double> feedback_ {0.35};
    std::atomic<double> mix_ {0.35};
    std::atomic<double> outputDb_ {0.0};
};

const clap_plugin_t* createDelayPlugin(const clap_host_t* host) noexcept;
const clap_plugin_descriptor_t* getDelayPluginDescriptor() noexcept;
