#pragma once

#include "core/RuntimeContext.h"

namespace mhf::runtime {

class RuntimeBootstrap {
public:
    static RuntimeContext Create();
    static void Initialize(RuntimeContext &context);
    static void Shutdown(RuntimeContext &context) noexcept;
};

} // namespace mhf::runtime
