#pragma once

#include "plugin/parameter_manager.h"

#include <clap/clap.h>

class StateSerializer {
public:
    static bool save(const clap_ostream_t* stream, const ParameterManager& parameters) noexcept;
    static bool load(const clap_istream_t* stream, ParameterManager& parameters) noexcept;
};
