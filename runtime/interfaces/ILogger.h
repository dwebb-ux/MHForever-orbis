#pragma once

#include <cstdint>
#include <string_view>

namespace mhf::runtime {

enum class LogLevel : std::uint8_t {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
};

class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void Log(LogLevel level, std::string_view category, std::string_view message) noexcept = 0;
};

} // namespace mhf::runtime
