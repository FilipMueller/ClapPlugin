#include "metrics_csv_logger.h"

#include <cstdlib>
#include <fstream>
#include <iomanip>

MetricsCsvLogger::~MetricsCsvLogger() noexcept {
    stop();
}

bool MetricsCsvLogger::start(const RealtimeMetrics& metrics,
                             std::chrono::milliseconds interval) noexcept {
    if (running_.load()) {
        return true;
    }

    metrics_ = &metrics;
    interval_ = interval;
    outputPath_ = createDefaultOutputPath();

    running_.store(true);

    try {
        workerThread_ = std::thread(&MetricsCsvLogger::run, this);
    } catch (...) {
        running_.store(false);
        metrics_ = nullptr;
        return false;
    }

    return true;
}

void MetricsCsvLogger::stop() noexcept {
    running_.store(false);

    if (workerThread_.joinable()) {
        workerThread_.join();
    }

    metrics_ = nullptr;
}

void MetricsCsvLogger::run() noexcept {
    if (metrics_ == nullptr) {
        return;
    }

    std::ofstream file(outputPath_, std::ios::out | std::ios::trunc);

    if (!file.is_open()) {
        running_.store(false);
        return;
    }

    writeHeader(file);

    while (running_.load()) {
        const RealtimeMetricsSnapshot snapshot = metrics_->snapshot();

        writeRow(file, snapshot);
        file.flush();

        std::this_thread::sleep_for(interval_);
    }
}

std::string MetricsCsvLogger::createDefaultOutputPath() {
#ifdef _WIN32
    const char* temp = std::getenv("TEMP");

    if (temp == nullptr) {
        temp = std::getenv("TMP");
    }

    std::string directory = temp != nullptr ? temp : ".";
#else
    const char* temp = std::getenv("TMPDIR");

    std::string directory = temp != nullptr ? temp : "/tmp";
#endif

    if (!directory.empty()) {
        const char last = directory.back();

        if (last != '/' && last != '\\') {
#ifdef _WIN32
            directory += "\\";
#else
            directory += "/";
#endif
        }
    }

    return directory + "FilipDelay_metrics.csv";
}

void MetricsCsvLogger::writeHeader(std::ofstream& file) {
    file
        << "processed_blocks,"
        << "sample_rate,"
        << "current_block_size,"
        << "max_block_size,"
        << "channel_count,"
        << "audio_format_bits,"
        << "available_block_time_ms,"
        << "last_process_time_ms,"
        << "average_process_time_ms,"
        << "max_process_time_ms,"
        << "realtime_load_percent,"
        << "overload_count,"
        << "delay_ms,"
        << "delay_samples,"
        << "feedback,"
        << "mix,"
        << "dsp_complexity"
        << '\n';
}

void MetricsCsvLogger::writeRow(std::ofstream& file,
                                const RealtimeMetricsSnapshot& snapshot) {
    file << std::fixed << std::setprecision(6);

    file
        << "processed_blocks=" << snapshot.processedBlocks << "; "
        << "sample_rate_hz=" << snapshot.sampleRate << "; "
        << "current_block_size_samples=" << snapshot.currentBlockSize << "; "
        << "max_block_size_samples=" << snapshot.maxBlockSize << "; "
        << "channel_count=" << snapshot.channelCount << "; "
        << "audio_format_bits=" << snapshot.audioFormatBits << "; "
        << "available_block_time_ms=" << snapshot.availableBlockTimeMs << "; "
        << "last_process_time_ms=" << snapshot.lastProcessTimeMs << "; "
        << "average_process_time_ms=" << snapshot.averageProcessTimeMs << "; "
        << "max_process_time_ms=" << snapshot.maxProcessTimeMs << "; "
        << "realtime_load_percent=" << snapshot.realtimeLoadPercent << "; "
        << "overload_count=" << snapshot.overloadCount << "; "
        << "delay_ms=" << snapshot.delayMs << "; "
        << "delay_samples=" << snapshot.delaySamples << "; "
        << "feedback=" << snapshot.feedback << "; "
        << "mix=" << snapshot.mix << "; "
        << "dsp_complexity=" << snapshot.dspComplexity
        << '\n';
}
