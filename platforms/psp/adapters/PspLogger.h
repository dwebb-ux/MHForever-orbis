#pragma once

#include "interfaces/ILogger.h"

namespace mhf::psp::adapters {

class PspLogger final : public runtime::ILogger {
public:
    void Log(runtime::LogLevel level,
             std::string_view category,
             std::string_view message) noexcept override;
};

} // namespace mhf::psp::adapters
