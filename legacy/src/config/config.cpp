#include "config.h"

#include <Windows.h>

#include <algorithm>
#include <iterator>
#include <string>

namespace windows_reac_asio {
namespace {

constexpr wchar_t kConfigDirectory[] = L"C:\\ProgramData\\WindowsReacASIO";
constexpr wchar_t kConfigPath[] = L"C:\\ProgramData\\WindowsReacASIO\\config.ini";

uint32_t readIniUint(const wchar_t* section, const wchar_t* key, uint32_t fallback) {
    return GetPrivateProfileIntW(section, key, fallback, kConfigPath);
}

std::wstring readIniString(const wchar_t* section, const wchar_t* key) {
    wchar_t value[512] = {};
    GetPrivateProfileStringW(section, key, L"", value, static_cast<DWORD>(std::size(value)), kConfigPath);
    return value;
}

std::wstring lastErrorMessage(DWORD errorCode) {
    if (errorCode == ERROR_SUCCESS) {
        return {};
    }

    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        0,
        reinterpret_cast<wchar_t*>(&buffer),
        0,
        nullptr);
    if (length == 0 || !buffer) {
        return L"Windows error " + std::to_wstring(errorCode);
    }

    std::wstring message(buffer, length);
    LocalFree(buffer);
    return message;
}

bool ensureConfigDirectory(std::wstring* errorMessage) {
    if (CreateDirectoryW(kConfigDirectory, nullptr)) {
        return true;
    }

    const DWORD error = GetLastError();
    if (error == ERROR_ALREADY_EXISTS) {
        return true;
    }

    if (errorMessage) {
        *errorMessage = L"Could not create " + std::wstring(kConfigDirectory) + L": " + lastErrorMessage(error);
    }
    return false;
}

bool writeIniString(const wchar_t* section, const wchar_t* key, const std::wstring& value, std::wstring* errorMessage) {
    if (WritePrivateProfileStringW(section, key, value.c_str(), kConfigPath)) {
        return true;
    }

    if (errorMessage) {
        const DWORD error = GetLastError();
        *errorMessage = L"Could not write " + std::wstring(section) + L"." + key + L" to " + kConfigPath;
        if (error != ERROR_SUCCESS) {
            *errorMessage += L": " + lastErrorMessage(error);
        }
    }
    return false;
}

bool writeIniUint(const wchar_t* section, const wchar_t* key, uint32_t value, std::wstring* errorMessage) {
    return writeIniString(section, key, std::to_wstring(value), errorMessage);
}

} // namespace

DriverConfig loadConfig() {
    DriverConfig config;
    config.audio.sampleRate = readIniUint(L"Audio", L"SampleRate", 48000);
    config.audio.channelCount = readIniUint(L"Audio", L"ChannelCount", 40);
    config.audio.asioBufferSize = readIniUint(L"Audio", L"AsioBufferSize", 512);
    config.audio.jitterBufferSamples = readIniUint(L"Audio", L"JitterBufferSamples", 8192);
    config.network.interfaceName = readIniString(L"Network", L"Interface");
    config.network.port = static_cast<uint16_t>(readIniUint(L"Network", L"Port", 5004));
    config.diagnostics.enableDebugLog = readIniUint(L"Diagnostics", L"EnableDebugLog", 1) != 0;
    config.monitor.enableWindowsOutput = readIniUint(L"Monitor", L"EnableWindowsOutput", 1) != 0;
    config.monitor.bufferFrames = readIniUint(L"Monitor", L"BufferFrames", 8192);
    config.receiver.readOnly = readIniUint(L"Receiver", L"ReadOnly", 0) != 0;
    config.transmit.enableReacOutput = readIniUint(L"Transmit", L"EnableReacOutput", 0) != 0;
    config.transmit.exposeMonoReturns = readIniUint(L"Transmit", L"ExposeMonoReturns", 0) != 0;
    config.transmit.outputChannels = std::clamp<uint32_t>(
        readIniUint(L"Transmit", L"OutputChannels", kS1608ReturnWireChannels),
        2,
        kS1608ReturnWireChannels);
    if ((config.transmit.outputChannels & 1U) != 0) {
        --config.transmit.outputChannels;
    }
    config.transmit.bufferFrames = readIniUint(L"Transmit", L"BufferFrames", 4096);
    config.transmit.returnMap = reacReturnMapModeValue(
        normalizeReacReturnMapMode(readIniUint(L"Transmit", L"ReturnMap", kDefaultReacReturnMapMode)));
    if (config.receiver.readOnly) {
        config.transmit.enableReacOutput = false;
        config.transmit.exposeMonoReturns = false;
    }
    return config;
}

bool saveConfig(const DriverConfig& config, std::wstring* errorMessage) {
    if (!ensureConfigDirectory(errorMessage)) {
        return false;
    }

    const bool enableReacOutput = !config.receiver.readOnly && config.transmit.enableReacOutput;
    const bool exposeMonoReturns = !config.receiver.readOnly && config.transmit.exposeMonoReturns;

    return writeIniUint(L"Audio", L"SampleRate", config.audio.sampleRate, errorMessage) &&
           writeIniUint(L"Audio", L"ChannelCount", config.audio.channelCount, errorMessage) &&
           writeIniUint(L"Audio", L"AsioBufferSize", config.audio.asioBufferSize, errorMessage) &&
           writeIniUint(L"Audio", L"JitterBufferSamples", config.audio.jitterBufferSamples, errorMessage) &&
           writeIniString(L"Network", L"Interface", config.network.interfaceName, errorMessage) &&
           writeIniUint(L"Network", L"Port", config.network.port, errorMessage) &&
           writeIniUint(L"Diagnostics", L"EnableDebugLog", config.diagnostics.enableDebugLog ? 1U : 0U, errorMessage) &&
           writeIniUint(L"Monitor", L"EnableWindowsOutput", config.monitor.enableWindowsOutput ? 1U : 0U, errorMessage) &&
           writeIniUint(L"Monitor", L"BufferFrames", config.monitor.bufferFrames, errorMessage) &&
           writeIniUint(L"Receiver", L"ReadOnly", config.receiver.readOnly ? 1U : 0U, errorMessage) &&
           writeIniUint(L"Transmit", L"EnableReacOutput", enableReacOutput ? 1U : 0U, errorMessage) &&
           writeIniUint(L"Transmit", L"ExposeMonoReturns", exposeMonoReturns ? 1U : 0U, errorMessage) &&
           writeIniUint(L"Transmit", L"OutputChannels", config.transmit.outputChannels, errorMessage) &&
           writeIniUint(L"Transmit", L"BufferFrames", config.transmit.bufferFrames, errorMessage) &&
           writeIniUint(L"Transmit", L"ReturnMap", reacReturnMapModeValue(
               normalizeReacReturnMapMode(config.transmit.returnMap)), errorMessage);
}

const wchar_t* configFilePath() noexcept {
    return kConfigPath;
}

} // namespace windows_reac_asio
