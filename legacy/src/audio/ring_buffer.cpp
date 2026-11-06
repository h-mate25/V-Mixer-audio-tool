#include "ring_buffer.h"

#include <algorithm>
#include <cstring>

namespace windows_reac_asio {

bool RingBuffer::configure(uint32_t channelCount, uint32_t capacityFrames) {
    if (channelCount == 0 || capacityFrames == 0) {
        return false;
    }

    channelCount_ = channelCount;
    capacityFrames_ = capacityFrames;
    data_.assign(static_cast<size_t>(channelCount_) * capacityFrames_, 0.0f);
    clear();
    return true;
}

void RingBuffer::clear() noexcept {
    if (!data_.empty()) {
        std::fill(data_.begin(), data_.end(), 0.0f);
    }
    readFrame_.store(0, std::memory_order_release);
    writeFrame_.store(0, std::memory_order_release);
}

uint32_t RingBuffer::availableFrames() const noexcept {
    const uint64_t read = readFrame_.load(std::memory_order_acquire);
    const uint64_t write = writeFrame_.load(std::memory_order_acquire);
    return static_cast<uint32_t>(std::min<uint64_t>(write - read, capacityFrames_));
}

bool RingBuffer::writeInterleaved(const float* interleaved, uint32_t frames, AudioCounters& counters) noexcept {
    if (!interleaved || frames == 0 || channelCount_ == 0 || capacityFrames_ == 0) {
        return false;
    }

    const uint64_t read = readFrame_.load(std::memory_order_acquire);
    const uint64_t write = writeFrame_.load(std::memory_order_relaxed);
    const uint64_t available = write - read;
    const uint64_t freeFrames = capacityFrames_ > available ? capacityFrames_ - available : 0;

    // Overflow policy: discard the new producer block. This keeps ownership
    // single-producer/single-consumer and avoids waits in the ASIO callback.
    if (frames > freeFrames) {
        counters.overflowCount.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    for (uint32_t frame = 0; frame < frames; ++frame) {
        const uint32_t ringFrame = static_cast<uint32_t>((write + frame) % capacityFrames_);
        for (uint32_t channel = 0; channel < channelCount_; ++channel) {
            data_[static_cast<size_t>(channel) * capacityFrames_ + ringFrame] =
                interleaved[static_cast<size_t>(frame) * channelCount_ + channel];
        }
    }

    writeFrame_.store(write + frames, std::memory_order_release);
    counters.framesReceived.fetch_add(frames, std::memory_order_relaxed);
    return true;
}

bool RingBuffer::writeSilence(uint32_t frames, AudioCounters& counters) noexcept {
    if (frames == 0 || channelCount_ == 0 || capacityFrames_ == 0) {
        return false;
    }

    const uint64_t read = readFrame_.load(std::memory_order_acquire);
    const uint64_t write = writeFrame_.load(std::memory_order_relaxed);
    const uint64_t available = write - read;
    const uint64_t freeFrames = capacityFrames_ > available ? capacityFrames_ - available : 0;

    if (frames > freeFrames) {
        counters.overflowCount.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    for (uint32_t frame = 0; frame < frames; ++frame) {
        const uint32_t ringFrame = static_cast<uint32_t>((write + frame) % capacityFrames_);
        for (uint32_t channel = 0; channel < channelCount_; ++channel) {
            data_[static_cast<size_t>(channel) * capacityFrames_ + ringFrame] = 0.0f;
        }
    }

    writeFrame_.store(write + frames, std::memory_order_release);
    counters.framesReceived.fetch_add(frames, std::memory_order_relaxed);
    return true;
}

uint32_t RingBuffer::readPlanar(
    const uint32_t* sourceChannels,
    float** destinationChannels,
    uint32_t destinationChannelCount,
    uint32_t frames,
    AudioCounters& counters) noexcept {
    if (!sourceChannels || !destinationChannels || frames == 0) {
        return 0;
    }

    const uint64_t read = readFrame_.load(std::memory_order_relaxed);
    const uint64_t write = writeFrame_.load(std::memory_order_acquire);
    const uint32_t available = static_cast<uint32_t>(std::min<uint64_t>(write - read, frames));

    for (uint32_t destination = 0; destination < destinationChannelCount; ++destination) {
        float* out = destinationChannels[destination];
        if (!out) {
            continue;
        }

        const uint32_t source = sourceChannels[destination];
        if (source >= channelCount_) {
            std::fill(out, out + frames, 0.0f);
            continue;
        }

        for (uint32_t frame = 0; frame < available; ++frame) {
            const uint32_t ringFrame = static_cast<uint32_t>((read + frame) % capacityFrames_);
            out[frame] = data_[static_cast<size_t>(source) * capacityFrames_ + ringFrame];
        }

        if (available < frames) {
            std::fill(out + available, out + frames, 0.0f);
        }
    }

    readFrame_.store(read + available, std::memory_order_release);
    counters.framesDelivered.fetch_add(frames, std::memory_order_relaxed);
    if (available < frames) {
        counters.underrunCount.fetch_add(1, std::memory_order_relaxed);
    }
    return available;
}

} // namespace windows_reac_asio
