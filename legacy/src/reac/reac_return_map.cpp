#include "reac_return_map.h"

namespace windows_reac_asio {

ReacReturnMapMode normalizeReacReturnMapMode(uint32_t mode) noexcept {
    switch (static_cast<ReacReturnMapMode>(mode)) {
    case ReacReturnMapMode::direct:
    case ReacReturnMapMode::s1608MirrorFirst8:
        return static_cast<ReacReturnMapMode>(mode);
    }
    return ReacReturnMapMode::s1608MirrorFirst8;
}

uint32_t reacReturnMapModeValue(ReacReturnMapMode mode) noexcept {
    return static_cast<uint32_t>(normalizeReacReturnMapMode(static_cast<uint32_t>(mode)));
}

const wchar_t* reacReturnMapDisplayName(ReacReturnMapMode mode) noexcept {
    switch (normalizeReacReturnMapMode(static_cast<uint32_t>(mode))) {
    case ReacReturnMapMode::direct:
        return L"Direct 1:1";
    case ReacReturnMapMode::s1608MirrorFirst8:
        return L"S-1608 mirror";
    }
    return L"S-1608 mirror";
}

uint32_t mapReacReturnSourceChannel(
    ReacReturnMapMode mode,
    uint32_t wireChannel,
    uint32_t sourceChannelCount) noexcept {
    if (sourceChannelCount == 0) {
        return kInvalidReacSourceChannel;
    }

    switch (normalizeReacReturnMapMode(static_cast<uint32_t>(mode))) {
    case ReacReturnMapMode::direct:
        return wireChannel < sourceChannelCount ? wireChannel : kInvalidReacSourceChannel;

    case ReacReturnMapMode::s1608MirrorFirst8: {
        const uint32_t sourceChannel = wireChannel % kS1608ReturnBankChannels;
        return sourceChannel < sourceChannelCount ? sourceChannel : kInvalidReacSourceChannel;
    }
    }

    return kInvalidReacSourceChannel;
}

} // namespace windows_reac_asio
