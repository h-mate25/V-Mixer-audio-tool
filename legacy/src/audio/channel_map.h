#pragma once

#include <cstdint>

namespace windows_reac_asio {

constexpr uint32_t kDefaultInputChannelCount = 40;

inline uint32_t hostInputToEngineChannel(uint32_t hostChannel) noexcept {
    return hostChannel;
}

} // namespace windows_reac_asio
