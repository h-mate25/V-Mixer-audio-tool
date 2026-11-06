#include "reac_receiver.h"

#include <utility>

#include "../diagnostics/logger.h"

namespace windows_reac_asio {
namespace {

constexpr uint32_t kIgnoreFirstPackets = 1024;
constexpr uint32_t kMaxLossFillFrames = 3700;
constexpr char kReceiverFilter[] = "ether proto 0x8819 and not ether src 02:00:00:00:00:01";

} // namespace

ReacReceiver::ReacReceiver(AudioEngine& audioEngine, std::string interfaceSelector)
    : audioEngine_(audioEngine)
    , interfaceSelector_(std::move(interfaceSelector))
    , decodedPacket_(kReacSamplesPerPacket * kReacChannelCount, 0.0f) {
}

ReacReceiver::~ReacReceiver() {
    stop();
}

bool ReacReceiver::start() {
    if (interfaceSelector_.empty()) {
        lastError_ = "No REAC capture interface configured";
        return false;
    }
    if (running_.exchange(true, std::memory_order_acq_rel)) {
        return true;
    }
    audioEngine_.clear();
    haveCounter_ = false;
    validPacketsSeen_ = 0;
    decodedPackets_ = 0;
    missingPackets_ = 0;
    lastMissingPacketsLog_ = 0;
    thread_ = std::thread(&ReacReceiver::run, this);
    return true;
}

void ReacReceiver::stop() {
    running_.store(false, std::memory_order_release);
    if (thread_.joinable()) {
        thread_.join();
    }
}

void ReacReceiver::run() {
    PcapCapture capture;
    if (!capture.available()) {
        lastError_ = capture.lastError();
        Logger::debug(std::string("REAC receiver unavailable: ") + lastError_);
        running_.store(false, std::memory_order_release);
        return;
    }

    const std::vector<CaptureDeviceInfo> devices = capture.listDevices();
    const std::string deviceName = capture.resolveDeviceName(interfaceSelector_, devices);
    if (deviceName.empty()) {
        lastError_ = "Configured REAC capture interface was not found: " + interfaceSelector_;
        Logger::debug(lastError_);
        running_.store(false, std::memory_order_release);
        return;
    }

    if (!capture.open(deviceName, kReceiverFilter)) {
        lastError_ = capture.lastError();
        Logger::debug(std::string("REAC receiver open failed: ") + lastError_);
        running_.store(false, std::memory_order_release);
        return;
    }

    while (running_.load(std::memory_order_acquire)) {
        if (!capture.captureOnce(100, [this](const CapturedPacket& packet) {
                handlePacket(packet);
                return running_.load(std::memory_order_acquire);
            })) {
            lastError_ = capture.lastError();
            Logger::debug(std::string("REAC receiver capture stopped: ") + lastError_);
            break;
        }
    }

    capture.close();
    running_.store(false, std::memory_order_release);
}

void ReacReceiver::handlePacket(const CapturedPacket& packet) {
    ReacPacketView view{};
    const ReacParseStatus status = parser_.decodeInterleavedFloat(
        packet.data,
        packet.size,
        decodedPacket_.data(),
        decodedPacket_.size(),
        &view);

    const auto accountCounter = [this](uint16_t counter) {
        if (haveCounter_) {
            const uint16_t expected = static_cast<uint16_t>(lastCounter_ + 1);
            if (counter != expected) {
                const uint16_t skipped = static_cast<uint16_t>(counter - expected);
                const uint32_t missing = skipped ? skipped : 1;
                missingPackets_ += missing;
                audioEngine_.counters().packetDropCount.fetch_add(missing, std::memory_order_relaxed);
                if (validPacketsSeen_ >= kIgnoreFirstPackets) {
                    const uint32_t missingFrames = missing * kReacSamplesPerPacket;
                    if (missingFrames <= kMaxLossFillFrames) {
                        audioEngine_.writeInputSilence(missingFrames);
                    }
                }
            }
        }

        haveCounter_ = true;
        lastCounter_ = counter;
        ++validPacketsSeen_;
    };

    if (status != ReacParseStatus::ok) {
        if (status == ReacParseStatus::nonAudioPacket) {
            accountCounter(view.counter);
            if (validPacketsSeen_ > kIgnoreFirstPackets) {
                audioEngine_.writeInputSilence(kReacSamplesPerPacket);
            }
            return;
        }

        audioEngine_.counters().packetDropCount.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    accountCounter(view.counter);

    if (validPacketsSeen_ <= kIgnoreFirstPackets) {
        return;
    }

    ++decodedPackets_;
    audioEngine_.writeInputInterleaved(decodedPacket_.data(), kReacSamplesPerPacket);

    if ((decodedPackets_ % 65536ULL) == 0 && missingPackets_ != lastMissingPacketsLog_) {
        Logger::debug(
            "REAC status: decodedPackets=" + std::to_string(decodedPackets_) +
            " missingPackets=" + std::to_string(missingPackets_));
        lastMissingPacketsLog_ = missingPackets_;
    }
}

} // namespace windows_reac_asio
