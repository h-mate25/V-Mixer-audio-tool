#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include "../audio/audio_engine.h"
#include "pcap_capture.h"
#include "reac_packet_parser.h"

namespace windows_reac_asio {

class ReacReceiver {
public:
    ReacReceiver(AudioEngine& audioEngine, std::string interfaceSelector);
    ~ReacReceiver();

    bool start();
    void stop();
    bool running() const noexcept { return running_.load(std::memory_order_acquire); }
    const std::string& lastError() const noexcept { return lastError_; }

private:
    void run();
    void handlePacket(const CapturedPacket& packet);

    AudioEngine& audioEngine_;
    std::string interfaceSelector_;
    std::string lastError_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    ReacPacketParser parser_;
    std::vector<float> decodedPacket_;
    bool haveCounter_ = false;
    uint16_t lastCounter_ = 0;
    uint64_t validPacketsSeen_ = 0;
    uint64_t decodedPackets_ = 0;
    uint64_t missingPackets_ = 0;
    uint64_t lastMissingPacketsLog_ = 0;
};

} // namespace windows_reac_asio
