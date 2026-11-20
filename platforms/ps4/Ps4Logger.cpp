#include "Ps4Logger.h"

#include <cstdio>

namespace mhf::platform::ps4 {

void Ps4Logger::WritePrefix(runtime::LogLevel level) noexcept {
    switch (level) {
    case runtime::LogLevel::Trace:
        std::fputs("[Trace]", stderr);
        break;
    case runtime::LogLevel::Debug:
        std::fputs("[Debug]", stderr);
        break;
    case runtime::LogLevel::Info:
        std::fputs("[Info]", stderr);
        break;
    case runtime::LogLevel::Warning:
        std::fputs("[Warning]", stderr);
        break;
    case runtime::LogLevel::Error:
        std::fputs("[Error]", stderr);
        break;
    }
}

void Ps4Logger::Log(runtime::LogLevel level, std::string_view category,
                    std::string_view message) noexcept {
    WritePrefix(level);
    std::fputs(" [", stderr);
    std::fwrite(category.data(), 1, category.size(), stderr);
    std::fputs("] ", stderr);
    std::fwrite(message.data(), 1, message.size(), stderr);
    std::fputc('\n', stderr);
}

} // namespace mhf::platform::ps4
