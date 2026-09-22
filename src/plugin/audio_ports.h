#pragma once

#include <cstdint>

#include <clap/clap.h>

class AudioPorts {
public:
    static uint32_t count(bool isInput) noexcept;
    static bool info(uint32_t index,
                     bool isInput,
                     bool allow64Bits,
                     clap_audio_port_info_t* info) noexcept;
};
