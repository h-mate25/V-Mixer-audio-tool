#pragma once

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "ring_buffer.h"
#include "../diagnostics/counters.h"

namespace windows_reac_asio {

struct AudioEngineConfig {
    uint32_t channelCount = 40;
    uint32_t sampleRate = 48000;
    uint32_t asioBufferSize = 512;
    uint32_t jitterBufferSamples = 8192;
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();

    bool configure(const AudioEngineConfig& config);
    void clear() noexcept;
    void startTestProducer();
    void stopTestProducer();
    bool writeInputInterleaved(const float* interleaved, uint32_t frames) noexcept;
    bool writeInputSilence(uint32_t frames) noexcept;

    void readInput(
        const uint32_t* sourceChannels,
        float** destinationChannels,
        uint32_t destinationChannelCount,
        uint32_t frames) noexcept;

    const AudioEngineConfig& config() const noexcept { return config_; }
    AudioCounters& counters() noexcept { return counters_; }

private:
    void producerLoop();

    AudioEngineConfig config_;
    RingBuffer ringBuffer_;
    AudioCounters counters_;
    std::atomic<bool> producerRunning_{false};
    std::thread producerThread_;
    std::vector<float> producerBlock_;
};

} // namespace windows_reac_asio
