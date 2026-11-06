#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace windows_reac_asio {

constexpr uint32_t kReacChannelCount = 40;
constexpr uint32_t kReacSamplesPerPacket = 12;
constexpr uint32_t kReacSampleRate = 48000;
constexpr uint32_t kReacEthernetHeaderBytes = 14;
constexpr uint32_t kReacHeaderBytes = 2 + 2 + 32;
constexpr uint32_t kReacPayloadOffset = kReacEthernetHeaderBytes + kReacHeaderBytes;
constexpr uint32_t kReacBytesPerSample = 3;
constexpr uint32_t kReacPayloadBytes = kReacSamplesPerPacket * kReacChannelCount * kReacBytesPerSample;
constexpr uint32_t kReacMinimumPacketBytes = kReacPayloadOffset + kReacPayloadBytes + 2;
constexpr uint32_t kReacMinimumDynamicPacketBytes = kReacPayloadOffset + (kReacSamplesPerPacket * 2 * kReacBytesPerSample) + 2;

enum class ReacParseStatus {
    ok,
    nullPacket,
    tooShort,
    wrongEtherType,
    badTrailer,
    nonAudioPacket,
    unsupportedPayload,
    outputTooSmall,
};

struct ReacPacketView {
    const uint8_t* pcmPayload = nullptr;
    size_t pcmPayloadBytes = 0;
    uint32_t channelCount = 0;
    uint32_t framesPerPacket = 0;
    uint16_t counter = 0;
    uint16_t packetType = 0;
};

class ReacPacketParser {
public:
    ReacParseStatus parse(const uint8_t* packet, size_t packetBytes, ReacPacketView& view) noexcept;
    ReacParseStatus decodeInterleavedFloat(
        const uint8_t* packet,
        size_t packetBytes,
        float* interleaved,
        size_t interleavedSampleCapacity,
        ReacPacketView* view = nullptr) noexcept;

    static const char* toString(ReacParseStatus status) noexcept;

private:
    static float decodeSample(const uint8_t* payload, uint32_t packetChannelCount, uint32_t channel, uint32_t sampleIndex) noexcept;
};

} // namespace windows_reac_asio
