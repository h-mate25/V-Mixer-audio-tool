#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "../diagnostics/counters.h"

namespace windows_reac_asio {

class RingBuffer {
public:
    RingBuffer() = default;

    bool configure(uint32_t channelCount, uint32_t capacityFrames);
    void clear() noexcept;

    uint32_t channelCount() const noexcept { return channelCount_; }
    uint32_t capacityFrames() const noexcept { return capacityFrames_; }
    uint32_t availableFrames() const noexcept;

    bool writeInterleaved(const float* interleaved, uint32_t frames, AudioCounters& counters) noexcept;
    bool writeSilence(uint32_t frames, AudioCounters& counters) noexcept;
    uint32_t readPlanar(
        const uint32_t* sourceChannels,
        float** destinationChannels,
        uint32_t destinationChannelCount,
        uint32_t frames,
        AudioCounters& counters) noexcept;

private:
    uint32_t channelCount_ = 0;
    uint32_t capacityFrames_ = 0;
    std::vector<float> data_;
    std::atomic<uint64_t> readFrame_{0};
    std::atomic<uint64_t> writeFrame_{0};
};

} // namespace windows_reac_asio
