#pragma once

#include "interfaces/ILogger.h"

#include <cstdio>

namespace mhf::platform::ps4 {

class Ps4Logger final : public runtime::ILogger {
public:
    void Log(runtime::LogLevel level, std::string_view category, std::string_view message) noexcept override;

private:
    static void WritePrefix(runtime::LogLevel level) noexcept;
};

} // namespace mhf::platform::ps4
