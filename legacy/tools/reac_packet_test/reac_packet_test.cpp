#include "reac/reac_packet_parser.h"
#include "reac/reac_packet_encoder.h"
#include "reac/reac_return_map.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

using namespace windows_reac_asio;

namespace {

void encodeSample(uint8_t* payload, uint32_t packetChannelCount, uint32_t channel, uint32_t sampleIndex, int32_t signed24) {
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

void encodeSample(uint8_t* payload, uint32_t channel, uint32_t sampleIndex, int32_t signed24) {
    encodeSample(payload, kReacChannelCount, channel, sampleIndex, signed24);
}

float decodeSample(const uint8_t* payload, uint32_t packetChannelCount, uint32_t channel, uint32_t sampleIndex) {
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
        ? static_cast<int32_t>(raw | 0xff000000U)
        : static_cast<int32_t>(raw);
    return static_cast<float>(signed24) / 8388608.0f;
}

bool closeEnough(float actual, float expected) {
    return std::fabs(actual - expected) < 0.000001f;
}

} // namespace

int main() {
    std::array<uint8_t, kReacMinimumPacketBytes> packet{};
    packet[12] = 0x88;
    packet[13] = 0x19;
    packet[14] = 0x34;
    packet[15] = 0x12;
    packet[packet.size() - 2] = 0xC2;
    packet[packet.size() - 1] = 0xEA;

    uint8_t* payload = packet.data() + kReacPayloadOffset;
    encodeSample(payload, 0, 0, 0x400000);
    encodeSample(payload, 1, 0, -0x400000);
    encodeSample(payload, 38, 11, 0x100000);
    encodeSample(payload, 39, 11, -0x100000);

    ReacPacketParser parser;
    std::array<float, kReacSamplesPerPacket * kReacChannelCount> decoded{};
    ReacPacketView view{};
    const ReacParseStatus status = parser.decodeInterleavedFloat(packet.data(), packet.size(), decoded.data(), decoded.size(), &view);
    if (status != ReacParseStatus::ok) {
        std::printf("decode failed: %s\n", ReacPacketParser::toString(status));
        return 1;
    }
    if (view.counter != 0x1234) {
        std::printf("counter mismatch: %u\n", view.counter);
        return 1;
    }
    if (!closeEnough(decoded[0], 0.5f) || !closeEnough(decoded[1], -0.5f)) {
        std::printf("first pair mismatch: %f %f\n", decoded[0], decoded[1]);
        return 1;
    }
    const size_t lastFrame = (kReacSamplesPerPacket - 1) * kReacChannelCount;
    if (!closeEnough(decoded[lastFrame + 38], 0.125f) || !closeEnough(decoded[lastFrame + 39], -0.125f)) {
        std::printf("last pair mismatch: %f %f\n", decoded[lastFrame + 38], decoded[lastFrame + 39]);
        return 1;
    }

    std::array<uint8_t, kReacMinimumPacketBytes> controlPacket = packet;
    controlPacket[16] = 0xcd;
    controlPacket[17] = 0xea;
    view = {};
    const ReacParseStatus controlStatus = parser.decodeInterleavedFloat(
        controlPacket.data(),
        controlPacket.size(),
        decoded.data(),
        decoded.size(),
        &view);
    if (controlStatus != ReacParseStatus::ok || view.counter != 0x1234 || view.packetType != 0xeacd) {
        std::printf(
            "control packet status mismatch: status=%s counter=%u type=0x%04x\n",
            ReacPacketParser::toString(controlStatus),
            view.counter,
            view.packetType);
        return 1;
    }
    if (!closeEnough(decoded[0], 0.5f) || !closeEnough(decoded[1], -0.5f)) {
        std::printf("control packet audio mismatch: %f %f\n", decoded[0], decoded[1]);
        return 1;
    }

    constexpr uint32_t kShortChannelCount = 8;
    constexpr size_t kShortPacketBytes =
        kReacPayloadOffset + (kReacSamplesPerPacket * kShortChannelCount * kReacBytesPerSample) + 2;
    std::array<uint8_t, kShortPacketBytes> shortPacket{};
    shortPacket[12] = 0x88;
    shortPacket[13] = 0x19;
    shortPacket[14] = 0x78;
    shortPacket[15] = 0x56;
    shortPacket[shortPacket.size() - 2] = 0xC2;
    shortPacket[shortPacket.size() - 1] = 0xEA;

    uint8_t* shortPayload = shortPacket.data() + kReacPayloadOffset;
    encodeSample(shortPayload, kShortChannelCount, 0, 0, 0x200000);
    encodeSample(shortPayload, kShortChannelCount, 7, 11, -0x200000);

    decoded.fill(1.0f);
    view = {};
    const ReacParseStatus shortStatus = parser.decodeInterleavedFloat(
        shortPacket.data(),
        shortPacket.size(),
        decoded.data(),
        decoded.size(),
        &view);
    if (shortStatus != ReacParseStatus::ok) {
        std::printf("short decode failed: %s\n", ReacPacketParser::toString(shortStatus));
        return 1;
    }
    if (view.counter != 0x5678 || view.channelCount != kShortChannelCount) {
        std::printf("short metadata mismatch: counter=%u channels=%u\n", view.counter, view.channelCount);
        return 1;
    }
    if (!closeEnough(decoded[0], 0.25f)) {
        std::printf("short first channel mismatch: %f\n", decoded[0]);
        return 1;
    }
    const size_t shortLastFrame = (kReacSamplesPerPacket - 1) * kReacChannelCount;
    if (!closeEnough(decoded[shortLastFrame + 7], -0.25f) || !closeEnough(decoded[shortLastFrame + 8], 0.0f)) {
        std::printf("short channel/silence mismatch: %f %f\n", decoded[shortLastFrame + 7], decoded[shortLastFrame + 8]);
        return 1;
    }

    ReacPacketEncoder encoder;
    std::array<float, kReacSamplesPerPacket * kShortChannelCount> encodeInput{};
    for (uint32_t frame = 0; frame < kReacSamplesPerPacket; ++frame) {
        for (uint32_t channel = 0; channel < kShortChannelCount; ++channel) {
            encodeInput[static_cast<size_t>(frame) * kShortChannelCount + channel] =
                static_cast<float>(static_cast<int>(frame) - static_cast<int>(channel)) / 32.0f;
        }
    }

    std::array<uint8_t, kShortPacketBytes> encoded{};
    size_t encodedBytes = 0;
    const ReacEncodeStatus encodeStatus = encoder.encodeInterleavedFloat(
        encodeInput.data(),
        kShortChannelCount,
        kReacSamplesPerPacket,
        0x9abc,
        encoded.data(),
        encoded.size(),
        &encodedBytes);
    if (encodeStatus != ReacEncodeStatus::ok || encodedBytes != encoded.size()) {
        std::printf("encode failed: %s bytes=%zu\n", ReacPacketEncoder::toString(encodeStatus), encodedBytes);
        return 1;
    }

    decoded.fill(0.0f);
    view = {};
    const ReacParseStatus roundTripStatus = parser.decodeInterleavedFloat(
        encoded.data(),
        encodedBytes,
        decoded.data(),
        decoded.size(),
        &view);
    if (roundTripStatus != ReacParseStatus::ok) {
        std::printf("round-trip decode failed: %s\n", ReacPacketParser::toString(roundTripStatus));
        return 1;
    }
    if (view.counter != 0x9abc || view.channelCount != kShortChannelCount) {
        std::printf("round-trip metadata mismatch: counter=%u channels=%u\n", view.counter, view.channelCount);
        return 1;
    }
    for (uint32_t frame = 0; frame < kReacSamplesPerPacket; ++frame) {
        for (uint32_t channel = 0; channel < kShortChannelCount; ++channel) {
            const float expected = encodeInput[static_cast<size_t>(frame) * kShortChannelCount + channel];
            const float actual = decoded[static_cast<size_t>(frame) * kReacChannelCount + channel];
            if (std::fabs(actual - expected) > 0.0000015f) {
                std::printf("round-trip sample mismatch frame=%u channel=%u actual=%f expected=%f\n", frame, channel, actual, expected);
                return 1;
            }
        }
    }

    constexpr uint32_t kS1608ReturnChannels = kS1608ReturnWireChannels;
    constexpr uint32_t kS1608ReturnFrames = 6;
    constexpr size_t kS1608PacketBytes =
        kReacPayloadOffset + (kS1608ReturnFrames * kS1608ReturnChannels * kReacBytesPerSample) + 2;
    static_assert(kS1608PacketBytes == 340, "S-1608 return packet must stay 340 bytes");
    if (ReacPacketEncoder::packetBytesForLayout(kS1608ReturnChannels, kS1608ReturnFrames) != kS1608PacketBytes) {
        std::printf("S-1608 layout size mismatch\n");
        return 1;
    }
    if (mapReacReturnSourceChannel(ReacReturnMapMode::s1608MirrorFirst8, 0, kS1608ReturnWireChannels) != 0 ||
        mapReacReturnSourceChannel(ReacReturnMapMode::s1608MirrorFirst8, 1, kS1608ReturnWireChannels) != 1 ||
        mapReacReturnSourceChannel(ReacReturnMapMode::s1608MirrorFirst8, 8, kS1608ReturnWireChannels) != 0 ||
        mapReacReturnSourceChannel(ReacReturnMapMode::s1608MirrorFirst8, 9, kS1608ReturnWireChannels) != 1 ||
        mapReacReturnSourceChannel(ReacReturnMapMode::direct, 9, kS1608ReturnWireChannels) != 9 ||
        mapReacReturnSourceChannel(ReacReturnMapMode::direct, 16, kS1608ReturnWireChannels) != kInvalidReacSourceChannel) {
        std::printf("S-1608 return map mismatch\n");
        return 1;
    }

    std::array<float, kS1608ReturnFrames * kS1608ReturnChannels> s1608Input{};
    for (uint32_t frame = 0; frame < kS1608ReturnFrames; ++frame) {
        s1608Input[static_cast<size_t>(frame) * kS1608ReturnChannels + 0] = 0.25f;
        s1608Input[static_cast<size_t>(frame) * kS1608ReturnChannels + 1] = -0.25f;
        s1608Input[static_cast<size_t>(frame) * kS1608ReturnChannels + 14] = 0.125f;
        s1608Input[static_cast<size_t>(frame) * kS1608ReturnChannels + 15] = -0.125f;
    }

    std::array<uint8_t, kS1608PacketBytes> s1608Encoded{};
    encodedBytes = 0;
    const ReacEncodeStatus s1608EncodeStatus = encoder.encodeInterleavedFloat(
        s1608Input.data(),
        kS1608ReturnChannels,
        kS1608ReturnFrames,
        0x2468,
        s1608Encoded.data(),
        s1608Encoded.size(),
        &encodedBytes);
    if (s1608EncodeStatus != ReacEncodeStatus::ok || encodedBytes != s1608Encoded.size()) {
        std::printf("S-1608 16x6 encode failed: %s bytes=%zu\n", ReacPacketEncoder::toString(s1608EncodeStatus), encodedBytes);
        return 1;
    }
    if (s1608Encoded[14] != 0x68 || s1608Encoded[15] != 0x24 ||
        s1608Encoded[s1608Encoded.size() - 2] != 0xc2 ||
        s1608Encoded[s1608Encoded.size() - 1] != 0xea) {
        std::printf("S-1608 16x6 packet metadata mismatch\n");
        return 1;
    }
    const uint8_t* s1608Payload = s1608Encoded.data() + kReacPayloadOffset;
    if (!closeEnough(decodeSample(s1608Payload, kS1608ReturnChannels, 0, 0), 0.25f) ||
        !closeEnough(decodeSample(s1608Payload, kS1608ReturnChannels, 1, 0), -0.25f) ||
        !closeEnough(decodeSample(s1608Payload, kS1608ReturnChannels, 14, 5), 0.125f) ||
        !closeEnough(decodeSample(s1608Payload, kS1608ReturnChannels, 15, 5), -0.125f)) {
        std::printf("S-1608 16x6 sample layout mismatch\n");
        return 1;
    }

    std::puts("reac_packet_test passed");
    return 0;
}
