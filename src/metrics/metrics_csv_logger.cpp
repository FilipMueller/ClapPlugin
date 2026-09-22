#include "metrics_csv_logger.h"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace {

void writeRunColumns(std::ostream& out, const RealtimeMetricsSnapshot& s) {
    out << s.runMarker << ','
        << s.sampleRate << ','
        << s.maxBlockSize << ','
        << s.currentBlockSize << ','
        << s.channelCount << ','
        << s.audioFormatBits << ','
        << s.requestedFormatBits << ','
        << s.availableBlockTimeMs << ','
        << s.dspComplexity << ','
        << s.delayMs << ','
        << s.delaySamples << ','
        << s.feedback << ','
        << s.mix << ','
        << s.outputDb;
}

constexpr const char* kRunColumnHeader =
    "run_marker,sample_rate_hz,max_block_size,current_block_size,channel_count,"
    "audio_format_bits,requested_format_bits,available_block_time_ms,dsp_complexity,delay_ms,"
    "delay_samples,feedback,mix,output_db";

} // namespace

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

    try {
        outputDirectory_ = resolveOutputDirectory();
        std::filesystem::create_directories(outputDirectory_);
    } catch (...) {
        metrics_ = nullptr;
        return false;
    }

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

    while (running_.load()) {
        try {
            const RealtimeMetricsSnapshot snapshot = metrics_->snapshot();

            if (!runFilesOpen_ || snapshot.runId != currentRunId_) {
                if (runFilesOpen_) {
                    finaliseRun(lastSnapshot_);
                }
                beginRunFiles(snapshot);
            }

            if (snapshot.blocksMeasured > 0) {
                appendTimelineRow(snapshot);
                writeSummary(snapshot);
                writeHistogram(snapshot);
                lastSnapshot_ = snapshot;
            }
        } catch (...) {
        }

        std::this_thread::sleep_for(interval_);
    }

    try {
        if (runFilesOpen_) {
            finaliseRun(lastSnapshot_);
        }
    } catch (...) {
    }
}

void MetricsCsvLogger::beginRunFiles(const RealtimeMetricsSnapshot& snapshot) {
    currentRunId_ = snapshot.runId;

    std::ostringstream name;
    name << "Delay"
         << "_sr" << static_cast<long long>(snapshot.sampleRate)
         << "_buf" << snapshot.maxBlockSize
         << "_run" << snapshot.runMarker
         << '_' << timestamp();

    basePath_ = outputDirectory_ + name.str();

    timelineFile_.close();
    timelineFile_.clear();

    runFilesOpen_ = true;
    lastSnapshot_ = snapshot;
}

void MetricsCsvLogger::appendTimelineRow(const RealtimeMetricsSnapshot& s) {
    if (!timelineFile_.is_open()) {
        timelineFile_.open(basePath_ + "_timeline.csv", std::ios::out | std::ios::trunc);
        if (!timelineFile_.is_open()) {
            return;
        }

        timelineFile_ << std::fixed << std::setprecision(6);
        timelineFile_ << kRunColumnHeader
                      << ",blocks_total,blocks_measured,frames_total,"
                         "warmup_frames,warmup_complete,last_process_time_ms,"
                         "min_process_time_ms,mean_process_time_ms,"
                         "max_process_time_ms,mean_load_percent,max_load_percent,"
                         "overload_count,overload_rate_percent\n";
    }

    writeRunColumns(timelineFile_, s);

    timelineFile_ << ',' << s.blocksTotal
                  << ',' << s.blocksMeasured
                  << ',' << s.framesTotal
                  << ',' << s.warmupFrames
                  << ',' << (s.warmupComplete ? 1 : 0)
                  << ',' << s.lastProcessTimeMs
                  << ',' << s.minProcessTimeMs
                  << ',' << s.averageProcessTimeMs
                  << ',' << s.maxProcessTimeMs
                  << ',' << s.loadPercent(s.averageProcessTimeMs)
                  << ',' << s.loadPercent(s.maxProcessTimeMs)
                  << ',' << s.overloadCount
                  << ',' << s.overloadRatePercent()
                  << '\n';

    timelineFile_.flush();
}

void MetricsCsvLogger::writeSummary(const RealtimeMetricsSnapshot& s) const {
    std::ofstream file(basePath_ + "_summary.csv", std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        return;
    }

    file << std::fixed << std::setprecision(6);

    file << kRunColumnHeader
         << ",build_type,inner_loop_mode,warmup_seconds,blocks_total,"
            "blocks_measured,frames_measured,min_ms,mean_ms,p50_ms,p90_ms,p99_ms,"
            "p999_ms,max_ms,mean_load_percent,p99_load_percent,max_load_percent,"
            "overload_count,overload_rate_percent,histogram_underflow,"
            "histogram_overflow\n";

    const double p50 = s.percentileMs(0.50);
    const double p90 = s.percentileMs(0.90);
    const double p99 = s.percentileMs(0.99);
    const double p999 = s.percentileMs(0.999);

    writeRunColumns(file, s);

    file << ',' << buildType()
         << ',' << innerLoopMode()
         << ',' << (s.sampleRate > 0.0
                        ? static_cast<double>(s.warmupFrames) / s.sampleRate
                        : 0.0)
         << ',' << s.blocksTotal
         << ',' << s.blocksMeasured
         << ',' << s.framesMeasured
         << ',' << s.minProcessTimeMs
         << ',' << s.averageProcessTimeMs
         << ',' << p50
         << ',' << p90
         << ',' << p99
         << ',' << p999
         << ',' << s.maxProcessTimeMs
         << ',' << s.loadPercent(s.averageProcessTimeMs)
         << ',' << s.loadPercent(p99)
         << ',' << s.loadPercent(s.maxProcessTimeMs)
         << ',' << s.overloadCount
         << ',' << s.overloadRatePercent()
         << ',' << s.histogramUnderflow
         << ',' << s.histogramOverflow
         << '\n';
}

void MetricsCsvLogger::writeHistogram(const RealtimeMetricsSnapshot& s) const {
    std::ofstream file(basePath_ + "_histogram.csv", std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        return;
    }

    file << std::fixed << std::setprecision(9);

    file << "run_marker,sample_rate_hz,max_block_size,dsp_complexity,"
            "bin_index,bin_lower_ms,bin_upper_ms,count,cumulative,"
            "cumulative_fraction\n";

    uint64_t cumulative = s.histogramUnderflow;
    const double total =
        s.blocksMeasured > 0 ? static_cast<double>(s.blocksMeasured) : 1.0;

    for (int i = 0; i < RealtimeMetricsSnapshot::kBinCount; ++i) {
        const uint32_t count = s.bins[i];
        cumulative += count;

        if (count == 0) {
            continue;
        }

        file << s.runMarker << ','
             << s.sampleRate << ','
             << s.maxBlockSize << ','
             << s.dspComplexity << ','
             << i << ','
             << RealtimeMetricsSnapshot::binLowerEdgeMs(i) << ','
             << RealtimeMetricsSnapshot::binUpperEdgeMs(i) << ','
             << count << ','
             << cumulative << ','
             << static_cast<double>(cumulative) / total
             << '\n';
    }
}

void MetricsCsvLogger::finaliseRun(const RealtimeMetricsSnapshot& snapshot) {
    if (snapshot.blocksMeasured > 0) {
        writeSummary(snapshot);
        writeHistogram(snapshot);
    }

    timelineFile_.flush();
    timelineFile_.close();
    timelineFile_.clear();
    runFilesOpen_ = false;
}

std::string MetricsCsvLogger::resolveOutputDirectory() {
    if (const char* override = std::getenv("DELAY_METRICS_DIR")) {
        std::string directory = override;
        if (!directory.empty()) {
            const char last = directory.back();
            if (last != '/' && last != '\\') {
                directory += std::filesystem::path::preferred_separator;
            }
            return directory;
        }
    }

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
            directory += std::filesystem::path::preferred_separator;
        }
    }

    directory += "DelayMetrics";
    directory += std::filesystem::path::preferred_separator;

    return directory;
}

std::string MetricsCsvLogger::timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t asTime = std::chrono::system_clock::to_time_t(now);

    std::tm broken {};
#ifdef _WIN32
    localtime_s(&broken, &asTime);
#else
    localtime_r(&asTime, &broken);
#endif

    char buffer[32] {};
    std::snprintf(buffer, sizeof(buffer), "%04d%02d%02d-%02d%02d%02d",
                  broken.tm_year + 1900, broken.tm_mon + 1, broken.tm_mday,
                  broken.tm_hour, broken.tm_min, broken.tm_sec);

    return buffer;
}

const char* MetricsCsvLogger::buildType() noexcept {
#ifdef NDEBUG
    return "Release";
#else
    return "Debug";
#endif
}

const char* MetricsCsvLogger::innerLoopMode() noexcept {
#ifdef DELAY_NAIVE_INNER_LOOP
    return "naive";
#else
    return "hoisted";
#endif
}
