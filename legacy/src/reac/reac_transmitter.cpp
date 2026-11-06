#include "reac_transmitter.h"

#include "pcap_capture.h"
#include "pcap_sender.h"
#include "reac_packet_encoder.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <limits>
#include <utility>

namespace windows_reac_asio {
namespace {

constexpr uint32_t kS1608ReturnChannels = kS1608ReturnWireChannels;
constexpr uint32_t kS1608ReturnFramesPerPacket = 6;
constexpr uint32_t kStandbyChunkMs = 2;
constexpr uint32_t kStreamChunkMs = 80;
constexpr uint32_t kMaxSendPps = 17000;
constexpr double kMaxClockFollowDeviationPpm = 1500.0;
constexpr double kInitialStreamQueueOverheadSeconds = 0.0010;
constexpr double kMaxStreamQueueOverheadSeconds = 0.0030;
constexpr uint32_t kMinTransmitBufferFrames = 3072;
constexpr uint32_t kMaxTransmitBufferFrames = 4096;
constexpr uint32_t kTransmitStartupPrefillFrames = 0;
constexpr auto kTransmitStartupPrefillTimeout = std::chrono::milliseconds(0);
constexpr uint8_t kBroadcastMac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
constexpr uint8_t kObservedS1608Mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
constexpr uint8_t kObservedMixerMac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};
constexpr uint8_t kControlTypeOne[5] = {0x01, 0x00, 0x00, 0x1a, 0x00};
constexpr uint8_t kControlTypeThree[5] = {0x01, 0x03, 0x00, 0x19, 0x01};
constexpr uint8_t kSlaveAnnounceTypes[][5] = {
    {0x01, 0x03, 0x00, 0x10, 0x82},
    {0x04, 0x03, 0x00, 0x14, 0x00},
    {0x04, 0x03, 0x00, 0x13, 0x00},
    {0x01, 0x03, 0x00, 0x01, 0x81},
};
constexpr uint8_t kSlaveHandshakeData[][19] = {
    {0x00, 0x00, 0x02, 0x02, 0x02, 0x02, 0x02, 0x01, 0x01, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x00, 0x00, 0x00, 0x00},
    {0x02, 0x00, 0xfe, 0x0f, 0xf0, 0x41, 0x0a, 0x00, 0x00, 0x12, 0x12, 0x01, 0x00, 0x06, 0x00, 0x01, 0x00, 0x78, 0xf7},
    {0x02, 0x00, 0xfe, 0x0f, 0xf0, 0x41, 0x0a, 0x00, 0x00, 0x12, 0x12, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x7d, 0xf7},
    {0x02, 0x00, 0xfe, 0x0e, 0xf0, 0x41, 0x0a, 0x00, 0x00, 0x12, 0x12, 0x03, 0x02, 0x00, 0x01, 0x00, 0x7a, 0xf7, 0x00},
};
constexpr size_t kSlaveAnnouncePacketCount = 1 + std::size(kSlaveAnnounceTypes);

bool macEquals(const uint8_t* left, const uint8_t* right) noexcept {
    return std::memcmp(left, right, 6) == 0;
}

std::string makeMasterFilter() {
    char filter[128] = {};
    std::snprintf(
        filter,
        sizeof(filter),
        "ether proto 0x8819 and ether src %02x:%02x:%02x:%02x:%02x:%02x",
        kObservedMixerMac[0],
        kObservedMixerMac[1],
        kObservedMixerMac[2],
        kObservedMixerMac[3],
        kObservedMixerMac[4],
        kObservedMixerMac[5]);
    return filter;
}

uint16_t packetCounter(const CapturedPacket& packet) noexcept {
    if (!packet.data || packet.size < kReacEthernetHeaderBytes + 2) {
        return 0;
    }
    return static_cast<uint16_t>(packet.data[14] | (static_cast<uint16_t>(packet.data[15]) << 8));
}

bool isAudioPacket(const CapturedPacket& packet) noexcept {
    if (!packet.data || packet.size < kReacPayloadOffset + 2) {
        return false;
    }
    if (packet.data[16] != 0x00 || packet.data[17] != 0x00) {
        return false;
    }
    return packet.data[packet.size - 2] == 0xc2 && packet.data[packet.size - 1] == 0xea;
}

double s1608ReturnPacketsPerSecond(uint32_t sampleRate) noexcept {
    return static_cast<double>(sampleRate) / static_cast<double>(kS1608ReturnFramesPerPacket);
}

bool isPlausibleStableClock(double packetsPerSecond, double nominal) noexcept {
    const double maxDeviation = nominal * kMaxClockFollowDeviationPpm / 1000000.0;
    return packetsPerSecond >= nominal - maxDeviation &&
           packetsPerSecond <= nominal + maxDeviation;
}

uint32_t compensatedQueuePps(uint64_t packetCount, double targetPps, double overheadSeconds) noexcept {
    if (packetCount <= 1 || targetPps <= 0.0) {
        return 1;
    }
    overheadSeconds = std::clamp(overheadSeconds, 0.0, kMaxStreamQueueOverheadSeconds);
    const double targetChunkSeconds = static_cast<double>(packetCount) / targetPps;
    const double queueSpanSeconds = std::max(0.001, targetChunkSeconds - overheadSeconds);
    const double queuePps = static_cast<double>(packetCount - 1U) / queueSpanSeconds;
    return static_cast<uint32_t>(std::llround(std::clamp(
        queuePps,
        1.0,
        static_cast<double>(kMaxSendPps))));
}

uint8_t reacControlChecksum(const uint8_t* headerData) noexcept {
    uint32_t sum = 0;
    for (size_t index = 0; index < 31; ++index) {
        sum += headerData[index];
    }
    return static_cast<uint8_t>(0U - (sum & 0xffU));
}

bool reacControlChecksumValid(const uint8_t* headerData) noexcept {
    uint32_t sum = 0;
    for (size_t index = 0; index < 32; ++index) {
        sum += headerData[index];
    }
    return (sum & 0xffU) == 0;
}

bool isEacdControlType(const CapturedPacket& packet, const uint8_t controlType[5]) noexcept {
    if (!packet.data || packet.size < kReacEthernetHeaderBytes + kReacHeaderBytes) {
        return false;
    }
    if (packet.data[16] != 0xcd || packet.data[17] != 0xea) {
        return false;
    }
    if (!reacControlChecksumValid(packet.data + 18)) {
        return false;
    }
    return std::memcmp(packet.data + 18, controlType, 5) == 0;
}

void fillHeaderPair(ReacPacketTemplate& packetTemplate, uint8_t first, uint8_t second) {
    for (size_t index = 0; index + 1 < sizeof(packetTemplate.headerData); index += 2) {
        packetTemplate.headerData[index] = first;
        packetTemplate.headerData[index + 1] = second;
    }
}

void buildControlPacket(
    const ReacPacketTemplate& packetTemplate,
    const uint8_t* destinationMac,
    uint16_t counter,
    const uint8_t* headerData,
    size_t headerDataBytes,
    uint8_t* packet,
    size_t packetBytes) {
    std::fill(packet, packet + packetBytes, 0);
    std::memcpy(packet, destinationMac, 6);
    std::memcpy(packet + 6, packetTemplate.sourceMac, 6);
    packet[12] = 0x88;
    packet[13] = 0x19;
    packet[14] = static_cast<uint8_t>(counter & 0xff);
    packet[15] = static_cast<uint8_t>((counter >> 8) & 0xff);
    packet[16] = 0xcd;
    packet[17] = 0xea;

    uint8_t* controlHeader = packet + 18;
    if (headerData && headerDataBytes > 0) {
        std::memcpy(controlHeader, headerData, std::min<size_t>(headerDataBytes, 31));
    }
    controlHeader[31] = reacControlChecksum(controlHeader);
    packet[packetBytes - 2] = 0xc2;
    packet[packetBytes - 1] = 0xea;
}

void buildSlaveAnnouncePacket(
    const ReacPacketTemplate& packetTemplate,
    const uint8_t* masterMac,
    uint16_t counter,
    size_t sequenceIndex,
    uint8_t* packet,
    size_t packetBytes,
    std::array<uint8_t, 2>* controlTail) {
    if (sequenceIndex == 0) {
        buildControlPacket(packetTemplate, masterMac, counter, nullptr, 0, packet, packetBytes);
    } else {
        uint8_t header[32] = {};
        const size_t announceIndex = sequenceIndex - 1;
        if (announceIndex < std::size(kSlaveAnnounceTypes)) {
            std::memcpy(header, kSlaveAnnounceTypes[announceIndex], sizeof(kSlaveAnnounceTypes[announceIndex]));
            std::memcpy(header + 5, kSlaveHandshakeData[announceIndex], sizeof(kSlaveHandshakeData[announceIndex]));
        }
        buildControlPacket(packetTemplate, masterMac, counter, header, sizeof(header), packet, packetBytes);
    }

    if (controlTail) {
        (*controlTail)[0] = packet[18 + 30];
        (*controlTail)[1] = packet[18 + 31];
    }
}

bool buildStandbyQueue(
    const ReacPacketTemplate& baseTemplate,
    uint32_t channelCount,
    uint32_t framesPerPacket,
    uint64_t packetCount,
    std::vector<uint8_t>& packets) {
    const size_t packetBytes = ReacPacketEncoder::packetBytesForLayout(channelCount, framesPerPacket);
    packets.assign(static_cast<size_t>(packetCount) * packetBytes, 0);

    ReacPacketTemplate standbyTemplate = baseTemplate;
    std::copy(std::begin(kBroadcastMac), std::end(kBroadcastMac), standbyTemplate.destinationMac);
    std::fill(std::begin(standbyTemplate.headerData), std::end(standbyTemplate.headerData), 0);
    standbyTemplate.packetType = 0;

    ReacPacketEncoder encoder;
    encoder.setTemplate(standbyTemplate);
    std::vector<float> silence(static_cast<size_t>(framesPerPacket) * channelCount, 0.0f);
    for (uint64_t index = 0; index < packetCount; ++index) {
        size_t encodedBytes = 0;
        const ReacEncodeStatus status = encoder.encodeInterleavedFloat(
            silence.data(),
            channelCount,
            framesPerPacket,
            static_cast<uint16_t>(index & 0xffffU),
            packets.data() + static_cast<size_t>(index) * packetBytes,
            packetBytes,
            &encodedBytes);
        if (status != ReacEncodeStatus::ok || encodedBytes != packetBytes) {
            return false;
        }
    }
    return true;
}

void patchPacketCounters(std::vector<uint8_t>& packets, size_t packetBytes, uint16_t startCounter) {
    const size_t count = packetBytes == 0 ? 0 : packets.size() / packetBytes;
    for (size_t index = 0; index < count; ++index) {
        uint8_t* packet = packets.data() + index * packetBytes;
        const uint16_t counter = static_cast<uint16_t>((static_cast<uint32_t>(startCounter) + index) & 0xffffU);
        packet[14] = static_cast<uint8_t>(counter & 0xff);
        packet[15] = static_cast<uint8_t>((counter >> 8) & 0xff);
    }
}

struct MasterClockFollower {
    std::atomic<bool> stop{false};
    std::atomic<bool> failed{false};
    std::atomic<bool> haveEstimate{false};
    std::atomic<uint32_t> estimatedPpsMilli{0};
    std::atomic<uint64_t> packetsObserved{0};
    std::string error;
    std::thread worker;
};

void startMasterClockFollower(MasterClockFollower& follower, const std::string& deviceName, double nominalPacketsPerSecond) {
    follower.stop.store(false, std::memory_order_release);
    follower.failed.store(false, std::memory_order_release);
    follower.haveEstimate.store(false, std::memory_order_release);
    follower.estimatedPpsMilli.store(0, std::memory_order_release);
    follower.packetsObserved.store(0, std::memory_order_release);
    follower.error.clear();

    follower.worker = std::thread([&follower, deviceName, nominalPacketsPerSecond]() {
        PcapCapture clockCapture;
        if (!clockCapture.available()) {
            follower.error = clockCapture.lastError();
            follower.failed.store(true, std::memory_order_release);
            return;
        }
        if (!clockCapture.open(deviceName, makeMasterFilter())) {
            follower.error = std::string("Could not open REAC master clock capture: ") + clockCapture.lastError();
            follower.failed.store(true, std::memory_order_release);
            return;
        }

        bool haveWindow = false;
        uint16_t windowCounter = 0;
        uint64_t windowTimeNs = 0;
        uint64_t windowObservedIntervals = 0;
        bool havePrevious = false;
        uint16_t previousCounter = 0;
        uint64_t previousTimeNs = 0;
        double estimatePps = 0.0;
        constexpr uint64_t kMaxClockGapNanoseconds = 5000000ULL;
        constexpr uint16_t kMaxConsecutiveCounterDelta = 4;

        while (!follower.stop.load(std::memory_order_acquire)) {
            const bool ok = clockCapture.captureOnce(50, [&](const CapturedPacket& captured) {
                if (!captured.data || captured.size < kReacEthernetHeaderBytes + 2 || captured.timestampNanoseconds == 0) {
                    return true;
                }
                if (!isAudioPacket(captured)) {
                    return true;
                }

                const uint16_t counter = packetCounter(captured);
                follower.packetsObserved.fetch_add(1, std::memory_order_relaxed);
                if (havePrevious) {
                    const uint16_t consecutiveDelta = static_cast<uint16_t>(counter - previousCounter);
                    const uint64_t interPacketNs = captured.timestampNanoseconds - previousTimeNs;
                    if (consecutiveDelta == 0 ||
                        consecutiveDelta > kMaxConsecutiveCounterDelta ||
                        interPacketNs > kMaxClockGapNanoseconds) {
                        haveWindow = false;
                        windowObservedIntervals = 0;
                    }
                }
                previousCounter = counter;
                previousTimeNs = captured.timestampNanoseconds;
                havePrevious = true;

                if (!haveWindow) {
                    haveWindow = true;
                    windowCounter = counter;
                    windowTimeNs = captured.timestampNanoseconds;
                    windowObservedIntervals = 0;
                    return true;
                }

                ++windowObservedIntervals;
                const uint16_t deltaCounter = static_cast<uint16_t>(counter - windowCounter);
                const uint64_t deltaTimeNs = captured.timestampNanoseconds - windowTimeNs;
                if (deltaCounter >= 1000 && deltaTimeNs > 0) {
                    const double seconds = static_cast<double>(deltaTimeNs) / 1000000000.0;
                    const double instantPps = static_cast<double>(deltaCounter) / seconds;
                    const uint64_t intervalError = deltaCounter > windowObservedIntervals
                        ? static_cast<uint64_t>(deltaCounter) - windowObservedIntervals
                        : windowObservedIntervals - static_cast<uint64_t>(deltaCounter);
                    if (intervalError <= 2 && isPlausibleStableClock(instantPps, nominalPacketsPerSecond)) {
                        estimatePps = estimatePps <= 0.0
                            ? instantPps
                            : (estimatePps * 0.85 + instantPps * 0.15);
                        follower.estimatedPpsMilli.store(
                            static_cast<uint32_t>(std::llround(estimatePps * 1000.0)),
                            std::memory_order_release);
                        follower.haveEstimate.store(true, std::memory_order_release);
                    }
                    windowCounter = counter;
                    windowTimeNs = captured.timestampNanoseconds;
                    windowObservedIntervals = 0;
                }
                return true;
            });
            if (!ok) {
                follower.error = std::string("REAC master clock capture failed: ") + clockCapture.lastError();
                follower.failed.store(true, std::memory_order_release);
                return;
            }
        }
    });
}

void stopMasterClockFollower(MasterClockFollower& follower) {
    follower.stop.store(true, std::memory_order_release);
    if (follower.worker.joinable()) {
        follower.worker.join();
    }
}

struct SlaveHandshakeTracker {
    enum class Phase {
        waitingForPrelude,
        waitingForMacInfo,
        gotMasterMacInfo,
    };

    Phase phase = Phase::waitingForPrelude;
    std::array<uint8_t, 6> masterMac{};
};

bool isSlaveMacPrelude(const CapturedPacket& captured) noexcept {
    return isEacdControlType(captured, kControlTypeOne) &&
           captured.size >= kReacPayloadOffset &&
           captured.data[18 + 29] == 0xc0 &&
           captured.data[18 + 30] == 0xa8;
}

bool isSlaveMacInfo(const CapturedPacket& captured) noexcept {
    return isEacdControlType(captured, kControlTypeOne) &&
           captured.size >= kReacPayloadOffset &&
           captured.data[18 + 5] == 0x01 &&
           captured.data[18 + 6] == 0x01 &&
           std::memcmp(captured.data + 18 + 7, captured.data + 18 + 17, 6) == 0;
}

void updateSlaveHandshakeTracker(SlaveHandshakeTracker& tracker, const CapturedPacket& captured) {
    if (!captured.data || captured.size < kReacPayloadOffset) {
        return;
    }

    if (tracker.phase == SlaveHandshakeTracker::Phase::waitingForPrelude) {
        if (isSlaveMacPrelude(captured)) {
            tracker.phase = SlaveHandshakeTracker::Phase::waitingForMacInfo;
        } else if (isEacdControlType(captured, kControlTypeThree)) {
            std::copy(captured.data + 6, captured.data + 12, tracker.masterMac.begin());
            tracker.phase = SlaveHandshakeTracker::Phase::gotMasterMacInfo;
        }
        return;
    }

    if (tracker.phase == SlaveHandshakeTracker::Phase::waitingForMacInfo) {
        if (!isSlaveMacInfo(captured)) {
            if (isSlaveMacPrelude(captured)) {
                return;
            }
            if (isEacdControlType(captured, kControlTypeThree)) {
                std::copy(captured.data + 6, captured.data + 12, tracker.masterMac.begin());
                tracker.phase = SlaveHandshakeTracker::Phase::gotMasterMacInfo;
                return;
            }
            tracker.phase = SlaveHandshakeTracker::Phase::waitingForPrelude;
            return;
        }
        std::copy(captured.data + 18 + 7, captured.data + 18 + 13, tracker.masterMac.begin());
        tracker.phase = SlaveHandshakeTracker::Phase::gotMasterMacInfo;
    }
}

struct InitialTriggerResponder {
    std::atomic<bool> stop{false};
    std::atomic<bool> sent{false};
    std::atomic<bool> failed{false};
    std::string error;
    std::thread worker;
    std::array<uint8_t, 6> masterMac{};
    std::array<uint8_t, 2> headerPair{};
    uint16_t announceStartCounter = 0;
};

void startInitialTriggerResponder(
    InitialTriggerResponder& responder,
    const std::string& deviceName,
    const ReacPacketTemplate& packetTemplate,
    size_t packetBytes,
    const std::atomic<uint32_t>* localNextCounter,
    const std::atomic<bool>* stopRequested) {
    responder.stop.store(false, std::memory_order_release);
    responder.sent.store(false, std::memory_order_release);
    responder.failed.store(false, std::memory_order_release);
    responder.error.clear();
    responder.masterMac = {};
    responder.headerPair = {};
    responder.announceStartCounter = 0;

    responder.worker = std::thread([&responder, deviceName, packetTemplate, packetBytes, localNextCounter, stopRequested]() {
        PcapCapture triggerCapture;
        if (!triggerCapture.available()) {
            responder.error = triggerCapture.lastError();
            responder.failed.store(true, std::memory_order_release);
            return;
        }
        if (!triggerCapture.open(deviceName, makeMasterFilter())) {
            responder.error = std::string("Could not open REAC initial trigger capture: ") + triggerCapture.lastError();
            responder.failed.store(true, std::memory_order_release);
            return;
        }

        PcapSender triggerSender;
        if (!triggerSender.available()) {
            responder.error = triggerSender.lastError();
            responder.failed.store(true, std::memory_order_release);
            return;
        }
        if (!triggerSender.open(deviceName)) {
            responder.error = std::string("Could not open REAC initial trigger sender: ") + triggerSender.lastError();
            responder.failed.store(true, std::memory_order_release);
            return;
        }

        std::vector<uint8_t> packet(packetBytes, 0);
        SlaveHandshakeTracker tracker{};
        while (!responder.stop.load(std::memory_order_acquire) &&
               !(stopRequested && stopRequested->load(std::memory_order_acquire))) {
            const bool ok = triggerCapture.captureOnce(50, [&](const CapturedPacket& captured) {
                updateSlaveHandshakeTracker(tracker, captured);
                if (tracker.phase != SlaveHandshakeTracker::Phase::gotMasterMacInfo ||
                    !isEacdControlType(captured, kControlTypeThree)) {
                    return true;
                }
                if (responder.sent.load(std::memory_order_acquire)) {
                    return true;
                }

                const uint16_t announceStartCounter = localNextCounter
                    ? static_cast<uint16_t>(localNextCounter->load(std::memory_order_acquire) & 0xffffU)
                    : static_cast<uint16_t>(packetCounter(captured) + 1U);
                std::array<uint8_t, 2> lastControlTail{};
                for (size_t sequenceIndex = 0; sequenceIndex < kSlaveAnnouncePacketCount; ++sequenceIndex) {
                    buildSlaveAnnouncePacket(
                        packetTemplate,
                        tracker.masterMac.data(),
                        static_cast<uint16_t>(announceStartCounter + sequenceIndex),
                        sequenceIndex,
                        packet.data(),
                        packet.size(),
                        &lastControlTail);
                    if (!triggerSender.send(packet.data(), packet.size())) {
                        responder.error = std::string("REAC initial trigger response failed: ") + triggerSender.lastError();
                        responder.failed.store(true, std::memory_order_release);
                        responder.stop.store(true, std::memory_order_release);
                        return true;
                    }
                }

                responder.masterMac = tracker.masterMac;
                responder.headerPair = lastControlTail;
                responder.announceStartCounter = announceStartCounter;
                responder.sent.store(true, std::memory_order_release);
                responder.stop.store(true, std::memory_order_release);
                return true;
            });
            if (!ok) {
                responder.error = std::string("REAC initial trigger capture failed: ") + triggerCapture.lastError();
                responder.failed.store(true, std::memory_order_release);
                return;
            }
        }
    });
}

void stopInitialTriggerResponder(InitialTriggerResponder& responder) {
    responder.stop.store(true, std::memory_order_release);
    if (responder.worker.joinable()) {
        responder.worker.join();
    }
}

} // namespace

ReacTransmitter::ReacTransmitter() {
    configure(ReacTransmitterConfig{});
}

ReacTransmitter::~ReacTransmitter() {
    stop();
}

bool ReacTransmitter::configure(const ReacTransmitterConfig& config) {
    if (running_.load(std::memory_order_acquire)) {
        lastError_ = "REAC transmitter cannot be configured while running";
        return false;
    }
    if (config.sampleRate != 44100 && config.sampleRate != 48000 && config.sampleRate != 96000) {
        lastError_ = "REAC output currently supports 44.1, 48, or 96 kHz host rates";
        return false;
    }

    if (config.channelCount == 0 ||
        config.channelCount > kS1608ReturnChannels ||
        (config.channelCount & 1U) != 0) {
        lastError_ = "S-1608 REAC output channel count must be an even value from 2 to 16";
        return false;
    }

    config_ = config;
    config_.sampleRate = config.sampleRate;
    config_.channelCount = config.channelCount;
    config_.bufferFrames = std::clamp<uint32_t>(
        config.bufferFrames,
        kMinTransmitBufferFrames,
        kMaxTransmitBufferFrames);
    config_.returnMap = reacReturnMapModeValue(normalizeReacReturnMapMode(config.returnMap));
    counters_.reset();
    writeScratch_.assign(static_cast<size_t>(config_.bufferFrames) * config_.channelCount, 0.0f);
    lastError_.clear();
    return ringBuffer_.configure(config_.channelCount, config_.bufferFrames);
}

bool ReacTransmitter::start() {
    if (running_.exchange(true, std::memory_order_acq_rel)) {
        return true;
    }
    if (config_.interfaceSelector.empty()) {
        running_.store(false, std::memory_order_release);
        lastError_ = "No REAC adapter selected for transmitter";
        return false;
    }

    lastError_.clear();
    stopRequested_.store(false, std::memory_order_release);
    ringBuffer_.clear();
    counters_.reset();
    worker_ = std::thread(&ReacTransmitter::workerLoop, this);
    return true;
}

void ReacTransmitter::stop() {
    stopRequested_.store(true, std::memory_order_release);
    running_.store(false, std::memory_order_release);
    if (worker_.joinable()) {
        worker_.join();
    }
}

bool ReacTransmitter::writeInterleaved(const float* interleaved, uint32_t sourceChannels, uint32_t frames) noexcept {
    if (!interleaved || sourceChannels == 0 || frames == 0 || !running_.load(std::memory_order_acquire)) {
        return false;
    }
    if (sourceChannels == config_.channelCount) {
        return ringBuffer_.writeInterleaved(interleaved, frames, counters_);
    }
    if (writeScratch_.size() < static_cast<size_t>(frames) * config_.channelCount) {
        return false;
    }

    for (uint32_t frame = 0; frame < frames; ++frame) {
        for (uint32_t channel = 0; channel < config_.channelCount; ++channel) {
            writeScratch_[static_cast<size_t>(frame) * config_.channelCount + channel] =
                channel < sourceChannels ? interleaved[static_cast<size_t>(frame) * sourceChannels + channel] : 0.0f;
        }
    }
    return ringBuffer_.writeInterleaved(writeScratch_.data(), frames, counters_);
}

void ReacTransmitter::workerLoop() {
    std::string deviceName;
    if (!openDevice(deviceName)) {
        running_.store(false, std::memory_order_release);
        return;
    }

    if (!runStableSlaveSession(deviceName)) {
        running_.store(false, std::memory_order_release);
        return;
    }
    running_.store(false, std::memory_order_release);
}

bool ReacTransmitter::openDevice(std::string& deviceName) {
    PcapCapture capture;
    if (!capture.available()) {
        lastError_ = capture.lastError();
        return false;
    }
    const std::vector<CaptureDeviceInfo> devices = capture.listDevices();
    deviceName = capture.resolveDeviceName(config_.interfaceSelector, devices);
    if (deviceName.empty()) {
        lastError_ = "Could not resolve REAC transmitter adapter";
        return false;
    }
    return true;
}

bool ReacTransmitter::runStableSlaveSession(const std::string& deviceName) {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    const uint32_t sourceChannelCount = config_.channelCount;
    const uint32_t wireChannels = kS1608ReturnChannels;
    const uint32_t wireFramesPerPacket = kS1608ReturnFramesPerPacket;
    const ReacReturnMapMode returnMap = normalizeReacReturnMapMode(config_.returnMap);
    const double targetPacketsPerSecond = s1608ReturnPacketsPerSecond(config_.sampleRate);
    const size_t packetBytes = ReacPacketEncoder::packetBytesForLayout(wireChannels, wireFramesPerPacket);

    PcapSender sender;
    if (!sender.available()) {
        lastError_ = sender.lastError();
        return false;
    }
    if (!sender.open(deviceName)) {
        lastError_ = std::string("Could not open REAC transmitter sender: ") + sender.lastError();
        return false;
    }

    PcapCapture capture;
    if (!capture.available()) {
        lastError_ = capture.lastError();
        return false;
    }
    if (!capture.open(deviceName, makeMasterFilter())) {
        lastError_ = std::string("Could not open REAC transmitter capture: ") + capture.lastError();
        return false;
    }

    ReacPacketTemplate packetTemplate{};
    std::copy(std::begin(kBroadcastMac), std::end(kBroadcastMac), packetTemplate.destinationMac);
    std::copy(std::begin(kObservedS1608Mac), std::end(kObservedS1608Mac), packetTemplate.sourceMac);
    packetTemplate.packetType = 0;

    const uint64_t standbyPacketCount = std::max<uint64_t>(
        1,
        static_cast<uint64_t>(std::llround(static_cast<double>(kStandbyChunkMs) * targetPacketsPerSecond / 1000.0)));
    std::vector<uint8_t> standbyPackets;
    if (!buildStandbyQueue(packetTemplate, wireChannels, wireFramesPerPacket, standbyPacketCount, standbyPackets)) {
        lastError_ = "Could not build REAC standby queue";
        return false;
    }

    bool sentHandshake = false;
    bool initialAnnouncePending = false;
    SlaveHandshakeTracker handshakeTracker{};
    std::array<uint8_t, 6> masterMac{};
    std::copy(std::begin(kObservedMixerMac), std::end(kObservedMixerMac), masterMac.begin());
    std::array<uint8_t, 2> lastControlTail{};
    uint16_t localCounter = 0;
    double standbyOverheadEstimateSeconds = 0.0;

    const auto handleControl = [&](const CapturedPacket& captured) {
        if (!captured.data || captured.size < kReacPayloadOffset) {
            return true;
        }
        updateSlaveHandshakeTracker(handshakeTracker, captured);
        if (handshakeTracker.phase == SlaveHandshakeTracker::Phase::gotMasterMacInfo) {
            std::copy(handshakeTracker.masterMac.begin(), handshakeTracker.masterMac.end(), masterMac.begin());
        }
        if (handshakeTracker.phase == SlaveHandshakeTracker::Phase::gotMasterMacInfo &&
            isEacdControlType(captured, kControlTypeThree)) {
            initialAnnouncePending = true;
            sentHandshake = true;
        }
        return true;
    };

    while (!stopRequested_.load(std::memory_order_acquire) && !sentHandshake) {
        for (uint32_t drain = 0; drain < 128 && !sentHandshake; ++drain) {
            if (!capture.captureOnce(0, handleControl)) {
                lastError_ = std::string("REAC handshake capture failed: ") + capture.lastError();
                break;
            }
        }
        if (!lastError_.empty()) {
            break;
        }
        if (sentHandshake) {
            break;
        }

        patchPacketCounters(standbyPackets, packetBytes, localCounter);
        const double targetStandbySeconds =
            static_cast<double>(standbyPacketCount) / targetPacketsPerSecond;
        const double compensatedStandbySeconds = (std::max)(0.001, targetStandbySeconds - standbyOverheadEstimateSeconds);
        const uint32_t standbySendPps = static_cast<uint32_t>(std::llround(std::clamp(
            static_cast<double>(standbyPacketCount) / compensatedStandbySeconds,
            1.0,
            static_cast<double>(kMaxSendPps))));
        const auto standbyStarted = std::chrono::steady_clock::now();
        if (!sender.sendTimedQueue(standbyPackets, static_cast<uint32_t>(packetBytes), standbySendPps)) {
            lastError_ = std::string("REAC standby transmit failed: ") + sender.lastError();
            break;
        }
        const auto standbyEnded = std::chrono::steady_clock::now();
        const double standbyElapsed = std::chrono::duration<double>(standbyEnded - standbyStarted).count();
        const double nominalStandbySendSeconds =
            static_cast<double>(standbyPacketCount) / static_cast<double>(standbySendPps);
        const double observedStandbyOverhead = (std::max)(0.0, standbyElapsed - nominalStandbySendSeconds);
        standbyOverheadEstimateSeconds = standbyOverheadEstimateSeconds <= 0.0
            ? observedStandbyOverhead
            : (standbyOverheadEstimateSeconds * 0.80 + observedStandbyOverhead * 0.20);
        localCounter = static_cast<uint16_t>((static_cast<uint32_t>(localCounter) + standbyPacketCount) & 0xffffU);
    }

    if (!lastError_.empty()) {
        return false;
    }
    if (!sentHandshake || stopRequested_.load(std::memory_order_acquire)) {
        return true;
    }

    std::copy(masterMac.begin(), masterMac.end(), packetTemplate.destinationMac);
    fillHeaderPair(packetTemplate, lastControlTail[0], lastControlTail[1]);
    ReacPacketEncoder encoder;
    encoder.setTemplate(packetTemplate);

    std::atomic<uint64_t> pendingAnnounces{0};
    std::atomic<bool> triggerStop{false};
    std::thread triggerThread([&]() {
        PcapCapture triggerCapture;
        if (!triggerCapture.available() || !triggerCapture.open(deviceName, makeMasterFilter())) {
            return;
        }
        auto lastResponseAt = std::chrono::steady_clock::time_point{};
        while (!triggerStop.load(std::memory_order_acquire) && !stopRequested_.load(std::memory_order_acquire)) {
            triggerCapture.captureOnce(50, [&](const CapturedPacket& captured) {
                if (!isEacdControlType(captured, kControlTypeThree)) {
                    return true;
                }
                const auto now = std::chrono::steady_clock::now();
                if (lastResponseAt.time_since_epoch().count() != 0 &&
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - lastResponseAt).count() < 200) {
                    return true;
                }
                lastResponseAt = now;
                pendingAnnounces.fetch_add(1, std::memory_order_relaxed);
                return true;
            });
        }
    });

    MasterClockFollower clockFollower;
    startMasterClockFollower(clockFollower, deviceName, targetPacketsPerSecond);

    std::vector<float> packetAudio(static_cast<size_t>(wireChannels) * wireFramesPerPacket, 0.0f);
    std::vector<uint32_t> sourceReadChannels(sourceChannelCount, 0);
    std::vector<float*> sourceReadDestinations(sourceChannelCount, nullptr);
    for (uint32_t channel = 0; channel < sourceChannelCount; ++channel) {
        sourceReadChannels[channel] = channel;
    }
    std::vector<float> sourcePlanar;

    const auto chooseTargetPps = [&]() {
        double targetPps = targetPacketsPerSecond;
        if (clockFollower.haveEstimate.load(std::memory_order_acquire)) {
            const double estimate = static_cast<double>(clockFollower.estimatedPpsMilli.load(std::memory_order_acquire)) / 1000.0;
            if (isPlausibleStableClock(estimate, targetPacketsPerSecond)) {
                targetPps = estimate;
            }
        }
        return targetPps;
    };

    ringBuffer_.clear();
    counters_.reset();

    const uint32_t startupPrefillFrames = std::min<uint32_t>(
        kTransmitStartupPrefillFrames,
        ringBuffer_.capacityFrames());
    const auto prefillDeadline = std::chrono::steady_clock::now() + kTransmitStartupPrefillTimeout;
    while (!stopRequested_.load(std::memory_order_acquire) &&
           ringBuffer_.availableFrames() < startupPrefillFrames &&
           std::chrono::steady_clock::now() < prefillDeadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    uint64_t announcePacketsRemaining = initialAnnouncePending ? kSlaveAnnouncePacketCount : 0;
    uint64_t announcePacketIndex = 0;
    double streamOverheadEstimateSeconds = kInitialStreamQueueOverheadSeconds;

    struct StreamChunk {
        std::vector<uint8_t> packets;
        std::unique_ptr<PcapSender::TimedQueue> queue;
        uint64_t packetCount = 0;
        uint32_t sendPps = 0;
        std::string error;
    };

    PcapSender queuePreparer;
    if (!queuePreparer.available()) {
        lastError_ = queuePreparer.lastError();
    }

    const auto buildStreamChunk = [&](uint16_t startCounter, StreamChunk& chunk) {
        const double targetPps = chooseTargetPps();
        chunk.packetCount = std::max<uint64_t>(
            1,
            static_cast<uint64_t>(std::llround(static_cast<double>(kStreamChunkMs) * targetPps / 1000.0)));
        chunk.sendPps = compensatedQueuePps(chunk.packetCount, targetPps, streamOverheadEstimateSeconds);
        chunk.packets.assign(static_cast<size_t>(chunk.packetCount) * packetBytes, 0);

        const uint64_t totalWireFrames = chunk.packetCount * wireFramesPerPacket;
        if (totalWireFrames > static_cast<uint64_t>((std::numeric_limits<size_t>::max)() / sizeof(float) / sourceChannelCount)) {
            chunk.error = "REAC audio chunk is too large";
            return false;
        }
        sourcePlanar.assign(static_cast<size_t>(totalWireFrames) * sourceChannelCount, 0.0f);
        for (uint32_t channel = 0; channel < sourceChannelCount; ++channel) {
            sourceReadDestinations[channel] =
                sourcePlanar.data() + (static_cast<size_t>(channel) * static_cast<size_t>(totalWireFrames));
        }
        ringBuffer_.readPlanar(
            sourceReadChannels.data(),
            sourceReadDestinations.data(),
            sourceChannelCount,
            static_cast<uint32_t>(totalWireFrames),
            counters_);
        uint64_t wireFrameIndex = 0;

        for (uint64_t index = 0; index < chunk.packetCount; ++index) {
            uint8_t* packet = chunk.packets.data() + static_cast<size_t>(index) * packetBytes;
            const uint16_t packetCounter = static_cast<uint16_t>(
                (static_cast<uint32_t>(startCounter) + static_cast<uint32_t>(index)) & 0xffffU);

            if (announcePacketsRemaining == 0) {
                const uint64_t requestedAnnounceSequences = pendingAnnounces.exchange(0, std::memory_order_acq_rel);
                if (requestedAnnounceSequences > 0) {
                    announcePacketsRemaining = requestedAnnounceSequences * kSlaveAnnouncePacketCount;
                    announcePacketIndex = 0;
                }
            }

            if (announcePacketsRemaining > 0) {
                buildSlaveAnnouncePacket(
                    packetTemplate,
                    masterMac.data(),
                    packetCounter,
                    static_cast<size_t>(announcePacketIndex % kSlaveAnnouncePacketCount),
                    packet,
                    packetBytes,
                    &lastControlTail);
                fillHeaderPair(packetTemplate, lastControlTail[0], lastControlTail[1]);
                encoder.setTemplate(packetTemplate);
                --announcePacketsRemaining;
                ++announcePacketIndex;
                wireFrameIndex += wireFramesPerPacket;
                continue;
            }

            for (uint32_t frame = 0; frame < wireFramesPerPacket; ++frame) {
                float* frameAudio = packetAudio.data() + static_cast<size_t>(frame) * wireChannels;
                for (uint32_t channel = 0; channel < wireChannels; ++channel) {
                    const uint32_t sourceChannel = mapReacReturnSourceChannel(
                        returnMap,
                        channel,
                        sourceChannelCount);
                    if (sourceChannel != kInvalidReacSourceChannel && wireFrameIndex < totalWireFrames) {
                        frameAudio[channel] =
                            sourcePlanar[static_cast<size_t>(sourceChannel) * static_cast<size_t>(totalWireFrames) +
                                         static_cast<size_t>(wireFrameIndex)];
                    } else {
                        frameAudio[channel] = 0.0f;
                    }
                }
                ++wireFrameIndex;
            }

            size_t encodedBytes = 0;
            const ReacEncodeStatus status = encoder.encodeInterleavedFloat(
                packetAudio.data(),
                wireChannels,
                wireFramesPerPacket,
                packetCounter,
                packet,
                packetBytes,
                &encodedBytes);
            if (status != ReacEncodeStatus::ok || encodedBytes != packetBytes) {
                chunk.error = "REAC audio encode failed";
                return false;
            }
        }

        chunk.queue = queuePreparer.prepareTimedQueue(
            chunk.packets,
            static_cast<uint32_t>(packetBytes),
            chunk.sendPps);
        if (!chunk.queue) {
            chunk.error = std::string("REAC audio queue prepare failed: ") + queuePreparer.lastError();
            return false;
        }
        return true;
    };

    StreamChunk currentChunk;
    if (lastError_.empty() && !buildStreamChunk(localCounter, currentChunk)) {
        lastError_ = currentChunk.error.empty() ? "REAC audio queue prepare failed" : currentChunk.error;
    } else if (lastError_.empty()) {
        localCounter = static_cast<uint16_t>(
            (static_cast<uint32_t>(localCounter) + static_cast<uint32_t>(currentChunk.packetCount)) & 0xffffU);
    }

    while (lastError_.empty() && !stopRequested_.load(std::memory_order_acquire)) {
        StreamChunk nextChunk;
        bool nextChunkReady = false;
        const uint16_t nextStartCounter = localCounter;
        std::thread queueBuilder([&]() {
            nextChunkReady = buildStreamChunk(nextStartCounter, nextChunk);
        });

        const auto transmitStarted = std::chrono::steady_clock::now();
        if (!sender.transmitTimedQueue(*currentChunk.queue)) {
            lastError_ = std::string("REAC audio transmit failed: ") + sender.lastError();
        }
        const auto transmitEnded = std::chrono::steady_clock::now();

        if (queueBuilder.joinable()) {
            queueBuilder.join();
        }

        if (!lastError_.empty()) {
            break;
        }
        if (!nextChunkReady) {
            lastError_ = nextChunk.error.empty() ? "REAC audio queue prepare failed" : nextChunk.error;
            break;
        }

        const double elapsedSeconds = std::chrono::duration<double>(transmitEnded - transmitStarted).count();
        const double queueSpanSeconds = currentChunk.packetCount > 1
            ? static_cast<double>(currentChunk.packetCount - 1U) / static_cast<double>(currentChunk.sendPps)
            : 0.0;
        const double observedOverhead = std::clamp(
            elapsedSeconds - queueSpanSeconds,
            0.0,
            kMaxStreamQueueOverheadSeconds);
        streamOverheadEstimateSeconds =
            (streamOverheadEstimateSeconds * 0.90) + (observedOverhead * 0.10);

        localCounter = static_cast<uint16_t>(
            (static_cast<uint32_t>(nextStartCounter) + static_cast<uint32_t>(nextChunk.packetCount)) & 0xffffU);
        currentChunk = std::move(nextChunk);
    }

    stopMasterClockFollower(clockFollower);
    if (clockFollower.failed.load(std::memory_order_acquire) && lastError_.empty()) {
        lastError_ = clockFollower.error;
    }
    triggerStop.store(true, std::memory_order_release);
    if (triggerThread.joinable()) {
        triggerThread.join();
    }
    return lastError_.empty();
}

} // namespace windows_reac_asio
