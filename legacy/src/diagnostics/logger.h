#pragma once

#include <string>

namespace windows_reac_asio {

class Logger {
public:
    static void debug(const char* message) noexcept;
    static void debug(const std::string& message) noexcept;
};

} // namespace windows_reac_asio
