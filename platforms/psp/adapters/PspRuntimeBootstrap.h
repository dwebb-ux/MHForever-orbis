#pragma once

#include "core/RuntimeContext.h"

namespace mhf::psp::adapters {

class PspRuntimeBootstrap final {
public:
    static void Initialize(runtime::RuntimeContext &context);
    static void Shutdown(runtime::RuntimeContext &context) noexcept;
};

} // namespace mhf::psp::adapters
