#pragma once

#include "core/RuntimeContext.h"

namespace mhf::platform::ps4 {

class Ps4RuntimeBootstrap final {
public:
    static void Initialize(runtime::RuntimeContext &context);
    static void Shutdown(runtime::RuntimeContext &context) noexcept;
};

} // namespace mhf::platform::ps4
