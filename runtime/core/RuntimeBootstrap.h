#pragma once

#include "core/RuntimeContext.h"

namespace mhf::runtime {

class RuntimeBootstrap {
public:
    [[nodiscard]] static RuntimeContext Create();
    static void Shutdown(RuntimeContext &context) noexcept;
};

} // namespace mhf::runtime
