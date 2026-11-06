#include "reac_packet_parser.h"

namespace windows_reac_asio {

ReacParseStatus ReacPacketParser::parse(const uint8_t* packet, size_t packetBytes, ReacPacketView& view) noexcept {
    view = {};
    if (!packet) {
        return ReacParseStatus::nullPacket;
    }

    if (packetBytes < kReacMinimumDynamicPacketBytes) {
        return ReacParseStatus::tooShort;
    }

    if (packet[12] != 0x88 || packet[13] != 0x19) {
        return ReacParseStatus::wrongEtherType;
    }

    if (packet[packetBytes - 2] != 0xC2 || packet[packetBytes - 1] != 0xEA) {
        return ReacParseStatus::badTrailer;
    }

    view.counter = static_cast<uint16_t>(packet[14] | (static_cast<uint16_t>(packet[15]) << 8));
    view.packetType = static_cast<uint16_t>(packet[16] | (static_cast<uint16_t>(packet[17]) << 8));

    // REAC control/filler frame types still carry PCM in the payload area. Treat
    // packetType as metadata for receive-side decoding and validate by layout.
    const size_t payloadBytes = packetBytes - kReacPayloadOffset - 2;
    const size_t bytesPerChannel = kReacSamplesPerPacket * kReacBytesPerSample;
    if ((payloadBytes % bytesPerChannel) != 0) {
        return ReacParseStatus::unsupportedPayload;
    }

    const size_t channelCount = payloadBytes / bytesPerChannel;
    if (channelCount == 0 || channelCount > kReacChannelCount || (channelCount & 1U) != 0) {
        return ReacParseStatus::unsupportedPayload;
    }

    view.pcmPayload = packet + kReacPayloadOffset;
    view.pcmPayloadBytes = payloadBytes;
    view.channelCount = static_cast<uint32_t>(channelCount);
    view.framesPerPacket = kReacSamplesPerPacket;
    return ReacParseStatus::ok;
}

ReacParseStatus ReacPacketParser::decodeInterleavedFloat(
    const uint8_t* packet,
    size_t packetBytes,
    float* interleaved,
    size_t interleavedSampleCapacity,
    ReacPacketView* view) noexcept {
    ReacPacketView localView{};
    const ReacParseStatus status = parse(packet, packetBytes, localView);
    if (status != ReacParseStatus::ok) {
        if (view) {
            *view = localView;
        }
        return status;
    }

    constexpr size_t kRequiredSamples = kReacSamplesPerPacket * kReacChannelCount;
    if (!interleaved || interleavedSampleCapacity < kRequiredSamples) {
        return ReacParseStatus::outputTooSmall;
    }

    for (uint32_t sample = 0; sample < kReacSamplesPerPacket; ++sample) {
        float* frame = interleaved + static_cast<size_t>(sample) * kReacChannelCount;
        for (uint32_t channel = 0; channel < kReacChannelCount; ++channel) {
            frame[channel] = 0.0f;
        }
        for (uint32_t channel = 0; channel < localView.channelCount; ++channel) {
            frame[channel] = decodeSample(localView.pcmPayload, localView.channelCount, channel, sample);
        }
    }

    if (view) {
        *view = localView;
    }
    return ReacParseStatus::ok;
}

const char* ReacPacketParser::toString(ReacParseStatus status) noexcept {
    switch (status) {
    case ReacParseStatus::ok:
        return "ok";
    case ReacParseStatus::nullPacket:
        return "null packet";
    case ReacParseStatus::tooShort:
        return "packet too short";
    case ReacParseStatus::wrongEtherType:
        return "wrong EtherType";
    case ReacParseStatus::badTrailer:
        return "bad REAC trailer";
    case ReacParseStatus::nonAudioPacket:
        return "non-audio REAC packet";
    case ReacParseStatus::unsupportedPayload:
        return "unsupported REAC payload";
    case ReacParseStatus::outputTooSmall:
        return "output buffer too small";
    }
    return "unknown";
}

float ReacPacketParser::decodeSample(const uint8_t* payload, uint32_t packetChannelCount, uint32_t channel, uint32_t sampleIndex) noexcept {
    const uint8_t* base = payload + sampleIndex * packetChannelCount * kReacBytesPerSample;
    const uint8_t* pair = base + (channel & ~1U) * kReacBytesPerSample;

    uint32_t raw = 0;
    if ((channel & 1U) == 0) {
        raw = static_cast<uint32_t>(pair[3]) |
              (static_cast<uint32_t>(pair[0]) << 8) |
              (static_cast<uint32_t>(pair[1]) << 16);
    } else {
        raw = static_cast<uint32_t>(pair[4]) |
              (static_cast<uint32_t>(pair[5]) << 8) |
              (static_cast<uint32_t>(pair[2]) << 16);
    }

    const int32_t signed24 = (raw & 0x800000U)
                                 ? static_cast<int32_t>(raw) - 0x1000000
                                 : static_cast<int32_t>(raw);
    return static_cast<float>(signed24) / 8388608.0f;
}

} // namespace windows_reac_asio
