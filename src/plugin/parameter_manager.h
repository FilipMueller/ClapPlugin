#pragma once

#include "dsp/delay_parameters.h"

#include <atomic>
#include <cstdint>

#include <clap/clap.h>
#include <clap/events.h>

struct DelayParameterState {
    double bypass = 0.0;
    double delayMs = 350.0;
    double feedback = 0.35;
    double mix = 0.35;
    double outputDb = 0.0;
    double dspComplexity = 0.0;
    double runMarker = 0.0;
    double sampleFormat = 0.0;
};

class ParameterManager {
public:
    enum ParamId : clap_id {
        ParamBypass = 0,
        ParamDelayMs = 1,
        ParamFeedback = 2,
        ParamMix = 3,
        ParamOutputDb = 4,
        ParamDspComplexity = 5,
        ParamRunMarker = 6,
        ParamSampleFormat = 7,
    };

    struct Definition {
        clap_id id;
        const char* name;
        const char* module;
        double minValue;
        double maxValue;
        double defaultValue;
        clap_param_info_flags flags;
    };

    static uint32_t count() noexcept;
    static const Definition* find(clap_id id) noexcept;
    static double clamp(clap_id id, double value) noexcept;

    bool info(uint32_t index, clap_param_info_t* info) const noexcept;
    bool value(clap_id id, double* value) const noexcept;
    bool valueToText(clap_id id, double value, char* display, uint32_t size) const noexcept;
    bool textToValue(clap_id id, const char* display, double* value) const noexcept;

    void flush(const clap_input_events_t* in) noexcept;
    void handleEvent(const clap_event_header_t* event) noexcept;

    void set(clap_id id, double value) noexcept;
    double get(clap_id id) const noexcept;

    DelayParameters currentDelayParameters() const noexcept;
    DelayParameterState state() const noexcept;
    void setState(const DelayParameterState& state) noexcept;

private:
    std::atomic<double> bypass_ {0.0};
    std::atomic<double> delayMs_ {350.0};
    std::atomic<double> feedback_ {0.35};
    std::atomic<double> mix_ {0.35};
    std::atomic<double> outputDb_ {0.0};
    std::atomic<double> dspComplexity_ {0.0};
    std::atomic<double> runMarker_ {0.0};
    std::atomic<double> sampleFormat_ {0.0};
};
