#pragma once

#include <atomic>
#include <cstdint>

namespace windows_reac_asio {

struct AudioCounters {
    std::atomic<uint64_t> underrunCount{0};
    std::atomic<uint64_t> overflowCount{0};
    std::atomic<uint64_t> framesReceived{0};
    std::atomic<uint64_t> framesDelivered{0};
    std::atomic<uint64_t> packetDropCount{0};

    void reset() noexcept {
        underrunCount.store(0, std::memory_order_relaxed);
        overflowCount.store(0, std::memory_order_relaxed);
        framesReceived.store(0, std::memory_order_relaxed);
        framesDelivered.store(0, std::memory_order_relaxed);
        packetDropCount.store(0, std::memory_order_relaxed);
    }
};

struct AudioCountersSnapshot {
    uint64_t underrunCount = 0;
    uint64_t overflowCount = 0;
    uint64_t framesReceived = 0;
    uint64_t framesDelivered = 0;
    uint64_t packetDropCount = 0;
};

inline AudioCountersSnapshot snapshotAudioCounters(const AudioCounters& counters) noexcept {
    AudioCountersSnapshot snapshot;
    snapshot.underrunCount = counters.underrunCount.load(std::memory_order_relaxed);
    snapshot.overflowCount = counters.overflowCount.load(std::memory_order_relaxed);
    snapshot.framesReceived = counters.framesReceived.load(std::memory_order_relaxed);
    snapshot.framesDelivered = counters.framesDelivered.load(std::memory_order_relaxed);
    snapshot.packetDropCount = counters.packetDropCount.load(std::memory_order_relaxed);
    return snapshot;
}

} // namespace windows_reac_asio
