#pragma once

#include <Windows.h>

#include <functional>
#include <string>

#include "../config/config.h"
#include "../diagnostics/counters.h"

namespace windows_reac_asio {

struct ControlPanelState {
    DriverConfig config;
    AudioCountersSnapshot counters;
    std::function<AudioCountersSnapshot()> readCounters;
    std::function<bool(const DriverConfig&, std::wstring*)> restartDriver;
    bool driverStarted = false;
    bool usingReacReceiver = false;
    double currentSampleRate = 48000.0;
    long currentBufferSize = 512;
    std::string currentInterfaceSelector;
};

bool showControlPanel(HWND parent, const ControlPanelState& state, DriverConfig* savedConfig);

} // namespace windows_reac_asio
