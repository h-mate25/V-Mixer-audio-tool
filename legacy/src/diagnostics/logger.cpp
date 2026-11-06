#include "logger.h"

#include <Windows.h>

namespace windows_reac_asio {

void Logger::debug(const char* message) noexcept {
    if (!message) {
        return;
    }
    OutputDebugStringA("[WindowsReacASIO] ");
    OutputDebugStringA(message);
    OutputDebugStringA("\n");
}

void Logger::debug(const std::string& message) noexcept {
    debug(message.c_str());
}

} // namespace windows_reac_asio
