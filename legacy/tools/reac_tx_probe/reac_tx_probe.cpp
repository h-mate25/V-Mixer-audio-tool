#include "reac/pcap_capture.h"
#include "reac/pcap_sender.h"
#include "reac/reac_packet_encoder.h"

#include <winsock2.h>
#include <Windows.h>
#include <Iphlpapi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

using namespace windows_reac_asio;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr uint32_t kDefaultPacketsPerSecond = 4000;
constexpr uint32_t kMaxQueuePacketsPerSecond = 4200;
constexpr int kDefaultSeconds = 3;
constexpr int kMaxSendSecondsWithoutForce = 10;
constexpr double kDefaultToneLevelDb = -48.0;
constexpr uint8_t kBroadcastMac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
constexpr uint8_t kFallbackSourceMac[6] = {0x02, 0x57, 0x52, 0x41, 0x53, 0x49};
constexpr uint8_t kObservedS1608Mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
constexpr uint8_t kObservedMixerMac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x02};
constexpr uint8_t kControlTypeOne[5] = {0x01, 0x00, 0x00, 0x1a, 0x00};
constexpr uint8_t kSlaveTriggerControlType[5] = {0x01, 0x03, 0x00, 0x19, 0x01};
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

std::atomic<bool> gStop{false};

struct Options {
    bool showHelp = false;
    bool listDevices = false;
    bool send = false;
    bool force = false;
    bool clocked = false;
    bool clockFollow = false;
    bool slaveHandshake = false;
    bool slaveHandshakeContinuousCounter = false;
    bool slaveLocalCounter = false;
    bool slavePerGronState = false;
    bool slaveTriggerResponder = false;
    bool slaveStandby = false;
    bool slaveStandbyResponder = false;
    bool havePacketsPerSecond = false;
    bool haveSampleRate = false;
    std::string deviceSelector;
    std::string profile = "s1608";
    uint32_t channels = 0;
    int seconds = kDefaultSeconds;
    uint32_t audioSampleRate = kReacSampleRate;
    uint32_t packetsPerSecond = kDefaultPacketsPerSecond;
    uint32_t queuePacketsPerSecond = 0;
    uint16_t counter = 0;
    bool haveSourceMac = false;
    std::array<uint8_t, 6> sourceMac{};
    bool haveDestinationMac = false;
    std::array<uint8_t, 6> destinationMac{};
    bool haveHeaderRepeat = false;
    uint8_t headerRepeat = 0;
    bool syncCounter = false;
    uint32_t syncWaitMs = 2000;
    uint32_t syncLeadPackets = 64;
    bool haveSyncSourceMac = false;
    std::array<uint8_t, 6> syncSourceMac{};
    uint32_t clockOffsetPackets = 0;
    uint32_t slaveHandshakeWaitMs = 3000;
    uint32_t slaveKeepaliveMs = 0;
    uint32_t slavePrerollMs = 0;
    uint32_t slaveStandbyChunkMs = 250;
    uint32_t slaveTriggerDebounceMs = 200;
    uint32_t streamChunkMs = 0;
    int toneChannel = 0;
    double toneHz = 1000.0;
    double toneLevelDb = kDefaultToneLevelDb;
};

struct CounterSyncResult {
    bool found = false;
    uint16_t counter = 0;
    std::array<uint8_t, 6> sourceMac{};
    std::chrono::steady_clock::time_point observedAt{};
};

BOOL WINAPI consoleCtrlHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        gStop.store(true, std::memory_order_release);
        return TRUE;
    }
    return FALSE;
}

void printUsage() {
    std::puts("reac_tx_probe - cautious REAC/H-8819 transmit probe");
    std::puts("");
    std::puts("Usage:");
    std::puts("  reac_tx_probe --list-devices");
    std::puts("  reac_tx_probe --device <index|name|description-fragment> [options]");
    std::puts("");
    std::puts("Safe defaults:");
    std::puts("  profile=s1608, channels=8, seconds=3, silence, dry-run only");
    std::puts("  packets are sent only when --send is present");
    std::puts("");
    std::puts("Examples:");
    std::puts("  reac_tx_probe --list-devices");
    std::puts("  reac_tx_probe --device 4");
    std::puts("  reac_tx_probe --device 4 --send");
    std::puts("  reac_tx_probe --device 4 --send --tone-channel 1");
    std::puts("");
    std::puts("Options:");
    std::puts("  --profile s1608|s1608-slave|mixer8|mixer40");
    std::puts("                              s1608: 8ch PC-as-Master test using NIC MAC");
    std::puts("                              s1608-slave: 8ch S-1608-like Slave stream to mixer");
    std::puts("                              mixer8: 8ch mixer-like Master phase to snake");
    std::puts("                              mixer40: 40ch mixer-like stream shape");
    std::puts("  --stable-slave             chosen low-variation S-1608 slave preset");
    std::puts("  --channels N              even channel count, 2..40; overrides profile default");
    std::puts("  --seconds N               duration; sending more than 10 seconds requires --force");
    std::puts("  --sample-rate N           44100 or 48000; derives pps as sampleRate/12");
    std::puts("  --pps N                   packets per second; default 4000");
    std::puts("  --queue-pps N             timed-queue pacing pps; default follows --pps");
    std::puts("  --counter N               initial 16-bit REAC counter");
    std::puts("  --sync-counter            listen first and start near the live REAC counter");
    std::puts("  --sync-ms N               max sync wait; default 2000");
    std::puts("  --sync-lead N             extra packets after observed counter; default 64");
    std::puts("  --sync-source-mac aa:bb   only sync/clock to this REAC source MAC");
    std::puts("  --clocked                 send one packet for each live REAC clock packet");
    std::puts("  --clock-follow            measure master clock and tune chunk send rate");
    std::puts("  --clock-offset N          counter offset from clock packet; default 0");
    std::puts("  --slave-handshake         send an experimental S-1608-like EACD announce");
    std::puts("  --handshake-continuous-counter");
    std::puts("                              after a handshake, continue near the announce counter");
    std::puts("  --slave-local-counter     keep the PC/S-1608 source counter continuous");
    std::puts("  --slave-per-gron-state    require master identity before slave trigger response");
    std::puts("  --handshake-ms N          max handshake wait; default 3000");
    std::puts("  --slave-preroll-ms N      send S-1608-like broadcast audio before handshake");
    std::puts("  --slave-standby           broadcast as standalone S-1608 until mixer appears");
    std::puts("  --standby-chunk-ms N      standby broadcast block size; default 250");
    std::puts("  --slave-standby-responder answer first standby trigger from a capture thread");
    std::puts("  --slave-keepalive-ms N    insert slave announce keepalives during queued audio");
    std::puts("  --slave-trigger-responder answer mixer EACD triggers during queued audio");
    std::puts("  --trigger-debounce-ms N   min gap between trigger responses; default 200");
    std::puts("  --stream-chunk-ms N       chunk long timed streams and queue trigger replies");
    std::puts("  --source-mac aa:bb:...    default is the selected adapter MAC if available");
    std::puts("  --dest-mac aa:bb:...      default is broadcast; handshake auto-targets mixer");
    std::puts("  --header-repeat XX        fill the 32-byte REAC header with 00 XX repeated");
    std::puts("  --tone-channel N          add a low-level sine tone on one channel; otherwise silence");
    std::puts("  --tone-hz N               default 1000");
    std::puts("  --level-db N              default -48; values above -18 are refused");
    std::puts("  --send                    actually transmit packets");
    std::puts("  --force                   allow longer send durations");
}

int parseInt(const char* text, int fallback) {
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (!end || *end != '\0') {
        return fallback;
    }
    return static_cast<int>(value);
}

uint32_t parseUInt32(const char* text, uint32_t fallback) {
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (!end || *end != '\0') {
        return fallback;
    }
    return static_cast<uint32_t>(value);
}

double parseDouble(const char* text, double fallback) {
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (!end || *end != '\0') {
        return fallback;
    }
    return value;
}

bool parseHexByte(const std::string& text, uint8_t& value) {
    if (text.size() != 2) {
        return false;
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 16);
    if (!end || *end != '\0' || parsed > 0xffUL) {
        return false;
    }
    value = static_cast<uint8_t>(parsed);
    return true;
}

bool parseMac(const std::string& text, std::array<uint8_t, 6>& mac) {
    size_t start = 0;
    for (size_t index = 0; index < mac.size(); ++index) {
        const size_t end = text.find(index == mac.size() - 1 ? '\0' : ':', start);
        const size_t partEnd = index == mac.size() - 1 ? text.size() : end;
        if (partEnd == std::string::npos || partEnd <= start) {
            return false;
        }
        const std::string part = text.substr(start, partEnd - start);
        uint8_t byte = 0;
        if (!parseHexByte(part, byte)) {
            return false;
        }
        mac[index] = byte;
        start = partEnd + 1;
    }
    return start == text.size() + 1;
}

void applyStableSlavePreset(Options& options) {
    options.profile = "s1608-slave";
    options.audioSampleRate = 44100;
    options.haveSampleRate = true;
    options.packetsPerSecond = 44100 / kReacSamplesPerPacket;
    options.havePacketsPerSecond = false;
    options.slaveStandby = true;
    options.slaveStandbyChunkMs = 5;
    options.slaveHandshake = true;
    options.slavePerGronState = true;
    options.slaveLocalCounter = true;
    options.slaveHandshakeContinuousCounter = true;
    options.slaveTriggerResponder = true;
    options.syncCounter = true;
    options.haveSyncSourceMac = true;
    std::copy(std::begin(kObservedMixerMac), std::end(kObservedMixerMac), options.syncSourceMac.begin());
    options.streamChunkMs = 250;
    options.clockFollow = true;
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

std::string normalizedGuid(std::string value) {
    value.erase(std::remove(value.begin(), value.end(), '{'), value.end());
    value.erase(std::remove(value.begin(), value.end(), '}'), value.end());
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool adapterMacFromNpcapName(const std::string& deviceName, std::array<uint8_t, 6>& mac) {
    constexpr char kNpcapPrefix[] = "\\Device\\NPF_";
    std::string guidText = deviceName;
    if (guidText.rfind(kNpcapPrefix, 0) == 0) {
        guidText = guidText.substr(sizeof(kNpcapPrefix) - 1);
    }
    const std::string targetGuid = normalizedGuid(guidText);
    if (targetGuid.empty()) {
        return false;
    }

    ULONG bufferBytes = 0;
    ULONG result = GetAdaptersAddresses(0, 0, nullptr, nullptr, &bufferBytes);
    if (result != ERROR_BUFFER_OVERFLOW || bufferBytes == 0) {
        return false;
    }

    std::vector<uint8_t> buffer(bufferBytes, 0);
    IP_ADAPTER_ADDRESSES* adapters = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    result = GetAdaptersAddresses(0, 0, nullptr, adapters, &bufferBytes);
    if (result != NO_ERROR) {
        return false;
    }

    for (IP_ADAPTER_ADDRESSES* adapter = adapters; adapter; adapter = adapter->Next) {
        if (!adapter->AdapterName) {
            continue;
        }
        if (normalizedGuid(adapter->AdapterName) == targetGuid && adapter->PhysicalAddressLength >= mac.size()) {
            std::copy(adapter->PhysicalAddress, adapter->PhysicalAddress + mac.size(), mac.begin());
            return true;
        }
    }
    return false;
}

bool macEquals(const uint8_t* left, const uint8_t* right) noexcept {
    return std::memcmp(left, right, 6) == 0;
}

void patchPacketCounters(std::vector<uint8_t>& packets, size_t packetBytes, uint16_t startCounter) {
    if (packetBytes < kReacEthernetHeaderBytes + 2 || packetBytes == 0) {
        return;
    }
    const size_t packetCount = packets.size() / packetBytes;
    uint16_t counter = startCounter;
    for (size_t index = 0; index < packetCount; ++index) {
        uint8_t* packet = packets.data() + index * packetBytes;
        packet[14] = static_cast<uint8_t>(counter & 0xff);
        packet[15] = static_cast<uint8_t>((counter >> 8) & 0xff);
        ++counter;
    }
}

void patchPacketDestination(std::vector<uint8_t>& packets, size_t packetBytes, const uint8_t* destinationMac) {
    if (!destinationMac || packetBytes < kReacEthernetHeaderBytes || packetBytes == 0) {
        return;
    }
    const size_t packetCount = packets.size() / packetBytes;
    for (size_t index = 0; index < packetCount; ++index) {
        std::memcpy(packets.data() + index * packetBytes, destinationMac, 6);
    }
}

void patchPacketHeaderData(std::vector<uint8_t>& packets, size_t packetBytes, const uint8_t* headerData) {
    if (!headerData || packetBytes < kReacPayloadOffset || packetBytes == 0) {
        return;
    }
    const size_t packetCount = packets.size() / packetBytes;
    for (size_t index = 0; index < packetCount; ++index) {
        uint8_t* packet = packets.data() + index * packetBytes;
        if (packet[16] == 0 && packet[17] == 0) {
            std::memcpy(packet + 18, headerData, 32);
        }
    }
}

std::string makeMasterOnlyReacFilter(const Options& options, const uint8_t* ownSourceMac) {
    std::string filter = "ether proto 0x8819";
    if (options.haveSyncSourceMac) {
        filter += " and ether src ";
        filter += formatMac(options.syncSourceMac.data());
    } else if (ownSourceMac) {
        filter += " and not ether src ";
        filter += formatMac(ownSourceMac);
    }
    return filter;
}

bool waitForClockCounter(
    const std::string& deviceName,
    const uint8_t* ownSourceMac,
    const Options& options,
    CounterSyncResult& result,
    std::string& errorMessage) {
    result = {};

    PcapCapture capture;
    if (!capture.available()) {
        errorMessage = capture.lastError();
        return false;
    }
    if (!capture.open(deviceName, makeMasterOnlyReacFilter(options, ownSourceMac))) {
        errorMessage = capture.lastError();
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(options.syncWaitMs);
    while (!gStop.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline && !result.found) {
        const bool ok = capture.captureOnce(50, [&](const CapturedPacket& packet) {
            if (!packet.data || packet.size < kReacEthernetHeaderBytes + 4) {
                return true;
            }
            if (packet.data[12] != 0x88 || packet.data[13] != 0x19) {
                return true;
            }
            const uint8_t* sourceMac = packet.data + 6;
            if (ownSourceMac && macEquals(sourceMac, ownSourceMac)) {
                return true;
            }
            if (options.haveSyncSourceMac && !macEquals(sourceMac, options.syncSourceMac.data())) {
                return true;
            }

            std::copy(sourceMac, sourceMac + 6, result.sourceMac.begin());
            result.counter = static_cast<uint16_t>(packet.data[14] | (static_cast<uint16_t>(packet.data[15]) << 8));
            result.observedAt = std::chrono::steady_clock::now();
            result.found = true;
            return true;
        });
        if (!ok) {
            errorMessage = capture.lastError();
            return false;
        }
    }

    if (!result.found) {
        errorMessage = "No REAC clock packet was seen before the sync timeout";
        return false;
    }
    return true;
}

void printDevices(const std::vector<CaptureDeviceInfo>& devices) {
    for (size_t i = 0; i < devices.size(); ++i) {
        std::printf("%zu. %s\n   %s\n", i + 1, devices[i].description.c_str(), devices[i].name.c_str());
    }
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
        } else if (arg == "--profile" && i + 1 < argc) {
            options.profile = argv[++i];
        } else if (arg == "--stable-slave") {
            applyStableSlavePreset(options);
        } else if (arg == "--channels" && i + 1 < argc) {
            options.channels = parseUInt32(argv[++i], 0);
        } else if (arg == "--seconds" && i + 1 < argc) {
            options.seconds = parseInt(argv[++i], kDefaultSeconds);
        } else if (arg == "--sample-rate" && i + 1 < argc) {
            options.audioSampleRate = parseUInt32(argv[++i], kReacSampleRate);
            options.haveSampleRate = true;
        } else if (arg == "--pps" && i + 1 < argc) {
            options.packetsPerSecond = parseUInt32(argv[++i], kDefaultPacketsPerSecond);
            options.havePacketsPerSecond = true;
        } else if (arg == "--queue-pps" && i + 1 < argc) {
            options.queuePacketsPerSecond = parseUInt32(argv[++i], 0);
        } else if (arg == "--counter" && i + 1 < argc) {
            options.counter = static_cast<uint16_t>(parseUInt32(argv[++i], 0) & 0xffffU);
        } else if (arg == "--sync-counter") {
            options.syncCounter = true;
        } else if (arg == "--sync-ms" && i + 1 < argc) {
            options.syncWaitMs = parseUInt32(argv[++i], 2000);
        } else if (arg == "--sync-lead" && i + 1 < argc) {
            options.syncLeadPackets = parseUInt32(argv[++i], 64);
        } else if (arg == "--sync-source-mac" && i + 1 < argc) {
            if (!parseMac(argv[++i], options.syncSourceMac)) {
                std::fprintf(stderr, "Invalid sync source MAC format\n");
                return false;
            }
            options.haveSyncSourceMac = true;
        } else if (arg == "--source-mac" && i + 1 < argc) {
            if (!parseMac(argv[++i], options.sourceMac)) {
                std::fprintf(stderr, "Invalid source MAC format\n");
                return false;
            }
            options.haveSourceMac = true;
        } else if (arg == "--dest-mac" && i + 1 < argc) {
            if (!parseMac(argv[++i], options.destinationMac)) {
                std::fprintf(stderr, "Invalid destination MAC format\n");
                return false;
            }
            options.haveDestinationMac = true;
        } else if (arg == "--header-repeat" && i + 1 < argc) {
            if (!parseHexByte(argv[++i], options.headerRepeat)) {
                std::fprintf(stderr, "Invalid header repeat byte\n");
                return false;
            }
            options.haveHeaderRepeat = true;
        } else if (arg == "--tone-channel" && i + 1 < argc) {
            options.toneChannel = parseInt(argv[++i], 0);
        } else if (arg == "--tone-hz" && i + 1 < argc) {
            options.toneHz = parseDouble(argv[++i], 1000.0);
        } else if (arg == "--level-db" && i + 1 < argc) {
            options.toneLevelDb = parseDouble(argv[++i], kDefaultToneLevelDb);
        } else if (arg == "--send") {
            options.send = true;
        } else if (arg == "--force") {
            options.force = true;
        } else if (arg == "--clocked") {
            options.clocked = true;
        } else if (arg == "--clock-follow") {
            options.clockFollow = true;
        } else if (arg == "--clock-offset" && i + 1 < argc) {
            options.clockOffsetPackets = parseUInt32(argv[++i], 0) & 0xffffU;
        } else if (arg == "--slave-handshake") {
            options.slaveHandshake = true;
        } else if (arg == "--handshake-continuous-counter") {
            options.slaveHandshakeContinuousCounter = true;
        } else if (arg == "--slave-local-counter") {
            options.slaveLocalCounter = true;
        } else if (arg == "--slave-per-gron-state") {
            options.slavePerGronState = true;
        } else if (arg == "--handshake-ms" && i + 1 < argc) {
            options.slaveHandshakeWaitMs = parseUInt32(argv[++i], 3000);
        } else if (arg == "--slave-preroll-ms" && i + 1 < argc) {
            options.slavePrerollMs = parseUInt32(argv[++i], 0);
        } else if (arg == "--slave-standby") {
            options.slaveStandby = true;
        } else if (arg == "--standby-chunk-ms" && i + 1 < argc) {
            options.slaveStandbyChunkMs = parseUInt32(argv[++i], 250);
        } else if (arg == "--slave-standby-responder") {
            options.slaveStandbyResponder = true;
        } else if (arg == "--slave-keepalive-ms" && i + 1 < argc) {
            options.slaveKeepaliveMs = parseUInt32(argv[++i], 0);
        } else if (arg == "--slave-trigger-responder") {
            options.slaveTriggerResponder = true;
        } else if (arg == "--trigger-debounce-ms" && i + 1 < argc) {
            options.slaveTriggerDebounceMs = parseUInt32(argv[++i], 200);
        } else if (arg == "--stream-chunk-ms" && i + 1 < argc) {
            options.streamChunkMs = parseUInt32(argv[++i], 0);
        } else {
            std::fprintf(stderr, "Unknown or incomplete argument: %s\n", arg.c_str());
            printUsage();
            return false;
        }
    }

    if (options.profile == "s1608") {
        if (options.channels == 0) {
            options.channels = 8;
        }
    } else if (options.profile == "s1608-slave") {
        if (options.channels == 0) {
            options.channels = 8;
        }
        if (!options.haveSourceMac) {
            std::copy(std::begin(kObservedS1608Mac), std::end(kObservedS1608Mac), options.sourceMac.begin());
            options.haveSourceMac = true;
        }
    } else if (options.profile == "mixer8") {
        if (options.channels == 0) {
            options.channels = 8;
        }
        if (!options.haveSourceMac) {
            std::copy(std::begin(kObservedMixerMac), std::end(kObservedMixerMac), options.sourceMac.begin());
            options.haveSourceMac = true;
        }
        if (!options.haveHeaderRepeat) {
            options.headerRepeat = 0xe7;
            options.haveHeaderRepeat = true;
        }
    } else if (options.profile == "mixer40") {
        if (options.channels == 0) {
            options.channels = 40;
        }
    } else {
        std::fprintf(stderr, "Unknown profile: %s\n", options.profile.c_str());
        return false;
    }

    if (options.channels == 0 || options.channels > kReacChannelCount || (options.channels & 1U) != 0) {
        std::fprintf(stderr, "Channel count must be an even number from 2 to 40\n");
        return false;
    }
    if (options.seconds <= 0) {
        std::fprintf(stderr, "Seconds must be positive\n");
        return false;
    }
    if (options.haveSampleRate) {
        if (options.audioSampleRate != 44100 && options.audioSampleRate != 48000) {
            std::fprintf(stderr, "Sample rate must be 44100 or 48000 for this TX probe\n");
            return false;
        }
        if ((options.audioSampleRate % kReacSamplesPerPacket) != 0) {
            std::fprintf(stderr, "Sample rate must be divisible by %u REAC samples/packet\n", kReacSamplesPerPacket);
            return false;
        }
        const uint32_t sampleRatePacketsPerSecond = options.audioSampleRate / kReacSamplesPerPacket;
        if (options.havePacketsPerSecond && options.packetsPerSecond != sampleRatePacketsPerSecond) {
            std::fprintf(
                stderr,
                "--sample-rate and --pps disagree (%u Hz implies %u pps)\n",
                options.audioSampleRate,
                sampleRatePacketsPerSecond);
            return false;
        }
        options.packetsPerSecond = sampleRatePacketsPerSecond;
    } else if (options.havePacketsPerSecond) {
        options.audioSampleRate = options.packetsPerSecond * kReacSamplesPerPacket;
    }
    if (options.packetsPerSecond == 0 || options.packetsPerSecond > kDefaultPacketsPerSecond) {
        std::fprintf(stderr, "Packet rate must be between 1 and 4000 packets/second\n");
        return false;
    }
    if (options.queuePacketsPerSecond == 0) {
        options.queuePacketsPerSecond = options.packetsPerSecond;
    }
    if (options.queuePacketsPerSecond == 0 || options.queuePacketsPerSecond > kMaxQueuePacketsPerSecond) {
        std::fprintf(stderr, "Timed-queue packet rate must be between 1 and %u packets/second\n", kMaxQueuePacketsPerSecond);
        return false;
    }
    if (options.syncWaitMs == 0) {
        std::fprintf(stderr, "Sync wait must be positive\n");
        return false;
    }
    if (options.slaveHandshakeWaitMs == 0) {
        std::fprintf(stderr, "Handshake wait must be positive\n");
        return false;
    }
    if (options.slaveKeepaliveMs > 0 && !options.slaveHandshake) {
        std::fprintf(stderr, "--slave-keepalive-ms requires --slave-handshake\n");
        return false;
    }
    if (options.slavePrerollMs > 0 && !options.slaveHandshake) {
        std::fprintf(stderr, "--slave-preroll-ms requires --slave-handshake\n");
        return false;
    }
    if (options.slaveStandby && !options.slaveHandshake) {
        std::fprintf(stderr, "--slave-standby requires --slave-handshake\n");
        return false;
    }
    if (options.slaveStandbyResponder && !options.slaveStandby) {
        std::fprintf(stderr, "--slave-standby-responder requires --slave-standby\n");
        return false;
    }
    if (options.slaveHandshakeContinuousCounter && !options.slaveHandshake) {
        std::fprintf(stderr, "--handshake-continuous-counter requires --slave-handshake\n");
        return false;
    }
    if (options.slaveLocalCounter && !options.slaveHandshake) {
        std::fprintf(stderr, "--slave-local-counter requires --slave-handshake\n");
        return false;
    }
    if (options.slavePerGronState && !options.slaveHandshake) {
        std::fprintf(stderr, "--slave-per-gron-state requires --slave-handshake\n");
        return false;
    }
    if (options.slaveStandbyChunkMs == 0) {
        std::fprintf(stderr, "Standby chunk duration must be positive\n");
        return false;
    }
    if (options.slaveTriggerResponder && !options.slaveHandshake) {
        std::fprintf(stderr, "--slave-trigger-responder requires --slave-handshake\n");
        return false;
    }
    if (options.slaveTriggerDebounceMs == 0) {
        std::fprintf(stderr, "Trigger debounce must be positive\n");
        return false;
    }
    if (options.streamChunkMs > 0 && options.clocked) {
        std::fprintf(stderr, "--stream-chunk-ms cannot be combined with --clocked\n");
        return false;
    }
    if (options.clockFollow && options.streamChunkMs == 0) {
        std::fprintf(stderr, "--clock-follow requires --stream-chunk-ms\n");
        return false;
    }
    if (options.clockFollow && options.clocked) {
        std::fprintf(stderr, "Use either --clock-follow or --clocked, not both\n");
        return false;
    }
    if (options.streamChunkMs > 0 && options.slaveKeepaliveMs > 0) {
        std::fprintf(stderr, "--stream-chunk-ms does not yet support --slave-keepalive-ms\n");
        return false;
    }
    if (options.syncCounter && options.clocked) {
        std::fprintf(stderr, "Use either --sync-counter or --clocked, not both\n");
        return false;
    }
    if (options.toneChannel < 0 || options.toneChannel > static_cast<int>(options.channels)) {
        std::fprintf(stderr, "Tone channel must be in the selected channel range\n");
        return false;
    }
    if (options.toneHz <= 0.0 || options.toneHz > 20000.0) {
        std::fprintf(stderr, "Tone frequency must be between 1 and 20000 Hz\n");
        return false;
    }
    if (options.toneLevelDb > -18.0) {
        std::fprintf(stderr, "Refusing tone levels above -18 dBFS\n");
        return false;
    }
    if (options.send && options.seconds > kMaxSendSecondsWithoutForce && !options.force) {
        std::fprintf(stderr, "Sending more than %d seconds requires --force\n", kMaxSendSecondsWithoutForce);
        return false;
    }

    return true;
}

void fillHeaderPair(ReacPacketTemplate& packetTemplate, uint8_t first, uint8_t second) {
    for (size_t i = 0; i + 1 < sizeof(packetTemplate.headerData); i += 2) {
        packetTemplate.headerData[i] = first;
        packetTemplate.headerData[i + 1] = second;
    }
}

void fillHeader(ReacPacketTemplate& packetTemplate, const Options& options) {
    std::fill(std::begin(packetTemplate.headerData), std::end(packetTemplate.headerData), 0);
    if (options.haveHeaderRepeat) {
        fillHeaderPair(packetTemplate, 0x00, options.headerRepeat);
    }
}

void fillAudio(std::vector<float>& audio, const Options& options, double audioSampleRate, double& phase) {
    std::fill(audio.begin(), audio.end(), 0.0f);
    if (options.toneChannel <= 0) {
        return;
    }

    const size_t channel = static_cast<size_t>(options.toneChannel - 1);
    const double amplitude = std::pow(10.0, options.toneLevelDb / 20.0);
    const double phaseStep = 2.0 * kPi * options.toneHz / audioSampleRate;
    for (uint32_t frame = 0; frame < kReacSamplesPerPacket; ++frame) {
        audio[static_cast<size_t>(frame) * options.channels + channel] = static_cast<float>(std::sin(phase) * amplitude);
        phase += phaseStep;
        if (phase >= 2.0 * kPi) {
            phase -= 2.0 * kPi;
        }
    }
}

bool sendSlavePreroll(
    PcapSender& sender,
    const ReacPacketTemplate& packetTemplate,
    const Options& options,
    size_t packetBytes) {
    if (options.slavePrerollMs == 0) {
        return true;
    }

    const uint64_t packetCount = std::max<uint64_t>(1, static_cast<uint64_t>(std::llround(
        static_cast<double>(options.slavePrerollMs) * static_cast<double>(options.queuePacketsPerSecond) / 1000.0)));
    const uint64_t totalBytes = packetCount * packetBytes;
    if (totalBytes > static_cast<uint64_t>(SIZE_MAX)) {
        std::fprintf(stderr, "Slave preroll queue is too large for this process\n");
        return false;
    }

    ReacPacketTemplate prerollTemplate = packetTemplate;
    std::copy(std::begin(kBroadcastMac), std::end(kBroadcastMac), prerollTemplate.destinationMac);
    std::fill(std::begin(prerollTemplate.headerData), std::end(prerollTemplate.headerData), 0);
    prerollTemplate.packetType = 0;

    ReacPacketEncoder prerollEncoder;
    prerollEncoder.setTemplate(prerollTemplate);

    std::vector<uint8_t> queuedPackets(static_cast<size_t>(totalBytes), 0);
    std::vector<float> silence(static_cast<size_t>(kReacSamplesPerPacket) * options.channels, 0.0f);
    uint16_t counter = options.counter;
    for (uint64_t packetIndex = 0; packetIndex < packetCount; ++packetIndex) {
        size_t encodedBytes = 0;
        const ReacEncodeStatus status = prerollEncoder.encodeInterleavedFloat(
            silence.data(),
            options.channels,
            kReacSamplesPerPacket,
            counter++,
            queuedPackets.data() + static_cast<size_t>(packetIndex) * packetBytes,
            packetBytes,
            &encodedBytes);
        if (status != ReacEncodeStatus::ok || encodedBytes != packetBytes) {
            std::fprintf(stderr, "Slave preroll encode failed: %s\n", ReacPacketEncoder::toString(status));
            return false;
        }
    }

    std::printf(
        "Slave preroll: sending %ums standalone broadcast (%llu packets, %.1f MB)\n",
        options.slavePrerollMs,
        static_cast<unsigned long long>(packetCount),
        static_cast<double>(totalBytes) / (1024.0 * 1024.0));
    const auto started = std::chrono::steady_clock::now();
    if (!sender.sendTimedQueue(queuedPackets, static_cast<uint32_t>(packetBytes), options.queuePacketsPerSecond)) {
        std::fprintf(stderr, "Slave preroll transmit failed: %s\n", sender.lastError().c_str());
        return false;
    }
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf(
        "Slave preroll: done elapsed=%.3fs rate=%.1f packets/s\n",
        elapsed,
        elapsed > 0.0 ? static_cast<double>(packetCount) / elapsed : 0.0);
    return true;
}

bool buildStandaloneBroadcastQueue(
    const ReacPacketTemplate& packetTemplate,
    const Options& options,
    size_t packetBytes,
    uint64_t packetCount,
    std::vector<uint8_t>& queuedPackets) {
    const uint64_t totalBytes = packetCount * packetBytes;
    if (totalBytes > static_cast<uint64_t>(SIZE_MAX)) {
        std::fprintf(stderr, "Standalone broadcast queue is too large for this process\n");
        return false;
    }

    ReacPacketTemplate standbyTemplate = packetTemplate;
    std::copy(std::begin(kBroadcastMac), std::end(kBroadcastMac), standbyTemplate.destinationMac);
    std::fill(std::begin(standbyTemplate.headerData), std::end(standbyTemplate.headerData), 0);
    standbyTemplate.packetType = 0;

    ReacPacketEncoder standbyEncoder;
    standbyEncoder.setTemplate(standbyTemplate);

    queuedPackets.assign(static_cast<size_t>(totalBytes), 0);
    std::vector<float> silence(static_cast<size_t>(kReacSamplesPerPacket) * options.channels, 0.0f);
    for (uint64_t packetIndex = 0; packetIndex < packetCount; ++packetIndex) {
        size_t encodedBytes = 0;
        const ReacEncodeStatus status = standbyEncoder.encodeInterleavedFloat(
            silence.data(),
            options.channels,
            kReacSamplesPerPacket,
            static_cast<uint16_t>(packetIndex & 0xffffU),
            queuedPackets.data() + static_cast<size_t>(packetIndex) * packetBytes,
            packetBytes,
            &encodedBytes);
        if (status != ReacEncodeStatus::ok || encodedBytes != packetBytes) {
            std::fprintf(stderr, "Standalone broadcast encode failed: %s\n", ReacPacketEncoder::toString(status));
            return false;
        }
    }
    return true;
}

bool buildTimedAudioQueueRange(
    ReacPacketEncoder& encoder,
    const Options& options,
    size_t packetCapacity,
    uint64_t packetCount,
    uint16_t startCounter,
    double audioSampleRate,
    double& phase,
    bool printSize,
    std::vector<uint8_t>& queuedPackets) {
    const uint64_t totalBytes = packetCount * packetCapacity;
    if (totalBytes > static_cast<uint64_t>(SIZE_MAX)) {
        std::fprintf(stderr, "Packet queue is too large for this process\n");
        return false;
    }

    if (printSize) {
        std::printf("Building timed send queue: %.1f MB\n", static_cast<double>(totalBytes) / (1024.0 * 1024.0));
    }
    queuedPackets.assign(static_cast<size_t>(totalBytes), 0);
    std::vector<float> audio(static_cast<size_t>(kReacSamplesPerPacket) * options.channels, 0.0f);
    uint16_t counter = startCounter;
    for (uint64_t packetIndex = 0; packetIndex < packetCount; ++packetIndex) {
        fillAudio(audio, options, audioSampleRate, phase);

        size_t packetBytes = 0;
        const ReacEncodeStatus status = encoder.encodeInterleavedFloat(
            audio.data(),
            options.channels,
            kReacSamplesPerPacket,
            counter++,
            queuedPackets.data() + static_cast<size_t>(packetIndex) * packetCapacity,
            packetCapacity,
            &packetBytes);
        if (status != ReacEncodeStatus::ok) {
            std::fprintf(stderr, "Encode failed: %s\n", ReacPacketEncoder::toString(status));
            return false;
        }
        if (packetBytes != packetCapacity) {
            std::fprintf(stderr, "Unexpected packet size: %zu\n", packetBytes);
            return false;
        }
    }
    return true;
}

bool buildTimedAudioQueue(
    ReacPacketEncoder& encoder,
    const Options& options,
    size_t packetCapacity,
    uint64_t totalPackets,
    std::vector<uint8_t>& queuedPackets) {
    double phase = 0.0;
    return buildTimedAudioQueueRange(
        encoder,
        options,
        packetCapacity,
        totalPackets,
        options.counter,
        static_cast<double>(options.audioSampleRate),
        phase,
        true,
        queuedPackets);
}

bool matchesClockSource(const CapturedPacket& packet, const uint8_t* ownSourceMac, const Options& options, uint16_t& counter) {
    if (!packet.data || packet.size < kReacEthernetHeaderBytes + 4) {
        return false;
    }
    if (packet.data[12] != 0x88 || packet.data[13] != 0x19) {
        return false;
    }

    const uint8_t* sourceMac = packet.data + 6;
    if (ownSourceMac && macEquals(sourceMac, ownSourceMac)) {
        return false;
    }
    if (options.haveSyncSourceMac && !macEquals(sourceMac, options.syncSourceMac.data())) {
        return false;
    }

    counter = static_cast<uint16_t>(packet.data[14] | (static_cast<uint16_t>(packet.data[15]) << 8));
    return true;
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

struct SlaveHandshakeTracker {
    bool sawPerGronAddressMarker = false;
    bool gotMasterMacInfo = false;
    std::array<uint8_t, 6> masterMac{};
};

bool packetControlContainsMac(const CapturedPacket& packet, const uint8_t* mac) noexcept {
    if (!packet.data || !mac || packet.size < kReacPayloadOffset) {
        return false;
    }
    const uint32_t end = std::min<uint32_t>(packet.size, kReacPayloadOffset);
    for (uint32_t offset = 18; offset + 6 <= end; ++offset) {
        if (std::memcmp(packet.data + offset, mac, 6) == 0) {
            return true;
        }
    }
    return false;
}

void updateSlaveHandshakeTracker(SlaveHandshakeTracker& tracker, const CapturedPacket& packet) {
    if (!isEacdControlType(packet, kControlTypeOne) || packet.size < kReacPayloadOffset) {
        return;
    }

    const uint8_t* control = packet.data + 18;
    if (!tracker.sawPerGronAddressMarker &&
        packet.size > 18 + 30 &&
        control[29] == 0xc0 &&
        control[30] == 0xa8) {
        tracker.sawPerGronAddressMarker = true;
        std::puts("Slave handshake: observed Per-Gron address marker.");
    }

    if (!tracker.gotMasterMacInfo &&
        packet.size > 18 + 22 &&
        control[5] == 0x01 &&
        control[6] == 0x01 &&
        std::memcmp(control + 7, control + 17, 6) == 0) {
        std::copy(control + 7, control + 13, tracker.masterMac.begin());
        tracker.gotMasterMacInfo = true;
        std::printf(
            "Slave handshake: observed Per-Gron master MAC info %s.\n",
            formatMac(tracker.masterMac.data()).c_str());
        return;
    }

    if (!tracker.gotMasterMacInfo && packetControlContainsMac(packet, packet.data + 6)) {
        std::copy(packet.data + 6, packet.data + 12, tracker.masterMac.begin());
        tracker.gotMasterMacInfo = true;
        std::printf(
            "Slave handshake: observed master MAC in control phase %s.\n",
            formatMac(tracker.masterMac.data()).c_str());
    }
}

struct SlaveInitialTriggerResponder {
    std::atomic<bool> stop{false};
    std::atomic<bool> sent{false};
    std::atomic<bool> failed{false};
    std::string error;
    std::thread worker;
    std::array<uint8_t, 6> masterMac{};
    std::array<uint8_t, 2> headerPair{};
    CounterSyncResult sync{};
    uint16_t masterTriggerCounter = 0;
    uint16_t announceStartCounter = 0;
};

void startSlaveInitialTriggerResponder(
    SlaveInitialTriggerResponder& responder,
    const std::string& deviceName,
    const ReacPacketTemplate& packetTemplate,
    const Options& options,
    const std::atomic<uint32_t>* localNextCounter) {
    responder.stop.store(false, std::memory_order_release);
    responder.sent.store(false, std::memory_order_release);
    responder.failed.store(false, std::memory_order_release);
    responder.error.clear();
    responder.masterMac = {};
    responder.headerPair = {};
    responder.sync = {};
    responder.masterTriggerCounter = 0;
    responder.announceStartCounter = 0;

    responder.worker = std::thread([&responder, deviceName, packetTemplate, options, localNextCounter]() {
        PcapCapture triggerCapture;
        if (!triggerCapture.available()) {
            responder.error = triggerCapture.lastError();
            responder.failed.store(true, std::memory_order_release);
            return;
        }
        if (!triggerCapture.open(deviceName, makeMasterOnlyReacFilter(options, packetTemplate.sourceMac))) {
            responder.error = std::string("Could not open standby trigger capture: ") + triggerCapture.lastError();
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
            responder.error = std::string("Could not open standby trigger sender: ") + triggerSender.lastError();
            responder.failed.store(true, std::memory_order_release);
            return;
        }

        const size_t packetBytes = ReacPacketEncoder::packetBytesForChannels(8);
        std::vector<uint8_t> packet(packetBytes, 0);
        SlaveHandshakeTracker tracker{};
        while (!responder.stop.load(std::memory_order_acquire) && !gStop.load(std::memory_order_acquire)) {
            const bool ok = triggerCapture.captureOnce(50, [&](const CapturedPacket& captured) {
                uint16_t counter = 0;
                if (!matchesClockSource(captured, packetTemplate.sourceMac, options, counter)) {
                    return true;
                }
                updateSlaveHandshakeTracker(tracker, captured);
                if (options.slavePerGronState && !tracker.gotMasterMacInfo) {
                    return true;
                }
                if (!isEacdControlType(captured, kSlaveTriggerControlType)) {
                    return true;
                }
                if (responder.sent.load(std::memory_order_acquire)) {
                    return true;
                }

                std::array<uint8_t, 6> masterMac{};
                if (tracker.gotMasterMacInfo) {
                    masterMac = tracker.masterMac;
                } else {
                    std::copy(captured.data + 6, captured.data + 12, masterMac.begin());
                }

                const uint16_t announceStartCounter = options.slaveLocalCounter && localNextCounter
                    ? static_cast<uint16_t>(localNextCounter->load(std::memory_order_acquire) & 0xffffU)
                    : static_cast<uint16_t>(counter + 1);
                const uint16_t syncBaseCounter = static_cast<uint16_t>(announceStartCounter - 1);
                std::array<uint8_t, 2> lastControlTail{};
                for (size_t sequenceIndex = 0; sequenceIndex < kSlaveAnnouncePacketCount; ++sequenceIndex) {
                    buildSlaveAnnouncePacket(
                        packetTemplate,
                        masterMac.data(),
                        static_cast<uint16_t>(announceStartCounter + sequenceIndex),
                        sequenceIndex,
                        packet.data(),
                        packet.size(),
                        &lastControlTail);
                    if (!triggerSender.send(packet.data(), packet.size())) {
                        responder.error = std::string("Standby trigger response failed: ") + triggerSender.lastError();
                        responder.failed.store(true, std::memory_order_release);
                        responder.stop.store(true, std::memory_order_release);
                        return true;
                    }
                }

                responder.masterMac = masterMac;
                responder.headerPair = lastControlTail;
                responder.masterTriggerCounter = counter;
                responder.announceStartCounter = announceStartCounter;
                responder.sync.found = true;
                responder.sync.counter = syncBaseCounter;
                responder.sync.sourceMac = masterMac;
                responder.sync.observedAt = std::chrono::steady_clock::now();
                responder.sent.store(true, std::memory_order_release);
                responder.stop.store(true, std::memory_order_release);
                if (options.slaveLocalCounter) {
                    std::printf(
                        "Slave standby responder: answered initial trigger masterCounter=%u localAnnounceCounter=%u\n",
                        counter,
                        announceStartCounter);
                } else {
                    std::printf(
                        "Slave standby responder: answered initial trigger counter=%u\n",
                        counter);
                }
                return true;
            });

            if (!ok) {
                responder.error = std::string("Standby trigger capture failed: ") + triggerCapture.lastError();
                responder.failed.store(true, std::memory_order_release);
                return;
            }
        }
    });
}

void stopSlaveInitialTriggerResponder(SlaveInitialTriggerResponder& responder) {
    responder.stop.store(true, std::memory_order_release);
    if (responder.worker.joinable()) {
        responder.worker.join();
    }
}

bool runSlaveHandshake(
    const std::string& deviceName,
    PcapSender& sender,
    const ReacPacketTemplate& packetTemplate,
    const Options& options,
    std::array<uint8_t, 6>* observedMasterMac,
    std::array<uint8_t, 2>* fillerHeaderPair,
    CounterSyncResult* triggerSync) {
    PcapCapture handshakeCapture;
    if (!handshakeCapture.available()) {
        std::fprintf(stderr, "%s\n", handshakeCapture.lastError().c_str());
        return false;
    }
    if (triggerSync) {
        *triggerSync = {};
    }

    if (!handshakeCapture.open(deviceName, makeMasterOnlyReacFilter(options, packetTemplate.sourceMac))) {
        std::fprintf(stderr, "Could not open handshake capture: %s\n", handshakeCapture.lastError().c_str());
        return false;
    }

    const size_t packetBytes = ReacPacketEncoder::packetBytesForChannels(8);
    std::vector<uint8_t> packet(packetBytes, 0);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(options.slaveHandshakeWaitMs);
    bool sent = false;
    bool failed = false;
    std::array<uint8_t, 6> masterMac{};
    std::array<uint8_t, 2> lastControlTail{};
    SlaveHandshakeTracker tracker{};
    uint16_t observedCounter = 0;
    uint16_t masterTriggerCounter = 0;
    uint16_t announceStartCounterForLog = 0;
    auto observedAt = std::chrono::steady_clock::time_point{};
    uint16_t standbyCounter = options.counter;
    std::atomic<uint32_t> localNextCounter{static_cast<uint32_t>(standbyCounter)};
    uint64_t standbyChunksSent = 0;
    uint64_t standbyPacketsSent = 0;
    std::vector<uint8_t> standbyPackets;
    uint64_t standbyPacketCount = 0;
    SlaveInitialTriggerResponder standbyResponder{};
    bool standbyResponderStarted = false;
    const auto stopStandbyResponder = [&]() {
        if (standbyResponderStarted) {
            stopSlaveInitialTriggerResponder(standbyResponder);
            standbyResponderStarted = false;
        }
    };

    if (options.slaveStandby) {
        standbyPacketCount = std::max<uint64_t>(1, static_cast<uint64_t>(std::llround(
            static_cast<double>(options.slaveStandbyChunkMs) * static_cast<double>(options.queuePacketsPerSecond) / 1000.0)));
        if (!buildStandaloneBroadcastQueue(packetTemplate, options, packetBytes, standbyPacketCount, standbyPackets)) {
            return false;
        }
        std::printf(
            "Slave standby: broadcasting until mixer trigger (%ums chunks, %llu packets/chunk). Press Ctrl+C to stop.\n",
            options.slaveStandbyChunkMs,
            static_cast<unsigned long long>(standbyPacketCount));
        if (options.slaveStandbyResponder) {
            std::puts("Slave standby responder: watching for the first mixer trigger in parallel.");
            startSlaveInitialTriggerResponder(standbyResponder, deviceName, packetTemplate, options, &localNextCounter);
            standbyResponderStarted = true;
        }
    }

    const auto handleCapturedTrigger = [&](const CapturedPacket& captured) -> bool {
        uint16_t counter = 0;
        if (!matchesClockSource(captured, packetTemplate.sourceMac, options, counter)) {
            return true;
        }
        updateSlaveHandshakeTracker(tracker, captured);
        if (options.slavePerGronState && !tracker.gotMasterMacInfo) {
            return true;
        }
        if (!isEacdControlType(captured, kSlaveTriggerControlType)) {
            return true;
        }

        if (tracker.gotMasterMacInfo) {
            masterMac = tracker.masterMac;
        } else {
            std::copy(captured.data + 6, captured.data + 12, masterMac.begin());
        }
        observedCounter = counter;
        masterTriggerCounter = counter;
        observedAt = std::chrono::steady_clock::now();
        const uint16_t announceStartCounter = options.slaveLocalCounter
            ? standbyCounter
            : static_cast<uint16_t>(observedCounter + 1);
        announceStartCounterForLog = announceStartCounter;
        const uint16_t syncBaseCounter = static_cast<uint16_t>(announceStartCounter - 1);
        for (size_t sequenceIndex = 0; sequenceIndex < kSlaveAnnouncePacketCount; ++sequenceIndex) {
            buildSlaveAnnouncePacket(
                packetTemplate,
                masterMac.data(),
                static_cast<uint16_t>(announceStartCounter + sequenceIndex),
                sequenceIndex,
                packet.data(),
                packet.size(),
                &lastControlTail);
            if (!sender.send(packet.data(), packet.size())) {
                std::fprintf(stderr, "Slave handshake send failed: %s\n", sender.lastError().c_str());
                gStop.store(true, std::memory_order_release);
                failed = true;
                return true;
            }
        }

        observedCounter = syncBaseCounter;
        sent = true;
        return true;
    };

    std::puts("Slave handshake: waiting for mixer EACD trigger.");
    while (!gStop.load(std::memory_order_acquire) && !sent) {
        if (!options.slaveStandby && std::chrono::steady_clock::now() >= deadline) {
            break;
        }

        if (standbyResponderStarted) {
            if (standbyResponder.failed.load(std::memory_order_acquire)) {
                failed = true;
                break;
            }
            if (standbyResponder.sent.load(std::memory_order_acquire)) {
                masterMac = standbyResponder.masterMac;
                lastControlTail = standbyResponder.headerPair;
                observedCounter = standbyResponder.sync.counter;
                masterTriggerCounter = standbyResponder.masterTriggerCounter;
                announceStartCounterForLog = standbyResponder.announceStartCounter;
                observedAt = standbyResponder.sync.observedAt;
                sent = true;
                break;
            }
        } else {
            const bool ok = handshakeCapture.captureOnce(options.slaveStandby ? 0 : 50, handleCapturedTrigger);
            if (!ok) {
                std::fprintf(stderr, "Slave handshake capture failed: %s\n", handshakeCapture.lastError().c_str());
                stopStandbyResponder();
                return false;
            }
        }
        if (failed || sent) {
            break;
        }

        if (options.slaveStandby) {
            if (!standbyResponderStarted && standbyChunksSent > 0) {
                const uint64_t drainAttempts = std::min<uint64_t>(standbyPacketCount + 128U, 4096U);
                for (uint64_t drainIndex = 0; drainIndex < drainAttempts && !sent && !failed; ++drainIndex) {
                    const bool drainOk = handshakeCapture.captureOnce(0, handleCapturedTrigger);
                    if (!drainOk) {
                        std::fprintf(stderr, "Slave handshake capture failed: %s\n", handshakeCapture.lastError().c_str());
                        stopStandbyResponder();
                        return false;
                    }
                }
            }
            if (failed || sent) {
                break;
            }

            patchPacketCounters(standbyPackets, packetBytes, standbyCounter);
            localNextCounter.store(
                static_cast<uint32_t>((static_cast<uint32_t>(standbyCounter) + standbyPacketCount) & 0xffffU),
                std::memory_order_release);
            if (!sender.sendTimedQueue(standbyPackets, static_cast<uint32_t>(packetBytes), options.queuePacketsPerSecond)) {
                std::fprintf(stderr, "Slave standby transmit failed: %s\n", sender.lastError().c_str());
                stopStandbyResponder();
                return false;
            }
            standbyCounter = static_cast<uint16_t>((static_cast<uint32_t>(standbyCounter) + standbyPacketCount) & 0xffffU);
            localNextCounter.store(standbyCounter, std::memory_order_release);
            ++standbyChunksSent;
            standbyPacketsSent += standbyPacketCount;
            if ((standbyChunksSent % 16U) == 0U) {
                std::printf(
                    "Slave standby: still waiting, sent %llu broadcast packets\n",
                    static_cast<unsigned long long>(standbyPacketsSent));
            }
        }
    }

    stopStandbyResponder();
    if (failed) {
        if (!standbyResponder.error.empty()) {
            std::fprintf(stderr, "%s\n", standbyResponder.error.c_str());
        }
        return false;
    }

    if (!sent) {
        std::fprintf(stderr, "Slave handshake did not see a trigger before timeout\n");
        return false;
    }
    if (options.slaveStandby) {
        std::printf(
            "Slave standby: stopped after %llu chunks (%llu broadcast packets)\n",
            static_cast<unsigned long long>(standbyChunksSent),
            static_cast<unsigned long long>(standbyPacketsSent));
    }

    if (options.slaveLocalCounter) {
        std::printf(
            "Slave handshake: sent announce sequence to %s after masterCounter=%u localAnnounceCounter=%u\n",
            formatMac(masterMac.data()).c_str(),
            masterTriggerCounter,
            announceStartCounterForLog);
    } else {
        std::printf(
            "Slave handshake: sent announce sequence to %s after counter=%u\n",
            formatMac(masterMac.data()).c_str(),
            observedCounter);
    }
    if (observedMasterMac) {
        *observedMasterMac = masterMac;
    }
    if (fillerHeaderPair) {
        *fillerHeaderPair = lastControlTail;
    }
    if (triggerSync) {
        triggerSync->found = true;
        triggerSync->counter = observedCounter;
        triggerSync->sourceMac = masterMac;
        triggerSync->observedAt = observedAt;
    }
    return true;
}

struct SlaveTriggerResponder {
    std::atomic<bool> stop{false};
    std::atomic<bool> failed{false};
    std::atomic<uint64_t> triggersSeen{0};
    std::atomic<uint64_t> sequencesSent{0};
    std::string error;
    std::thread worker;
};

void startSlaveTriggerResponder(
    SlaveTriggerResponder& responder,
    const std::string& deviceName,
    const ReacPacketTemplate& packetTemplate,
    const Options& options,
    const std::array<uint8_t, 6>& masterMac) {
    responder.stop.store(false, std::memory_order_release);
    responder.failed.store(false, std::memory_order_release);
    responder.triggersSeen.store(0, std::memory_order_release);
    responder.sequencesSent.store(0, std::memory_order_release);
    responder.error.clear();

    responder.worker = std::thread([&responder, deviceName, packetTemplate, options, masterMac]() {
        PcapCapture triggerCapture;
        if (!triggerCapture.available()) {
            responder.error = triggerCapture.lastError();
            responder.failed.store(true, std::memory_order_release);
            return;
        }
        if (!triggerCapture.open(deviceName, makeMasterOnlyReacFilter(options, packetTemplate.sourceMac))) {
            responder.error = std::string("Could not open trigger capture: ") + triggerCapture.lastError();
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
            responder.error = std::string("Could not open trigger sender: ") + triggerSender.lastError();
            responder.failed.store(true, std::memory_order_release);
            return;
        }

        const size_t packetBytes = ReacPacketEncoder::packetBytesForChannels(8);
        std::vector<uint8_t> packet(packetBytes, 0);
        auto lastResponseAt = std::chrono::steady_clock::time_point{};
        const auto streamStartAt = std::chrono::steady_clock::now();

        while (!responder.stop.load(std::memory_order_acquire) && !gStop.load(std::memory_order_acquire)) {
            const bool ok = triggerCapture.captureOnce(50, [&](const CapturedPacket& captured) {
                uint16_t counter = 0;
                if (!matchesClockSource(captured, packetTemplate.sourceMac, options, counter)) {
                    return true;
                }
                if (!macEquals(captured.data + 6, masterMac.data())) {
                    return true;
                }
                if (!isEacdControlType(captured, kSlaveTriggerControlType)) {
                    return true;
                }

                responder.triggersSeen.fetch_add(1, std::memory_order_relaxed);
                const auto now = std::chrono::steady_clock::now();
                if (lastResponseAt.time_since_epoch().count() != 0) {
                    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastResponseAt).count();
                    if (elapsedMs >= 0 && static_cast<uint32_t>(elapsedMs) < options.slaveTriggerDebounceMs) {
                        return true;
                    }
                }
                lastResponseAt = now;

                uint16_t announceStartCounter = static_cast<uint16_t>(counter + 1);
                if (options.slaveLocalCounter) {
                    const double elapsedSeconds = std::chrono::duration<double>(now - streamStartAt).count();
                    const double nonnegativeElapsedSeconds = elapsedSeconds > 0.0 ? elapsedSeconds : 0.0;
                    const uint32_t elapsedPackets = static_cast<uint32_t>(
                        std::round(nonnegativeElapsedSeconds * options.queuePacketsPerSecond));
                    announceStartCounter = static_cast<uint16_t>(
                        (static_cast<uint32_t>(options.counter) + elapsedPackets + 1U) & 0xffffU);
                }
                for (size_t sequenceIndex = 0; sequenceIndex < kSlaveAnnouncePacketCount; ++sequenceIndex) {
                    buildSlaveAnnouncePacket(
                        packetTemplate,
                        masterMac.data(),
                        static_cast<uint16_t>(announceStartCounter + sequenceIndex),
                        sequenceIndex,
                        packet.data(),
                        packet.size(),
                        nullptr);
                    if (!triggerSender.send(packet.data(), packet.size())) {
                        responder.error = std::string("Trigger response send failed: ") + triggerSender.lastError();
                        responder.failed.store(true, std::memory_order_release);
                        responder.stop.store(true, std::memory_order_release);
                        return true;
                    }
                }

                responder.sequencesSent.fetch_add(1, std::memory_order_relaxed);
                if (options.slaveLocalCounter) {
                    std::printf(
                        "Slave trigger responder: answered trigger masterCounter=%u localAnnounceCounter=%u\n",
                        counter,
                        announceStartCounter);
                } else {
                    std::printf(
                        "Slave trigger responder: answered trigger counter=%u\n",
                        counter);
                }
                return true;
            });
            if (!ok) {
                responder.error = std::string("Trigger capture failed: ") + triggerCapture.lastError();
                responder.failed.store(true, std::memory_order_release);
                return;
            }
        }
    });
}

void stopSlaveTriggerResponder(SlaveTriggerResponder& responder) {
    responder.stop.store(true, std::memory_order_release);
    if (responder.worker.joinable()) {
        responder.worker.join();
    }
}

struct SlaveQueuedTriggerResponder {
    std::atomic<bool> stop{false};
    std::atomic<bool> failed{false};
    std::atomic<uint64_t> triggersSeen{0};
    std::atomic<uint64_t> sequencesQueued{0};
    std::atomic<uint64_t> sequencesInserted{0};
    std::atomic<uint64_t> pendingSequences{0};
    std::string error;
    std::thread worker;
};

void startSlaveQueuedTriggerResponder(
    SlaveQueuedTriggerResponder& responder,
    const std::string& deviceName,
    const ReacPacketTemplate& packetTemplate,
    const Options& options,
    const std::array<uint8_t, 6>& masterMac) {
    responder.stop.store(false, std::memory_order_release);
    responder.failed.store(false, std::memory_order_release);
    responder.triggersSeen.store(0, std::memory_order_release);
    responder.sequencesQueued.store(0, std::memory_order_release);
    responder.sequencesInserted.store(0, std::memory_order_release);
    responder.pendingSequences.store(0, std::memory_order_release);
    responder.error.clear();

    responder.worker = std::thread([&responder, deviceName, packetTemplate, options, masterMac]() {
        PcapCapture triggerCapture;
        if (!triggerCapture.available()) {
            responder.error = triggerCapture.lastError();
            responder.failed.store(true, std::memory_order_release);
            return;
        }
        if (!triggerCapture.open(deviceName, makeMasterOnlyReacFilter(options, packetTemplate.sourceMac))) {
            responder.error = std::string("Could not open queued trigger capture: ") + triggerCapture.lastError();
            responder.failed.store(true, std::memory_order_release);
            return;
        }

        auto lastResponseAt = std::chrono::steady_clock::time_point{};
        while (!responder.stop.load(std::memory_order_acquire) && !gStop.load(std::memory_order_acquire)) {
            const bool ok = triggerCapture.captureOnce(50, [&](const CapturedPacket& captured) {
                uint16_t counter = 0;
                if (!matchesClockSource(captured, packetTemplate.sourceMac, options, counter)) {
                    return true;
                }
                if (!macEquals(captured.data + 6, masterMac.data())) {
                    return true;
                }
                if (!isEacdControlType(captured, kSlaveTriggerControlType)) {
                    return true;
                }

                responder.triggersSeen.fetch_add(1, std::memory_order_relaxed);
                const auto now = std::chrono::steady_clock::now();
                if (lastResponseAt.time_since_epoch().count() != 0) {
                    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastResponseAt).count();
                    if (elapsedMs >= 0 && static_cast<uint32_t>(elapsedMs) < options.slaveTriggerDebounceMs) {
                        return true;
                    }
                }
                lastResponseAt = now;

                responder.pendingSequences.fetch_add(1, std::memory_order_relaxed);
                responder.sequencesQueued.fetch_add(1, std::memory_order_relaxed);
                std::printf(
                    "Slave queued trigger responder: queued trigger masterCounter=%u\n",
                    counter);
                return true;
            });
            if (!ok) {
                responder.error = std::string("Queued trigger capture failed: ") + triggerCapture.lastError();
                responder.failed.store(true, std::memory_order_release);
                return;
            }
        }
    });
}

void stopSlaveQueuedTriggerResponder(SlaveQueuedTriggerResponder& responder) {
    responder.stop.store(true, std::memory_order_release);
    if (responder.worker.joinable()) {
        responder.worker.join();
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

void startMasterClockFollower(
    MasterClockFollower& follower,
    const std::string& deviceName,
    const ReacPacketTemplate& packetTemplate,
    const Options& options) {
    follower.stop.store(false, std::memory_order_release);
    follower.failed.store(false, std::memory_order_release);
    follower.haveEstimate.store(false, std::memory_order_release);
    follower.estimatedPpsMilli.store(0, std::memory_order_release);
    follower.packetsObserved.store(0, std::memory_order_release);
    follower.error.clear();

    follower.worker = std::thread([&follower, deviceName, packetTemplate, options]() {
        PcapCapture clockCapture;
        if (!clockCapture.available()) {
            follower.error = clockCapture.lastError();
            follower.failed.store(true, std::memory_order_release);
            return;
        }
        if (!clockCapture.open(deviceName, makeMasterOnlyReacFilter(options, packetTemplate.sourceMac))) {
            follower.error = std::string("Could not open master clock capture: ") + clockCapture.lastError();
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

        while (!follower.stop.load(std::memory_order_acquire) && !gStop.load(std::memory_order_acquire)) {
            const bool ok = clockCapture.captureOnce(50, [&](const CapturedPacket& captured) {
                uint16_t counter = 0;
                if (!matchesClockSource(captured, packetTemplate.sourceMac, options, counter)) {
                    return true;
                }
                if (captured.timestampNanoseconds == 0) {
                    return true;
                }

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
                    if (intervalError <= 2 && instantPps >= 3000.0 && instantPps <= 5000.0) {
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
                follower.error = std::string("Master clock capture failed: ") + clockCapture.lastError();
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

uint64_t insertQueuedSlaveAnnounceSequences(
    std::vector<uint8_t>& queuedPackets,
    size_t packetBytes,
    const ReacPacketTemplate& packetTemplate,
    const std::array<uint8_t, 6>& masterMac,
    uint16_t startCounter,
    uint64_t requestedSequences) {
    if (requestedSequences == 0 || packetBytes == 0) {
        return 0;
    }

    const uint64_t packetCount = queuedPackets.size() / packetBytes;
    const uint64_t maxSequences = packetCount / kSlaveAnnouncePacketCount;
    const uint64_t sequences = std::min<uint64_t>(requestedSequences, maxSequences);
    for (uint64_t sequence = 0; sequence < sequences; ++sequence) {
        const uint64_t basePacket = sequence * kSlaveAnnouncePacketCount;
        for (size_t sequenceIndex = 0; sequenceIndex < kSlaveAnnouncePacketCount; ++sequenceIndex) {
            const uint64_t absoluteIndex = basePacket + sequenceIndex;
            buildSlaveAnnouncePacket(
                packetTemplate,
                masterMac.data(),
                static_cast<uint16_t>((static_cast<uint32_t>(startCounter) + absoluteIndex) & 0xffffU),
                sequenceIndex,
                queuedPackets.data() + static_cast<size_t>(absoluteIndex) * packetBytes,
                packetBytes,
                nullptr);
        }
    }
    return sequences;
}

bool sendChunkedTimedAudio(
    const std::string& deviceName,
    PcapSender& sender,
    ReacPacketEncoder& encoder,
    const ReacPacketTemplate& packetTemplate,
    const Options& options,
    uint64_t totalPackets,
    size_t packetCapacity,
    const std::array<uint8_t, 6>* masterMac) {
    SlaveQueuedTriggerResponder queuedResponder;
    const bool useQueuedResponder = options.slaveTriggerResponder && masterMac;
    if (useQueuedResponder) {
        std::puts("Slave queued trigger responder: watching for mixer triggers to insert into chunks.");
        startSlaveQueuedTriggerResponder(queuedResponder, deviceName, packetTemplate, options, *masterMac);
    }

    MasterClockFollower clockFollower;
    if (options.clockFollow) {
        std::puts("Master clock follow: measuring live master counter rate.");
        startMasterClockFollower(clockFollower, deviceName, packetTemplate, options);
    }

    std::vector<uint8_t> queuedPackets;
    double phase = 0.0;
    uint64_t sentPackets = 0;
    uint64_t effectiveTotalPackets = totalPackets;
    uint16_t counter = options.counter;
    auto started = std::chrono::steady_clock::now();
    auto lastProgress = started;
    bool transmitOk = true;
    bool clockFollowAdjustedTotal = false;
    double overheadEstimateSeconds = 0.0;
    uint32_t lastChunkSendPps = options.queuePacketsPerSecond;

    const auto targetPacketsPerSecond = [&]() {
        double targetPps = static_cast<double>(options.packetsPerSecond);
        if (options.clockFollow && clockFollower.haveEstimate.load(std::memory_order_acquire)) {
            targetPps = static_cast<double>(clockFollower.estimatedPpsMilli.load(std::memory_order_acquire)) / 1000.0;
            if (targetPps < 3000.0 || targetPps > 5000.0) {
                targetPps = static_cast<double>(options.packetsPerSecond);
            }
        }
        return targetPps;
    };

    std::printf(
        "Chunked timed queue: chunk=%ums targetPps=%u total=%llu packets\n",
        options.streamChunkMs,
        options.packetsPerSecond,
        static_cast<unsigned long long>(totalPackets));

    while (!gStop.load(std::memory_order_acquire) && sentPackets < effectiveTotalPackets) {
        const auto chunkStarted = std::chrono::steady_clock::now();
        const double targetPps = targetPacketsPerSecond();
        if (options.clockFollow &&
            !clockFollowAdjustedTotal &&
            clockFollower.haveEstimate.load(std::memory_order_acquire)) {
            const uint64_t adjustedTotal = std::max<uint64_t>(
                sentPackets + 1,
                static_cast<uint64_t>(std::llround(static_cast<double>(options.seconds) * targetPps)));
            effectiveTotalPackets = adjustedTotal;
            clockFollowAdjustedTotal = true;
            std::printf(
                "Master clock follow: using %.2f pps (%.1f Hz), duration target=%llu packets\n",
                targetPps,
                targetPps * static_cast<double>(kReacSamplesPerPacket),
                static_cast<unsigned long long>(effectiveTotalPackets));
        }

        const uint64_t chunkPackets = std::max<uint64_t>(1, static_cast<uint64_t>(std::llround(
            static_cast<double>(options.streamChunkMs) * targetPps / 1000.0)));
        const uint64_t packetCount = std::min<uint64_t>(chunkPackets, effectiveTotalPackets - sentPackets);
        if (!buildTimedAudioQueueRange(
                encoder,
                options,
                packetCapacity,
                packetCount,
                counter,
                targetPps * static_cast<double>(kReacSamplesPerPacket),
                phase,
                false,
                queuedPackets)) {
            transmitOk = false;
            break;
        }

        if (useQueuedResponder) {
            const uint64_t requested = queuedResponder.pendingSequences.exchange(0, std::memory_order_acq_rel);
            if (requested > 0) {
                const uint64_t inserted = insertQueuedSlaveAnnounceSequences(
                    queuedPackets,
                    packetCapacity,
                    packetTemplate,
                    *masterMac,
                    counter,
                    requested);
                if (inserted < requested) {
                    queuedResponder.pendingSequences.fetch_add(requested - inserted, std::memory_order_relaxed);
                }
                queuedResponder.sequencesInserted.fetch_add(inserted, std::memory_order_relaxed);
                if (inserted > 0) {
                    std::printf(
                        "Slave queued trigger responder: inserted %llu sequence(s) at localCounter=%u\n",
                        static_cast<unsigned long long>(inserted),
                        counter);
                }
            }
        }

        uint32_t chunkSendPps = options.queuePacketsPerSecond;
        if (options.clockFollow) {
            const double targetChunkSeconds = static_cast<double>(packetCount) / targetPps;
            const double compensatedSeconds = (std::max)(0.001, targetChunkSeconds - overheadEstimateSeconds);
            const double requestedPps = static_cast<double>(packetCount) / compensatedSeconds;
            chunkSendPps = static_cast<uint32_t>(std::llround(std::clamp(
                requestedPps,
                1.0,
                static_cast<double>(kMaxQueuePacketsPerSecond))));
        }
        lastChunkSendPps = chunkSendPps;

        if (!sender.sendTimedQueue(queuedPackets, static_cast<uint32_t>(packetCapacity), chunkSendPps)) {
            transmitOk = false;
            break;
        }

        const auto chunkEnded = std::chrono::steady_clock::now();
        const double chunkElapsed = std::chrono::duration<double>(chunkEnded - chunkStarted).count();
        if (options.clockFollow) {
            const double nominalSendSeconds = static_cast<double>(packetCount) / static_cast<double>(chunkSendPps);
            const double observedOverhead = (std::max)(0.0, chunkElapsed - nominalSendSeconds);
            overheadEstimateSeconds = overheadEstimateSeconds <= 0.0
                ? observedOverhead
                : (overheadEstimateSeconds * 0.80 + observedOverhead * 0.20);
        }

        sentPackets += packetCount;
        counter = static_cast<uint16_t>((static_cast<uint32_t>(counter) + packetCount) & 0xffffU);

        const auto now = chunkEnded;
        if (std::chrono::duration_cast<std::chrono::seconds>(now - lastProgress).count() >= 5) {
            const double elapsed = std::chrono::duration<double>(now - started).count();
            if (options.clockFollow) {
                const double masterPps = clockFollower.haveEstimate.load(std::memory_order_acquire)
                    ? static_cast<double>(clockFollower.estimatedPpsMilli.load(std::memory_order_acquire)) / 1000.0
                    : 0.0;
                std::printf(
                    "Chunked timed queue: sent=%llu/%llu elapsed=%.1fs rate=%.1f packets/s sendPps=%u masterPps=%.2f overhead=%.3fms\n",
                    static_cast<unsigned long long>(sentPackets),
                    static_cast<unsigned long long>(effectiveTotalPackets),
                    elapsed,
                    elapsed > 0.0 ? static_cast<double>(sentPackets) / elapsed : 0.0,
                    lastChunkSendPps,
                    masterPps,
                    overheadEstimateSeconds * 1000.0);
            } else {
                std::printf(
                    "Chunked timed queue: sent=%llu/%llu elapsed=%.1fs rate=%.1f packets/s\n",
                    static_cast<unsigned long long>(sentPackets),
                    static_cast<unsigned long long>(effectiveTotalPackets),
                    elapsed,
                    elapsed > 0.0 ? static_cast<double>(sentPackets) / elapsed : 0.0);
            }
            lastProgress = now;
        }
    }

    if (useQueuedResponder) {
        stopSlaveQueuedTriggerResponder(queuedResponder);
        std::printf(
            "Slave queued trigger responder: triggers=%llu queued=%llu inserted=%llu pending=%llu\n",
            static_cast<unsigned long long>(queuedResponder.triggersSeen.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(queuedResponder.sequencesQueued.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(queuedResponder.sequencesInserted.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(queuedResponder.pendingSequences.load(std::memory_order_relaxed)));
    }
    if (options.clockFollow) {
        stopMasterClockFollower(clockFollower);
        const double masterPps = clockFollower.haveEstimate.load(std::memory_order_acquire)
            ? static_cast<double>(clockFollower.estimatedPpsMilli.load(std::memory_order_acquire)) / 1000.0
            : 0.0;
        std::printf(
            "Master clock follow: observed=%llu estimate=%.2f pps (%.1f Hz) lastSendPps=%u\n",
            static_cast<unsigned long long>(clockFollower.packetsObserved.load(std::memory_order_relaxed)),
            masterPps,
            masterPps * static_cast<double>(kReacSamplesPerPacket),
            lastChunkSendPps);
    }

    if (!transmitOk) {
        std::fprintf(stderr, "Chunked timed queue transmit failed: %s\n", sender.lastError().c_str());
        return false;
    }
    if (queuedResponder.failed.load(std::memory_order_acquire)) {
        std::fprintf(stderr, "Slave queued trigger responder failed: %s\n", queuedResponder.error.c_str());
        return false;
    }
    if (clockFollower.failed.load(std::memory_order_acquire)) {
        std::fprintf(stderr, "Master clock follow failed: %s\n", clockFollower.error.c_str());
        return false;
    }

    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf(
        "Done. sent=%llu elapsed=%.3fs rate=%.1f packets/s\n",
        static_cast<unsigned long long>(sentPackets),
        elapsed,
        elapsed > 0.0 ? static_cast<double>(sentPackets) / elapsed : 0.0);
    return true;
}

uint64_t insertSlaveKeepaliveSequences(
    std::vector<uint8_t>& queuedPackets,
    size_t packetBytes,
    const ReacPacketTemplate& packetTemplate,
    const std::array<uint8_t, 6>& masterMac,
    const Options& options,
    uint16_t startCounter,
    uint64_t* intervalPacketsOut) {
    if (options.slaveKeepaliveMs == 0 || packetBytes == 0) {
        return 0;
    }

    const uint64_t packetCount = queuedPackets.size() / packetBytes;
    uint64_t intervalPackets = static_cast<uint64_t>(std::llround(
        static_cast<double>(options.slaveKeepaliveMs) * static_cast<double>(options.queuePacketsPerSecond) / 1000.0));
    if (intervalPackets < kSlaveAnnouncePacketCount) {
        intervalPackets = kSlaveAnnouncePacketCount;
    }
    if (intervalPacketsOut) {
        *intervalPacketsOut = intervalPackets;
    }

    uint64_t inserted = 0;
    for (uint64_t packetIndex = intervalPackets;
         packetIndex + kSlaveAnnouncePacketCount <= packetCount;
         packetIndex += intervalPackets) {
        for (size_t sequenceIndex = 0; sequenceIndex < kSlaveAnnouncePacketCount; ++sequenceIndex) {
            const uint64_t absoluteIndex = packetIndex + sequenceIndex;
            buildSlaveAnnouncePacket(
                packetTemplate,
                masterMac.data(),
                static_cast<uint16_t>((static_cast<uint32_t>(startCounter) + absoluteIndex) & 0xffffU),
                sequenceIndex,
                queuedPackets.data() + static_cast<size_t>(absoluteIndex) * packetBytes,
                packetBytes,
                nullptr);
        }
        ++inserted;
    }
    return inserted;
}

bool sendClocked(
    const std::string& deviceName,
    PcapSender& sender,
    ReacPacketEncoder& encoder,
    const ReacPacketTemplate& packetTemplate,
    const Options& options,
    uint64_t totalPackets,
    size_t packetCapacity) {
    PcapCapture clockCapture;
    if (!clockCapture.available()) {
        std::fprintf(stderr, "%s\n", clockCapture.lastError().c_str());
        return false;
    }
    if (!clockCapture.open(deviceName, makeMasterOnlyReacFilter(options, packetTemplate.sourceMac))) {
        std::fprintf(stderr, "Could not open clock capture: %s\n", clockCapture.lastError().c_str());
        return false;
    }

    std::vector<uint8_t> packet(packetCapacity, 0);
    std::vector<float> audio(static_cast<size_t>(kReacSamplesPerPacket) * options.channels, 0.0f);
    double phase = 0.0;
    uint64_t sent = 0;
    bool started = false;
    bool failed = false;
    auto startedAt = std::chrono::steady_clock::now();

    std::puts("Clocked transmit. Sending one packet for each matching live REAC packet.");
    while (!gStop.load(std::memory_order_acquire) && sent < totalPackets) {
        const bool ok = clockCapture.captureOnce(50, [&](const CapturedPacket& captured) {
            uint16_t clockCounter = 0;
            if (!matchesClockSource(captured, packetTemplate.sourceMac, options, clockCounter)) {
                return true;
            }

            if (!started) {
                started = true;
                startedAt = std::chrono::steady_clock::now();
                std::printf(
                    "Clock: first src=%s counter=%u sendCounter=%u\n",
                    formatMac(captured.data + 6).c_str(),
                    clockCounter,
                    static_cast<uint16_t>((clockCounter + options.clockOffsetPackets) & 0xffffU));
            }

            fillAudio(audio, options, static_cast<double>(options.audioSampleRate), phase);
            size_t packetBytes = 0;
            const uint16_t sendCounter = static_cast<uint16_t>((clockCounter + options.clockOffsetPackets) & 0xffffU);
            const ReacEncodeStatus status = encoder.encodeInterleavedFloat(
                audio.data(),
                options.channels,
                kReacSamplesPerPacket,
                sendCounter,
                packet.data(),
                packet.size(),
                &packetBytes);
            if (status != ReacEncodeStatus::ok || packetBytes != packetCapacity) {
                std::fprintf(stderr, "Encode failed during clocked transmit: %s\n", ReacPacketEncoder::toString(status));
                failed = true;
                gStop.store(true, std::memory_order_release);
                return true;
            }
            if (!sender.send(packet.data(), packetBytes)) {
                std::fprintf(stderr, "Clocked transmit failed: %s\n", sender.lastError().c_str());
                failed = true;
                gStop.store(true, std::memory_order_release);
                return true;
            }

            ++sent;
            return true;
        });
        if (!ok) {
            std::fprintf(stderr, "Clock capture failed: %s\n", clockCapture.lastError().c_str());
            return false;
        }
    }

    const double elapsed = started
                               ? std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt).count()
                               : 0.0;
    std::printf("Done. sent=%llu elapsed=%.3fs rate=%.1f packets/s\n",
        static_cast<unsigned long long>(sent),
        elapsed,
        elapsed > 0.0 ? static_cast<double>(sent) / elapsed : 0.0);
    return !failed;
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
        std::fprintf(stderr, "Could not resolve device: %s\n", options.deviceSelector.c_str());
        printDevices(devices);
        return 1;
    }

    ReacPacketTemplate packetTemplate{};
    if (options.haveDestinationMac) {
        std::copy(options.destinationMac.begin(), options.destinationMac.end(), packetTemplate.destinationMac);
    } else {
        std::copy(std::begin(kBroadcastMac), std::end(kBroadcastMac), packetTemplate.destinationMac);
    }

    std::array<uint8_t, 6> adapterMac{};
    if (options.haveSourceMac) {
        std::copy(options.sourceMac.begin(), options.sourceMac.end(), packetTemplate.sourceMac);
    } else if (adapterMacFromNpcapName(deviceName, adapterMac)) {
        std::copy(adapterMac.begin(), adapterMac.end(), packetTemplate.sourceMac);
    } else {
        std::copy(std::begin(kFallbackSourceMac), std::end(kFallbackSourceMac), packetTemplate.sourceMac);
    }
    packetTemplate.packetType = 0;
    fillHeader(packetTemplate, options);

    const size_t packetCapacity = ReacPacketEncoder::packetBytesForChannels(options.channels);
    std::vector<uint8_t> packet(packetCapacity, 0);
    std::vector<float> audio(static_cast<size_t>(kReacSamplesPerPacket) * options.channels, 0.0f);
    ReacPacketEncoder encoder;
    encoder.setTemplate(packetTemplate);

    const uint64_t totalPackets = static_cast<uint64_t>(options.seconds) * options.packetsPerSecond;
    std::printf("Device: %s\n", deviceName.c_str());
    std::printf("Mode: %s\n", options.send ? "SEND" : "DRY-RUN");
    std::printf("Profile: %s channels=%u packetBytes=%zu sampleRate=%u pps=%u queuePps=%u seconds=%d packets=%llu\n",
        options.profile.c_str(),
        options.channels,
        packetCapacity,
        options.audioSampleRate,
        options.packetsPerSecond,
        options.queuePacketsPerSecond,
        options.seconds,
        static_cast<unsigned long long>(totalPackets));
    std::printf("Ethernet: src=%s dst=%s\n",
        formatMac(packetTemplate.sourceMac).c_str(),
        formatMac(packetTemplate.destinationMac).c_str());
    if (options.toneChannel > 0) {
        std::printf("Audio: tone channel=%d hz=%.2f level=%.1f dBFS\n", options.toneChannel, options.toneHz, options.toneLevelDb);
    } else {
        std::puts("Audio: silence");
    }
    if (options.syncCounter) {
        std::printf(
            "Sync: enabled wait=%ums lead=%u packets%s%s\n",
            options.syncWaitMs,
            options.syncLeadPackets,
            options.haveSyncSourceMac ? " source=" : "",
            options.haveSyncSourceMac ? formatMac(options.syncSourceMac.data()).c_str() : "");
    }
    if (options.clocked) {
        std::printf(
            "Clocked: enabled offset=%u packets%s%s\n",
            options.clockOffsetPackets,
            options.haveSyncSourceMac ? " source=" : "",
            options.haveSyncSourceMac ? formatMac(options.syncSourceMac.data()).c_str() : "");
    }
    if (options.clockFollow) {
        std::puts("Master clock follow: enabled");
    }
    if (options.slaveHandshake) {
        std::printf("Slave handshake: enabled wait=%ums\n", options.slaveHandshakeWaitMs);
    }
    if (options.slaveHandshakeContinuousCounter) {
        std::puts("Slave handshake counter: continuous post-announce mode enabled");
    }
    if (options.slaveLocalCounter) {
        std::puts("Slave local counter: enabled");
    }
    if (options.slavePerGronState) {
        std::puts("Slave Per-Gron state gate: enabled");
    }
    if (options.slavePrerollMs > 0) {
        std::printf("Slave preroll: enabled duration=%ums\n", options.slavePrerollMs);
    }
    if (options.slaveStandby) {
        std::printf("Slave standby: enabled chunk=%ums\n", options.slaveStandbyChunkMs);
    }
    if (options.slaveStandbyResponder) {
        std::puts("Slave standby responder: enabled");
    }
    if (options.slaveKeepaliveMs > 0) {
        std::printf("Slave keepalive: enabled interval=%ums\n", options.slaveKeepaliveMs);
    }
    if (options.slaveTriggerResponder) {
        std::printf("Slave trigger responder: enabled debounce=%ums\n", options.slaveTriggerDebounceMs);
    }
    if (options.streamChunkMs > 0) {
        std::printf("Chunked stream: enabled chunk=%ums\n", options.streamChunkMs);
    }

    if (!options.send) {
        double dryRunPhase = 0.0;
        fillAudio(audio, options, static_cast<double>(options.audioSampleRate), dryRunPhase);
        size_t packetBytes = 0;
        const ReacEncodeStatus status = encoder.encodeInterleavedFloat(
            audio.data(),
            options.channels,
            kReacSamplesPerPacket,
            options.counter,
            packet.data(),
            packet.size(),
            &packetBytes);
        if (status != ReacEncodeStatus::ok) {
            std::fprintf(stderr, "Encode failed: %s\n", ReacPacketEncoder::toString(status));
            return 1;
        }
        std::printf("Dry run encoded one packet successfully: %zu bytes. Add --send to transmit.\n", packetBytes);
        return 0;
    }

    PcapSender sender;
    if (!sender.available()) {
        std::fprintf(stderr, "%s\n", sender.lastError().c_str());
        return 1;
    }
    if (!sender.open(deviceName)) {
        std::fprintf(stderr, "Could not open sender: %s\n", sender.lastError().c_str());
        return 1;
    }

    std::vector<uint8_t> queuedPackets;
    bool queuedPacketsBuilt = false;
    if (!options.clocked && options.slaveHandshake && options.streamChunkMs == 0) {
        if (!buildTimedAudioQueue(encoder, options, packetCapacity, totalPackets, queuedPackets)) {
            return 1;
        }
        queuedPacketsBuilt = true;
        std::puts("Slave handshake: timed queue is prebuilt to keep the post-announce gap short.");
    }

    std::array<uint8_t, 6> handshakeMasterMac{};
    bool haveHandshakeMasterMac = false;
    CounterSyncResult handshakeSync{};
    if (options.slaveHandshake) {
        if (!sendSlavePreroll(sender, packetTemplate, options, packetCapacity)) {
            return 1;
        }

        std::array<uint8_t, 2> handshakeHeaderPair{};
        bool packetTemplateChanged = false;
        if (!runSlaveHandshake(deviceName, sender, packetTemplate, options, &handshakeMasterMac, &handshakeHeaderPair, &handshakeSync)) {
            return 1;
        }
        haveHandshakeMasterMac = true;
        if (!options.haveDestinationMac) {
            std::copy(handshakeMasterMac.begin(), handshakeMasterMac.end(), packetTemplate.destinationMac);
            packetTemplateChanged = true;
            std::printf(
                "Slave handshake: audio destination updated to %s\n",
                formatMac(packetTemplate.destinationMac).c_str());
        }
        if (!options.haveHeaderRepeat) {
            fillHeaderPair(packetTemplate, handshakeHeaderPair[0], handshakeHeaderPair[1]);
            packetTemplateChanged = true;
            std::printf(
                "Slave handshake: audio header updated to %02x%02x repeated\n",
                handshakeHeaderPair[0],
                handshakeHeaderPair[1]);
        }
        if (packetTemplateChanged) {
            encoder.setTemplate(packetTemplate);
            if (queuedPacketsBuilt) {
                patchPacketDestination(queuedPackets, packetCapacity, packetTemplate.destinationMac);
                patchPacketHeaderData(queuedPackets, packetCapacity, packetTemplate.headerData);
            }
        }
    }

    if (options.clocked) {
        return sendClocked(deviceName, sender, encoder, packetTemplate, options, totalPackets, packetCapacity) ? 0 : 1;
    }

    if (options.streamChunkMs == 0 && !queuedPacketsBuilt) {
        if (!buildTimedAudioQueue(encoder, options, packetCapacity, totalPackets, queuedPackets)) {
            return 1;
        }
        queuedPacketsBuilt = true;
    }

    if (options.syncCounter) {
        CounterSyncResult sync{};
        if (handshakeSync.found) {
            sync = handshakeSync;
            std::puts("Sync: using slave handshake trigger counter.");
        } else {
            std::puts("Waiting for live REAC counter before transmit.");
            std::string syncError;
            if (!waitForClockCounter(deviceName, packetTemplate.sourceMac, options, sync, syncError)) {
                std::fprintf(stderr, "Counter sync failed: %s\n", syncError.c_str());
                return 1;
            }
        }

        const auto now = std::chrono::steady_clock::now();
        const double elapsedSeconds = std::chrono::duration<double>(now - sync.observedAt).count();
        const double nonnegativeElapsedSeconds = elapsedSeconds > 0.0 ? elapsedSeconds : 0.0;
        const uint32_t elapsedPackets = static_cast<uint32_t>(std::round(nonnegativeElapsedSeconds * options.packetsPerSecond));
        uint32_t startOffset = elapsedPackets + options.syncLeadPackets;
        if (handshakeSync.found && options.slaveLocalCounter) {
            startOffset = static_cast<uint32_t>(kSlaveAnnouncePacketCount + 1);
            std::printf(
                "Sync: slave local counter strict continuation enabled, startOffset=%u (elapsed/sync-lead ignored)\n",
                startOffset);
        } else if (handshakeSync.found && options.slaveHandshakeContinuousCounter) {
            startOffset = std::max<uint32_t>(
                elapsedPackets,
                static_cast<uint32_t>(kSlaveAnnouncePacketCount + 1));
            std::printf(
                "Sync: handshake continuous counter enabled, startOffset=%u (sync-lead ignored)\n",
                startOffset);
        }
        options.counter = static_cast<uint16_t>((sync.counter + startOffset) & 0xffffU);
        if (queuedPacketsBuilt) {
            patchPacketCounters(queuedPackets, packetCapacity, options.counter);
        }
        std::printf(
            "Sync: observed src=%s counter=%u elapsedPackets=%u startCounter=%u\n",
            formatMac(sync.sourceMac.data()).c_str(),
            sync.counter,
            elapsedPackets,
            options.counter);
    }

    if (options.streamChunkMs > 0) {
        return sendChunkedTimedAudio(
            deviceName,
            sender,
            encoder,
            packetTemplate,
            options,
            totalPackets,
            packetCapacity,
            haveHandshakeMasterMac ? &handshakeMasterMac : nullptr) ? 0 : 1;
    }

    if (options.slaveKeepaliveMs > 0 && haveHandshakeMasterMac) {
        uint64_t intervalPackets = 0;
        const uint64_t keepaliveCount = insertSlaveKeepaliveSequences(
            queuedPackets,
            packetCapacity,
            packetTemplate,
            handshakeMasterMac,
            options,
            options.counter,
            &intervalPackets);
        std::printf(
            "Slave keepalive: inserted %llu announce sequences every %ums (~%llu queued packets)\n",
            static_cast<unsigned long long>(keepaliveCount),
            options.slaveKeepaliveMs,
            static_cast<unsigned long long>(intervalPackets));
    }

    SlaveTriggerResponder triggerResponder;
    if (options.slaveTriggerResponder && haveHandshakeMasterMac) {
        std::puts("Slave trigger responder: watching for live mixer EACD triggers.");
        startSlaveTriggerResponder(triggerResponder, deviceName, packetTemplate, options, handshakeMasterMac);
    }

    std::puts("Transmitting timed queue. Press Ctrl+C to terminate the process if needed.");
    const auto started = std::chrono::steady_clock::now();
    const bool transmitOk = sender.sendTimedQueue(queuedPackets, static_cast<uint32_t>(packetCapacity), options.queuePacketsPerSecond);
    if (triggerResponder.worker.joinable()) {
        stopSlaveTriggerResponder(triggerResponder);
        std::printf(
            "Slave trigger responder: triggers=%llu answered=%llu\n",
            static_cast<unsigned long long>(triggerResponder.triggersSeen.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(triggerResponder.sequencesSent.load(std::memory_order_relaxed)));
    }
    if (!transmitOk) {
        std::fprintf(stderr, "Timed queue transmit failed: %s\n", sender.lastError().c_str());
        return 1;
    }
    if (triggerResponder.failed.load(std::memory_order_acquire)) {
        std::fprintf(stderr, "Slave trigger responder failed: %s\n", triggerResponder.error.c_str());
        return 1;
    }
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::printf("Done. sent=%llu elapsed=%.3fs rate=%.1f packets/s\n",
        static_cast<unsigned long long>(totalPackets),
        elapsed,
        elapsed > 0.0 ? static_cast<double>(totalPackets) / elapsed : 0.0);
    return 0;
}
