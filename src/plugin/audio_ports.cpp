#include "plugin/audio_ports.h"

#include <cstdio>
#include <cstring>

#include <clap/ext/audio-ports.h>

namespace {
constexpr uint32_t MainAudioPortId = 0;
}

uint32_t AudioPorts::count(bool) noexcept {
    return 1;
}

bool AudioPorts::info(uint32_t index, bool isInput, clap_audio_port_info_t* info) noexcept {
    if (!info || index != 0) {
        return false;
    }

    std::memset(info, 0, sizeof(*info));
    info->id = MainAudioPortId;
    std::snprintf(info->name, sizeof(info->name), "%s", isInput ? "Stereo In" : "Stereo Out");
    info->channel_count = 2;
    info->flags = CLAP_AUDIO_PORT_IS_MAIN
                | CLAP_AUDIO_PORT_SUPPORTS_64BITS
                | CLAP_AUDIO_PORT_PREFERS_64BITS;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
