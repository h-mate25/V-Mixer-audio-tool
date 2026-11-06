#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "../audio/ring_buffer.h"
#include "../diagnostics/counters.h"
#include "reac_return_map.h"

namespace windows_reac_asio {

struct ReacTransmitterConfig {
    std::string interfaceSelector;
    uint32_t sampleRate = 44100;
    uint32_t channelCount = 8;
    uint32_t bufferFrames = 8192;
    uint32_t returnMap = kDefaultReacReturnMapMode;
};

class ReacTransmitter {
public:
    ReacTransmitter();
    ~ReacTransmitter();

    bool configure(const ReacTransmitterConfig& config);
    bool start();
    void stop();

    bool writeInterleaved(const float* interleaved, uint32_t sourceChannels, uint32_t frames) noexcept;

    bool running() const noexcept { return running_.load(std::memory_order_acquire); }
    const std::string& lastError() const noexcept { return lastError_; }
    AudioCounters& counters() noexcept { return counters_; }

private:
    void workerLoop();
    bool openDevice(std::string& deviceName);
    bool runStableSlaveSession(const std::string& deviceName);

    ReacTransmitterConfig config_{};
    RingBuffer ringBuffer_;
    AudioCounters counters_;
    std::vector<float> writeScratch_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    std::thread worker_;
    std::string lastError_;
};

} // namespace windows_reac_asio
