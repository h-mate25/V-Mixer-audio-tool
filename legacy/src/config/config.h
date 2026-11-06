#pragma once

#include <cstdint>
#include <string>

#include "../audio/audio_engine.h"
#include "../reac/reac_return_map.h"

namespace windows_reac_asio {

struct NetworkConfig {
    std::wstring interfaceName;
    uint16_t port = 5004;
};

struct DiagnosticsConfig {
    bool enableDebugLog = true;
};

struct MonitorConfig {
    bool enableWindowsOutput = true;
    uint32_t bufferFrames = 8192;
};

struct ReceiverConfig {
    bool readOnly = false;
};

struct TransmitConfig {
    bool enableReacOutput = false;
    bool exposeMonoReturns = false;
    uint32_t outputChannels = 16;
    uint32_t bufferFrames = 4096;
    uint32_t returnMap = kDefaultReacReturnMapMode;
};

struct DriverConfig {
    AudioEngineConfig audio;
    NetworkConfig network;
    DiagnosticsConfig diagnostics;
    MonitorConfig monitor;
    ReceiverConfig receiver;
    TransmitConfig transmit;
};

DriverConfig loadConfig();
bool saveConfig(const DriverConfig& config, std::wstring* errorMessage = nullptr);
const wchar_t* configFilePath() noexcept;

} // namespace windows_reac_asio
