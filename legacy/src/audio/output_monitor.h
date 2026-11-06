#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "ring_buffer.h"
#include "../diagnostics/counters.h"

namespace windows_reac_asio {

struct OutputMonitorConfig {
    uint32_t sampleRate = 48000;
    uint32_t channelCount = 2;
    uint32_t bufferFrames = 8192;
};

class OutputMonitor {
public:
    OutputMonitor() = default;
    ~OutputMonitor();

    bool configure(const OutputMonitorConfig& config);
    bool start();
    void stop();
    bool writeInterleaved(const float* interleaved, uint32_t frames) noexcept;

    bool running() const noexcept { return running_.load(std::memory_order_acquire); }
    const std::string& lastError() const noexcept { return lastError_; }

private:
    void renderLoop();

    OutputMonitorConfig config_;
    RingBuffer ringBuffer_;
    AudioCounters counters_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    std::string lastError_;
};

} // namespace windows_reac_asio
