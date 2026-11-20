#include "PspLogger.h"

#include <cstdio>
#include <mutex>

namespace mhf::psp::adapters {

void PspLogger::Log(runtime::LogLevel level,
                    std::string_view category,
                    std::string_view message) noexcept {
    static_cast<void>(level);

    static std::mutex output_mutex;
    const std::lock_guard lock(output_mutex);

    std::fputc('[', stderr);
    if (!category.empty()) {
        std::fwrite(category.data(), sizeof(char), category.size(), stderr);
    }
    std::fputs("] ", stderr);
    if (!message.empty()) {
        std::fwrite(message.data(), sizeof(char), message.size(), stderr);
    }
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

} // namespace mhf::psp::adapters
