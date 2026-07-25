cmake_minimum_required(VERSION 3.15)
project(FilipDelay VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

add_subdirectory(libs/clap)
add_subdirectory(libs/clap-helpers)

add_library(FilipDelay MODULE
        src/entry.cpp
        src/plugin.cpp
        src/plugin.h
        src/delay_parameters.h
        src/delay_processor.cpp
        src/delay_processor.h
        src/realtime_metrics.h
        src/scoped_process_timer.h
        src/metrics_csv_logger.h
        src/metrics_csv_logger.cpp
)

target_link_libraries(FilipDelay PRIVATE clap clap-helpers)

target_compile_definitions(FilipDelay PRIVATE
        FILIP_DELAY_VERSION="${PROJECT_VERSION}"
)

set_target_properties(FilipDelay PROPERTIES
        PREFIX ""
        OUTPUT_NAME "FilipDelay"
        SUFFIX ".clap"
)

if (MINGW)
    target_link_options(FilipDelay PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
    )
endif()