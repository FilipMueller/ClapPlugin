cmake_minimum_required(VERSION 3.15)
project(Delay VERSION 0.1.0 LANGUAGES C CXX)

# Measurements taken from a -O0 build characterise the compiler, not the DSP.
# Default to an optimised build so that an accidental configure cannot silently
# invalidate a measurement campaign.
if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
            Debug Release RelWithDebInfo MinSizeRel)
endif()

# Rebuilds the DSP coefficients for every sample instead of once per event
# segment. Only useful for the inner-loop comparison measurement.
option(DELAY_NAIVE_INNER_LOOP
        "Recompute DSP coefficients per sample (measurement reference)" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

add_subdirectory(libs/clap)
add_subdirectory(libs/clap-helpers)

add_library(Delay MODULE
        src/entry.cpp

        src/plugin/delay_plugin.cpp
        src/plugin/delay_plugin.h
        src/plugin/plugin_descriptor.cpp
        src/plugin/clap_callbacks.cpp
        src/plugin/clap_extensions.cpp
        src/plugin/audio_ports.cpp
        src/plugin/audio_ports.h
        src/plugin/parameter_manager.cpp
        src/plugin/parameter_manager.h
        src/plugin/state_serializer.cpp
        src/plugin/state_serializer.h
        src/plugin/event_segments.h

        src/dsp/delay_processor.cpp
        src/dsp/delay_processor.h
        src/dsp/delay_parameters.h

        src/metrics/realtime_metrics.h
        src/metrics/metrics_csv_logger.cpp
        src/metrics/metrics_csv_logger.h

        src/utils/scoped_process_timer.h
        src/utils/denormal_guard.h
)

target_include_directories(Delay PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(Delay PRIVATE clap clap-helpers)

target_compile_definitions(Delay PRIVATE
        DELAY_VERSION="${PROJECT_VERSION}"
)

if (DELAY_NAIVE_INNER_LOOP)
    target_compile_definitions(Delay PRIVATE DELAY_NAIVE_INNER_LOOP)
endif()

message(STATUS "Delay build type      : ${CMAKE_BUILD_TYPE}")
message(STATUS "Delay naive inner loop: ${DELAY_NAIVE_INNER_LOOP}")

set_target_properties(Delay PROPERTIES
        PREFIX ""
        OUTPUT_NAME "Delay"
        SUFFIX ".clap"
)

if (MINGW)
    target_link_options(Delay PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
    )
endif()