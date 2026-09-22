#include "plugin/state_serializer.h"

#include <cstdint>

namespace {
constexpr uint32_t StateMagic = 0x46444C59;
constexpr uint32_t StateVersion = 4;

struct PersistedDelayState {
    uint32_t magic;
    uint32_t version;
    double bypass;
    double delayMs;
    double feedback;
    double mix;
    double outputDb;
    double dspComplexity;
    double runMarker;
    double sampleFormat;
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
} // namespace

bool StateSerializer::save(const clap_ostream_t* stream, const ParameterManager& parameters) noexcept {
    if (!stream) {
        return false;
    }

    const DelayParameterState params = parameters.state();
    const PersistedDelayState state {
        StateMagic,
        StateVersion,
        params.bypass,
        params.delayMs,
        params.feedback,
        params.mix,
        params.outputDb,
        params.dspComplexity,
        params.runMarker,
        params.sampleFormat,
    };

    return writeBytes(stream, &state, sizeof(state));
}

bool StateSerializer::load(const clap_istream_t* stream, ParameterManager& parameters) noexcept {
    if (!stream) {
        return false;
    }

    PersistedDelayState state {};
    if (!readBytes(stream, &state, sizeof(state)) ||
        state.magic != StateMagic ||
        state.version != StateVersion) {
        return false;
    }

    DelayParameterState params {};
    params.bypass = state.bypass;
    params.delayMs = state.delayMs;
    params.feedback = state.feedback;
    params.mix = state.mix;
    params.outputDb = state.outputDb;
    params.dspComplexity = state.dspComplexity;
    params.runMarker = state.runMarker;
    params.sampleFormat = state.sampleFormat;
    parameters.setState(params);
    return true;
}
