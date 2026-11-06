#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace windows_reac_asio {

class PcapSender {
public:
    class TimedQueue;

    PcapSender();
    ~PcapSender();

    PcapSender(const PcapSender&) = delete;
    PcapSender& operator=(const PcapSender&) = delete;

    bool available() const noexcept;
    const std::string& lastError() const noexcept { return lastError_; }

    bool open(const std::string& deviceName);
    bool send(const uint8_t* packet, size_t packetBytes);
    std::unique_ptr<TimedQueue> prepareTimedQueue(const std::vector<uint8_t>& packets, uint32_t packetBytes, uint32_t packetsPerSecond);
    bool transmitTimedQueue(const TimedQueue& queue);
    bool sendTimedQueue(const std::vector<uint8_t>& packets, uint32_t packetBytes, uint32_t packetsPerSecond);
    void close() noexcept;

private:
    struct Impl;
    Impl* impl_ = nullptr;
    std::string lastError_;
};

class PcapSender::TimedQueue {
public:
    ~TimedQueue();

    TimedQueue(const TimedQueue&) = delete;
    TimedQueue& operator=(const TimedQueue&) = delete;

private:
    friend class PcapSender;

    TimedQueue(Impl* impl, void* queue) noexcept
        : impl_(impl), queue_(queue) {}

    Impl* impl_ = nullptr;
    void* queue_ = nullptr;
};

} // namespace windows_reac_asio
