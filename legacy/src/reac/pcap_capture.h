#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace windows_reac_asio {

struct CaptureDeviceInfo {
    std::string name;
    std::string description;
};

struct CapturedPacket {
    const uint8_t* data = nullptr;
    uint32_t size = 0;
    uint32_t wireSize = 0;
    uint64_t timestampNanoseconds = 0;
};

class PcapCapture {
public:
    using PacketCallback = std::function<bool(const CapturedPacket&)>;

    PcapCapture();
    ~PcapCapture();

    PcapCapture(const PcapCapture&) = delete;
    PcapCapture& operator=(const PcapCapture&) = delete;

    bool available() const noexcept;
    const std::string& lastError() const noexcept { return lastError_; }

    std::vector<CaptureDeviceInfo> listDevices();
    std::string resolveDeviceName(const std::string& selector, const std::vector<CaptureDeviceInfo>& devices) const;

    bool open(const std::string& deviceName, const std::string& filterExpression = "ether proto 0x8819");
    void close() noexcept;
    bool captureOnce(uint32_t waitMilliseconds, const PacketCallback& callback);

private:
    struct Impl;
    Impl* impl_ = nullptr;
    std::string lastError_;
};

std::string wideToUtf8(const std::wstring& value);
std::string windowsFriendlyInterfaceName(const std::string& deviceName);

} // namespace windows_reac_asio
