#pragma once

#include "core/RuntimeContext.h"

#include <array>
#include <cstddef>

namespace mhf::runtime {

struct BootstrapReport {
    bool logger = false;
    bool file_system = false;
    bool renderer = false;
    bool input = false;
    bool audio = false;
    bool network = false;

    [[nodiscard]] bool Complete() const noexcept {
        return logger && file_system && renderer && input && audio && network;
    }

    [[nodiscard]] std::array<bool, 6> Availability() const noexcept {
        return {logger, file_system, renderer, input, audio, network};
    }
};

class RuntimeBootstrap {
public:
    static BootstrapReport Initialize();
    [[nodiscard]] static RuntimeContext &Context() noexcept;
    static void Shutdown() noexcept;
};

} // namespace mhf::runtime
