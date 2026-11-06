#pragma once

#include <cstdint>

namespace windows_reac_asio {

enum class ReacReturnMapMode : uint32_t {
    direct = 0,
    s1608MirrorFirst8 = 1,
};

constexpr uint32_t kInvalidReacSourceChannel = 0xffffffffU;
constexpr uint32_t kS1608ReturnWireChannels = 16;
constexpr uint32_t kS1608ReturnBankChannels = 8;
constexpr uint32_t kDefaultReacReturnMapMode = static_cast<uint32_t>(ReacReturnMapMode::s1608MirrorFirst8);

ReacReturnMapMode normalizeReacReturnMapMode(uint32_t mode) noexcept;
uint32_t reacReturnMapModeValue(ReacReturnMapMode mode) noexcept;
const wchar_t* reacReturnMapDisplayName(ReacReturnMapMode mode) noexcept;

uint32_t mapReacReturnSourceChannel(
    ReacReturnMapMode mode,
    uint32_t wireChannel,
    uint32_t sourceChannelCount) noexcept;

} // namespace windows_reac_asio
