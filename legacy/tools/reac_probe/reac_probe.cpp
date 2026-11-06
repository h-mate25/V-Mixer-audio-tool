#include "reac/pcap_capture.h"
#include "reac/reac_packet_parser.h"

#include <chrono>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

using namespace windows_reac_asio;

namespace {

struct Options {
    bool showHelp = false;
    bool listDevices = false;
    bool inspect = false;
    std::string deviceSelector;
    std::string wavPath;
    int seconds = 10;
    int inspectPackets = 16;
};

void printUsage() {
    std::puts("reac_probe - REAC/H-8819 raw Ethernet probe");
    std::puts("");
    std::puts("Usage:");
    std::puts("  reac_probe --list-devices");
    std::puts("  reac_probe --device <index|name|description-fragment> [--seconds N] [--wav file.wav] [--inspect]");
    std::puts("");
    std::puts("Examples:");
    std::puts("  reac_probe --list-devices");
    std::puts("  reac_probe --device Realtek --seconds 10 --wav reac_40ch.wav");
    std::puts("  reac_probe --device 4 --seconds 10 --inspect");
    std::puts("");
    std::puts("--wav writes 40 mono 24-bit PCM files using the supplied path as a prefix:");
    std::puts("  reac_40ch-01.wav ... reac_40ch-40.wav");
    std::puts("--inspect prints packet metadata for the first packets.");
}

bool parseArgs(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            options.showHelp = true;
            return true;
        }
        if (arg == "--list-devices") {
            options.listDevices = true;
        } else if ((arg == "--device" || arg == "-d") && i + 1 < argc) {
            options.deviceSelector = argv[++i];
        } else if (arg == "--seconds" && i + 1 < argc) {
            options.seconds = std::atoi(argv[++i]);
        } else if (arg == "--wav" && i + 1 < argc) {
            options.wavPath = argv[++i];
        } else if (arg == "--inspect") {
            options.inspect = true;
        } else if (arg == "--inspect-packets" && i + 1 < argc) {
            options.inspectPackets = std::atoi(argv[++i]);
        } else {
            std::fprintf(stderr, "Unknown or incomplete argument: %s\n", arg.c_str());
            printUsage();
            return false;
        }
    }
    if (options.inspectPackets < 1) {
        options.inspectPackets = 1;
    }
    return true;
}

uint16_t readLe16(const uint8_t* data) {
    return static_cast<uint16_t>(data[0] | (static_cast<uint16_t>(data[1]) << 8));
}

uint16_t readBe16(const uint8_t* data) {
    return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
}

std::string formatMac(const uint8_t* mac) {
    char text[18] = {};
    std::snprintf(
        text,
        sizeof(text),
        "%02x:%02x:%02x:%02x:%02x:%02x",
        mac[0],
        mac[1],
        mac[2],
        mac[3],
        mac[4],
        mac[5]);
    return text;
}

std::string formatHex(const uint8_t* data, size_t bytes) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string text;
    text.reserve(bytes * 2);
    for (size_t i = 0; i < bytes; ++i) {
        text.push_back(kHex[(data[i] >> 4) & 0x0f]);
        text.push_back(kHex[data[i] & 0x0f]);
    }
    return text;
}

void printInspectPacket(uint64_t packetIndex, const CapturedPacket& packet, const ReacPacketView& view, ReacParseStatus status) {
    if (!packet.data || packet.size < kReacEthernetHeaderBytes) {
        std::printf(
            "inspect #%llu len=%u wire=%u status=%s\n",
            static_cast<unsigned long long>(packetIndex),
            packet.size,
            packet.wireSize,
            ReacPacketParser::toString(status));
        return;
    }

    const uint16_t etherType = readBe16(packet.data + 12);
    const uint16_t counter = packet.size >= 16 ? readLe16(packet.data + 14) : 0;
    const uint16_t packetType = packet.size >= 18 ? readLe16(packet.data + 16) : 0;
    const std::string header = packet.size >= kReacPayloadOffset ? formatHex(packet.data + 18, 32) : "";

    std::printf(
        "inspect #%llu len=%u wire=%u src=%s dst=%s eth=0x%04x counter=%u type=0x%04x status=%s channels=%u header=%s\n",
        static_cast<unsigned long long>(packetIndex),
        packet.size,
        packet.wireSize,
        formatMac(packet.data + 6).c_str(),
        formatMac(packet.data).c_str(),
        etherType,
        counter,
        packetType,
        ReacPacketParser::toString(status),
        status == ReacParseStatus::ok ? view.channelCount : 0,
        header.c_str());
}

void writeU16(std::ofstream& out, uint16_t value) {
    out.put(static_cast<char>(value & 0xff));
    out.put(static_cast<char>((value >> 8) & 0xff));
}

void writeU32(std::ofstream& out, uint32_t value) {
    out.put(static_cast<char>(value & 0xff));
    out.put(static_cast<char>((value >> 8) & 0xff));
    out.put(static_cast<char>((value >> 16) & 0xff));
    out.put(static_cast<char>((value >> 24) & 0xff));
}

int32_t floatToSigned24(float value) {
    if (value > 0.99999988f) {
        value = 0.99999988f;
    } else if (value < -1.0f) {
        value = -1.0f;
    }
    return static_cast<int32_t>(std::lrint(static_cast<double>(value) * 8388607.0));
}

void writeS24Le(std::ofstream& out, int32_t value) {
    const uint32_t raw = static_cast<uint32_t>(value) & 0x00ffffffU;
    out.put(static_cast<char>(raw & 0xff));
    out.put(static_cast<char>((raw >> 8) & 0xff));
    out.put(static_cast<char>((raw >> 16) & 0xff));
}

std::string wavChannelPath(const std::string& requestedPath, uint32_t channel) {
    std::string prefix = requestedPath;
    const size_t dot = prefix.find_last_of('.');
    const size_t slash = prefix.find_last_of("\\/");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        prefix.erase(dot);
    }

    char suffix[16] = {};
    std::snprintf(suffix, sizeof(suffix), "-%02u.wav", channel + 1);
    return prefix + suffix;
}

class MonoWavSetWriter {
public:
    bool open(const std::string& path) {
        dataBytes_ = 0;
        for (uint32_t channel = 0; channel < kReacChannelCount; ++channel) {
            paths_[channel] = wavChannelPath(path, channel);
            outputs_[channel].open(paths_[channel], std::ios::binary | std::ios::trunc);
            if (!outputs_[channel]) {
                close();
                return false;
            }
            outputs_[channel].write("RIFF", 4);
            writeU32(outputs_[channel], 0);
            outputs_[channel].write("WAVE", 4);
            outputs_[channel].write("fmt ", 4);
            writeU32(outputs_[channel], 16);
            writeU16(outputs_[channel], 1); // PCM
            writeU16(outputs_[channel], 1); // mono
            writeU32(outputs_[channel], kReacSampleRate);
            writeU32(outputs_[channel], kReacSampleRate * 3);
            writeU16(outputs_[channel], 3);
            writeU16(outputs_[channel], 24);
            outputs_[channel].write("data", 4);
            writeU32(outputs_[channel], 0);
        }
        return true;
    }

    void write(const float* interleaved, uint32_t frames) {
        if (!interleaved || frames == 0) {
            return;
        }

        for (uint32_t frame = 0; frame < frames; ++frame) {
            const float* frameSamples = interleaved + static_cast<size_t>(frame) * kReacChannelCount;
            for (uint32_t channel = 0; channel < kReacChannelCount; ++channel) {
                if (outputs_[channel]) {
                    writeS24Le(outputs_[channel], floatToSigned24(frameSamples[channel]));
                }
            }
        }

        dataBytes_ += static_cast<uint64_t>(frames) * 3ULL;
    }

    void close() {
        const uint32_t dataBytes32 = static_cast<uint32_t>(dataBytes_);
        for (std::ofstream& output : outputs_) {
            if (!output) {
                continue;
            }
            output.seekp(4, std::ios::beg);
            writeU32(output, 36U + dataBytes32);
            output.seekp(40, std::ios::beg);
            writeU32(output, dataBytes32);
            output.close();
        }
    }

    ~MonoWavSetWriter() {
        close();
    }

    const std::string& firstPath() const noexcept { return paths_[0]; }
    const std::string& lastPath() const noexcept { return paths_[kReacChannelCount - 1]; }

private:
    std::array<std::ofstream, kReacChannelCount> outputs_;
    std::array<std::string, kReacChannelCount> paths_;
    uint64_t dataBytes_ = 0;
};

void printDevices(const std::vector<CaptureDeviceInfo>& devices) {
    for (size_t i = 0; i < devices.size(); ++i) {
        std::printf("%zu. %s\n   %s\n", i + 1, devices[i].description.c_str(), devices[i].name.c_str());
    }
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parseArgs(argc, argv, options)) {
        return 1;
    }
    if (options.showHelp) {
        printUsage();
        return 0;
    }

    PcapCapture capture;
    if (!capture.available()) {
        std::fprintf(stderr, "%s\n", capture.lastError().c_str());
        return 1;
    }

    const std::vector<CaptureDeviceInfo> devices = capture.listDevices();
    if (devices.empty()) {
        std::fprintf(stderr, "No Npcap devices found. %s\n", capture.lastError().c_str());
        return 1;
    }

    if (options.listDevices || options.deviceSelector.empty()) {
        printDevices(devices);
        return options.listDevices ? 0 : 1;
    }

    const std::string deviceName = capture.resolveDeviceName(options.deviceSelector, devices);
    if (deviceName.empty()) {
        std::fprintf(stderr, "Could not resolve capture device: %s\n", options.deviceSelector.c_str());
        printDevices(devices);
        return 1;
    }

    if (!capture.open(deviceName)) {
        std::fprintf(stderr, "Could not open capture device: %s\n", capture.lastError().c_str());
        return 1;
    }

    MonoWavSetWriter wav;
    if (!options.wavPath.empty() && !wav.open(options.wavPath)) {
        std::fprintf(stderr, "Could not open WAV set for writing from prefix: %s\n", options.wavPath.c_str());
        return 1;
    }

    std::printf("Listening on %s\n", deviceName.c_str());
    if (options.seconds > 0) {
        std::printf("Capture duration: %d seconds\n", options.seconds);
    } else {
        std::puts("Capture duration: unlimited");
    }
    if (!options.wavPath.empty()) {
        std::printf("Writing mono WAV files: %s ... %s\n", wav.firstPath().c_str(), wav.lastPath().c_str());
    }
    if (options.inspect) {
        std::printf("Inspecting first %d packets and first %d non-audio packets after that\n", options.inspectPackets, options.inspectPackets);
    }

    ReacPacketParser parser;
    std::vector<float> decoded(kReacSamplesPerPacket * kReacChannelCount, 0.0f);

    uint64_t totalPackets = 0;
    uint64_t decodedPackets = 0;
    uint64_t nonAudioPackets = 0;
    uint64_t parseDrops = 0;
    uint64_t tooShortDrops = 0;
    uint64_t wrongEtherTypeDrops = 0;
    uint64_t badTrailerDrops = 0;
    uint64_t outputTooSmallDrops = 0;
    uint64_t unsupportedPayloadDrops = 0;
    uint64_t missingPackets = 0;
    uint32_t minDecodedChannels = UINT32_MAX;
    uint32_t maxDecodedChannels = 0;
    uint64_t bytesSeen = 0;
    uint64_t wireBytesSeen = 0;
    uint32_t minCapturedSize = UINT32_MAX;
    uint32_t maxCapturedSize = 0;
    uint32_t minWireSize = UINT32_MAX;
    uint32_t maxWireSize = 0;
    bool haveCounter = false;
    uint16_t lastCounter = 0;
    int inspectedPackets = 0;
    int inspectedNonAudioPackets = 0;

    const auto accountCounter = [&](uint16_t counter) {
        if (haveCounter) {
            const uint16_t expected = static_cast<uint16_t>(lastCounter + 1);
            if (counter != expected) {
                const uint16_t skipped = static_cast<uint16_t>(counter - expected);
                missingPackets += skipped ? skipped : 1;
            }
        }
        haveCounter = true;
        lastCounter = counter;
    };

    const auto started = std::chrono::steady_clock::now();
    auto lastPrint = started;

    while (true) {
        const auto now = std::chrono::steady_clock::now();
        if (options.seconds > 0 && now - started >= std::chrono::seconds(options.seconds)) {
            break;
        }

        if (!capture.captureOnce(100, [&](const CapturedPacket& packet) {
                ++totalPackets;
                bytesSeen += packet.size;
                wireBytesSeen += packet.wireSize;
                minCapturedSize = std::min(minCapturedSize, packet.size);
                maxCapturedSize = std::max(maxCapturedSize, packet.size);
                minWireSize = std::min(minWireSize, packet.wireSize);
                maxWireSize = std::max(maxWireSize, packet.wireSize);

                ReacPacketView view{};
                const ReacParseStatus status = parser.decodeInterleavedFloat(
                    packet.data,
                    packet.size,
                    decoded.data(),
                    decoded.size(),
                    &view);

                if (options.inspect) {
                    bool shouldPrintInspect = inspectedPackets < options.inspectPackets;
                    bool shouldPrintNonAudio = status == ReacParseStatus::nonAudioPacket && inspectedNonAudioPackets < options.inspectPackets;
                    if (shouldPrintInspect || shouldPrintNonAudio) {
                        printInspectPacket(totalPackets, packet, view, status);
                        if (shouldPrintInspect) {
                            ++inspectedPackets;
                        } else {
                            ++inspectedNonAudioPackets;
                        }
                    }
                }

                if (status == ReacParseStatus::nonAudioPacket) {
                    ++nonAudioPackets;
                    accountCounter(view.counter);
                    if (!options.wavPath.empty()) {
                        std::fill(decoded.begin(), decoded.end(), 0.0f);
                        wav.write(decoded.data(), kReacSamplesPerPacket);
                    }
                    return true;
                }

                if (status != ReacParseStatus::ok) {
                    ++parseDrops;
                    switch (status) {
                    case ReacParseStatus::tooShort:
                        ++tooShortDrops;
                        break;
                    case ReacParseStatus::wrongEtherType:
                        ++wrongEtherTypeDrops;
                        break;
                    case ReacParseStatus::badTrailer:
                        ++badTrailerDrops;
                        break;
                    case ReacParseStatus::unsupportedPayload:
                        ++unsupportedPayloadDrops;
                        break;
                    case ReacParseStatus::outputTooSmall:
                        ++outputTooSmallDrops;
                        break;
                    default:
                        break;
                    }
                    return true;
                }

                accountCounter(view.counter);
                minDecodedChannels = std::min(minDecodedChannels, view.channelCount);
                maxDecodedChannels = std::max(maxDecodedChannels, view.channelCount);
                ++decodedPackets;
                wav.write(decoded.data(), kReacSamplesPerPacket);
                return true;
            })) {
            std::fprintf(stderr, "Capture stopped: %s\n", capture.lastError().c_str());
            return 1;
        }

        const auto after = std::chrono::steady_clock::now();
        if (after - lastPrint >= std::chrono::seconds(1)) {
            std::printf(
                "packets=%llu decoded=%llu nonAudio=%llu parseDrops=%llu tooShort=%llu badTrailer=%llu unsupportedPayload=%llu missing=%llu channels=%u..%u capBytes=%llu wireBytes=%llu capLen=%u..%u wireLen=%u..%u lastCounter=%u\n",
                static_cast<unsigned long long>(totalPackets),
                static_cast<unsigned long long>(decodedPackets),
                static_cast<unsigned long long>(nonAudioPackets),
                static_cast<unsigned long long>(parseDrops),
                static_cast<unsigned long long>(tooShortDrops),
                static_cast<unsigned long long>(badTrailerDrops),
                static_cast<unsigned long long>(unsupportedPayloadDrops),
                static_cast<unsigned long long>(missingPackets),
                minDecodedChannels == UINT32_MAX ? 0 : minDecodedChannels,
                maxDecodedChannels,
                static_cast<unsigned long long>(bytesSeen),
                static_cast<unsigned long long>(wireBytesSeen),
                minCapturedSize == UINT32_MAX ? 0 : minCapturedSize,
                maxCapturedSize,
                minWireSize == UINT32_MAX ? 0 : minWireSize,
                maxWireSize,
                static_cast<unsigned>(lastCounter));
            lastPrint = after;
        }
    }

    wav.close();
    std::printf(
        "Done. packets=%llu decoded=%llu nonAudio=%llu parseDrops=%llu tooShort=%llu wrongEtherType=%llu badTrailer=%llu unsupportedPayload=%llu outputTooSmall=%llu missing=%llu channels=%u..%u capBytes=%llu wireBytes=%llu capLen=%u..%u wireLen=%u..%u\n",
        static_cast<unsigned long long>(totalPackets),
        static_cast<unsigned long long>(decodedPackets),
        static_cast<unsigned long long>(nonAudioPackets),
        static_cast<unsigned long long>(parseDrops),
        static_cast<unsigned long long>(tooShortDrops),
        static_cast<unsigned long long>(wrongEtherTypeDrops),
        static_cast<unsigned long long>(badTrailerDrops),
        static_cast<unsigned long long>(unsupportedPayloadDrops),
        static_cast<unsigned long long>(outputTooSmallDrops),
        static_cast<unsigned long long>(missingPackets),
        minDecodedChannels == UINT32_MAX ? 0 : minDecodedChannels,
        maxDecodedChannels,
        static_cast<unsigned long long>(bytesSeen),
        static_cast<unsigned long long>(wireBytesSeen),
        minCapturedSize == UINT32_MAX ? 0 : minCapturedSize,
        maxCapturedSize,
        minWireSize == UINT32_MAX ? 0 : minWireSize,
        maxWireSize);
    return 0;
}
