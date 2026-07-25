#pragma once

#include "realtime_metrics.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

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

    bool isRunning() const noexcept {
        return running_.load();
    }

    const std::string& outputPath() const noexcept {
        return outputPath_;
    }

private:
    void run() noexcept;

    static std::string createDefaultOutputPath();
    static void writeHeader(std::ofstream& file);
    static void writeRow(std::ofstream& file,
                         const RealtimeMetricsSnapshot& snapshot);

private:
    std::atomic<bool> running_ {false};

    const RealtimeMetrics* metrics_ = nullptr;

    std::chrono::milliseconds interval_ {250};
    std::thread workerThread_;

    std::string outputPath_;
};
