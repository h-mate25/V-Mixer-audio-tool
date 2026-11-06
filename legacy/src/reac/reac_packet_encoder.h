#pragma once

#include <cstddef>
#include <cstdint>

#include "reac_packet_parser.h"

namespace windows_reac_asio {

enum class ReacEncodeStatus {
    ok,
    nullInput,
    invalidChannelCount,
    invalidFrameCount,
    outputTooSmall,
};

struct ReacPacketTemplate {
    uint8_t destinationMac[6] = {};
    uint8_t sourceMac[6] = {};
    uint8_t headerData[32] = {};
    uint16_t packetType = 0;
};

class ReacPacketEncoder {
public:
    ReacPacketEncoder();

    void setTemplate(const ReacPacketTemplate& packetTemplate) noexcept;

    ReacEncodeStatus encodeInterleavedFloat(
        const float* interleaved,
        uint32_t channelCount,
        uint32_t frames,
        uint16_t counter,
        uint8_t* packet,
        size_t packetCapacity,
        size_t* packetBytes = nullptr) const noexcept;

    static size_t packetBytesForChannels(uint32_t channelCount) noexcept;
    static size_t packetBytesForLayout(uint32_t channelCount, uint32_t frames) noexcept;
    static const char* toString(ReacEncodeStatus status) noexcept;

private:
    static int32_t floatToSigned24(float sample) noexcept;
    static void encodeSample(uint8_t* payload, uint32_t packetChannelCount, uint32_t channel, uint32_t sampleIndex, int32_t signed24) noexcept;

    ReacPacketTemplate template_;
};

} // namespace windows_reac_asio
