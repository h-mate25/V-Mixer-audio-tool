#include "reac_packet_encoder.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace windows_reac_asio {
namespace {

constexpr uint8_t kDefaultDestinationMac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
constexpr uint8_t kDefaultSourceMac[6] = {0x02, 0x57, 0x52, 0x41, 0x53, 0x49};

} // namespace

ReacPacketEncoder::ReacPacketEncoder() {
    std::memcpy(template_.destinationMac, kDefaultDestinationMac, sizeof(template_.destinationMac));
    std::memcpy(template_.sourceMac, kDefaultSourceMac, sizeof(template_.sourceMac));
    template_.packetType = 0;
}

void ReacPacketEncoder::setTemplate(const ReacPacketTemplate& packetTemplate) noexcept {
    template_ = packetTemplate;
}

ReacEncodeStatus ReacPacketEncoder::encodeInterleavedFloat(
    const float* interleaved,
    uint32_t channelCount,
    uint32_t frames,
    uint16_t counter,
    uint8_t* packet,
    size_t packetCapacity,
    size_t* packetBytes) const noexcept {
    if (packetBytes) {
        *packetBytes = 0;
    }
    if (!interleaved || !packet) {
        return ReacEncodeStatus::nullInput;
    }
    if (channelCount == 0 || channelCount > kReacChannelCount || (channelCount & 1U) != 0) {
        return ReacEncodeStatus::invalidChannelCount;
    }
    if (frames == 0 || frames > kReacSamplesPerPacket) {
        return ReacEncodeStatus::invalidFrameCount;
    }

    const size_t requiredBytes = packetBytesForLayout(channelCount, frames);
    if (packetCapacity < requiredBytes) {
        return ReacEncodeStatus::outputTooSmall;
    }

    std::fill(packet, packet + requiredBytes, 0);
    std::memcpy(packet, template_.destinationMac, sizeof(template_.destinationMac));
    std::memcpy(packet + 6, template_.sourceMac, sizeof(template_.sourceMac));
    packet[12] = 0x88;
    packet[13] = 0x19;
    packet[14] = static_cast<uint8_t>(counter & 0xff);
    packet[15] = static_cast<uint8_t>((counter >> 8) & 0xff);
    packet[16] = static_cast<uint8_t>(template_.packetType & 0xff);
    packet[17] = static_cast<uint8_t>((template_.packetType >> 8) & 0xff);
    std::memcpy(packet + 18, template_.headerData, sizeof(template_.headerData));

    uint8_t* payload = packet + kReacPayloadOffset;
    for (uint32_t frame = 0; frame < frames; ++frame) {
        for (uint32_t channel = 0; channel < channelCount; ++channel) {
            const float sample = interleaved[static_cast<size_t>(frame) * channelCount + channel];
            encodeSample(payload, channelCount, channel, frame, floatToSigned24(sample));
        }
    }

    packet[requiredBytes - 2] = 0xC2;
    packet[requiredBytes - 1] = 0xEA;
    if (packetBytes) {
        *packetBytes = requiredBytes;
    }
    return ReacEncodeStatus::ok;
}

size_t ReacPacketEncoder::packetBytesForChannels(uint32_t channelCount) noexcept {
    return packetBytesForLayout(channelCount, kReacSamplesPerPacket);
}

size_t ReacPacketEncoder::packetBytesForLayout(uint32_t channelCount, uint32_t frames) noexcept {
    return kReacPayloadOffset + static_cast<size_t>(frames) * channelCount * kReacBytesPerSample + 2;
}

const char* ReacPacketEncoder::toString(ReacEncodeStatus status) noexcept {
    switch (status) {
    case ReacEncodeStatus::ok:
        return "ok";
    case ReacEncodeStatus::nullInput:
        return "null input";
    case ReacEncodeStatus::invalidChannelCount:
        return "invalid channel count";
    case ReacEncodeStatus::invalidFrameCount:
        return "invalid frame count";
    case ReacEncodeStatus::outputTooSmall:
        return "output buffer too small";
    }
    return "unknown";
}

int32_t ReacPacketEncoder::floatToSigned24(float sample) noexcept {
    sample = std::max(-1.0f, std::min(0.99999988f, sample));
    return static_cast<int32_t>(std::lrint(static_cast<double>(sample) * 8388607.0));
}

void ReacPacketEncoder::encodeSample(uint8_t* payload, uint32_t packetChannelCount, uint32_t channel, uint32_t sampleIndex, int32_t signed24) noexcept {
    const uint32_t raw = static_cast<uint32_t>(signed24) & 0x00ffffffU;
    uint8_t* base = payload + sampleIndex * packetChannelCount * kReacBytesPerSample;
    uint8_t* pair = base + (channel & ~1U) * kReacBytesPerSample;

    if ((channel & 1U) == 0) {
        pair[3] = static_cast<uint8_t>(raw & 0xff);
        pair[0] = static_cast<uint8_t>((raw >> 8) & 0xff);
        pair[1] = static_cast<uint8_t>((raw >> 16) & 0xff);
    } else {
        pair[4] = static_cast<uint8_t>(raw & 0xff);
        pair[5] = static_cast<uint8_t>((raw >> 8) & 0xff);
        pair[2] = static_cast<uint8_t>((raw >> 16) & 0xff);
    }
}

} // namespace windows_reac_asio
