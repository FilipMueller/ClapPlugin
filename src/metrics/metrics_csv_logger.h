#pragma once

#include "realtime_metrics.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <string>
#include <thread>

// ---------------------------------------------------------------------------
// MetricsCsvLogger
//
// Polls RealtimeMetrics from a dedicated worker thread and writes three CSV
// files per measurement run:
//
//   <base>_timeline.csv    one row per poll; shows how the statistics settle
//   <base>_summary.csv     a single row holding the final result of the run
//   <base>_histogram.csv   the full per-block distribution, one row per bin
//
// The summary is a one-row CSV on purpose: concatenating the summary files of
// every run yields a single table that can be pasted straight into the
// evaluation chapter, with no manual transcription step.
//
// All three files carry the sample rate, block size, complexity setting, build
// type and inner-loop mode as columns, so a file is self-describing even if it
// is renamed or moved.
//
// Files are rewritten on every poll, so a run can be collected at any time
// after the transport stops without deactivating the plugin. When the run id
// changes (the Run Marker parameter was moved), the current files are
// finalised and a new set is started automatically.
//
// The worker thread does all file I/O. The audio thread never touches it.
// ---------------------------------------------------------------------------

class MetricsCsvLogger {
public:
    MetricsCsvLogger() = default;

    MetricsCsvLogger(const MetricsCsvLogger&) = delete;
    MetricsCsvLogger& operator=(const MetricsCsvLogger&) = delete;

    ~MetricsCsvLogger() noexcept;

    bool start(const RealtimeMetrics& metrics,
               std::chrono::milliseconds interval =
                   std::chrono::milliseconds(250)) noexcept;

    void stop() noexcept;

    bool isRunning() const noexcept { return running_.load(); }

    const std::string& outputDirectory() const noexcept { return outputDirectory_; }

private:
    void run() noexcept;

    void beginRunFiles(const RealtimeMetricsSnapshot& snapshot);
    void appendTimelineRow(const RealtimeMetricsSnapshot& snapshot);
    void writeSummary(const RealtimeMetricsSnapshot& snapshot) const;
    void writeHistogram(const RealtimeMetricsSnapshot& snapshot) const;
    void finaliseRun(const RealtimeMetricsSnapshot& snapshot);

    static std::string resolveOutputDirectory();
    static std::string timestamp();
    static const char* buildType() noexcept;
    static const char* innerLoopMode() noexcept;

private:
    std::atomic<bool> running_ {false};

    const RealtimeMetrics* metrics_ = nullptr;

    std::chrono::milliseconds interval_ {250};
    std::thread workerThread_;

    std::string outputDirectory_;

    // State for the run currently being written.
    std::string basePath_;
    std::ofstream timelineFile_;
    uint32_t currentRunId_ = 0;
    bool runFilesOpen_ = false;
    RealtimeMetricsSnapshot lastSnapshot_ {};
};
