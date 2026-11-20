#include "core/RuntimeContext.h"

namespace mhf::runtime {

void RuntimeContext::Clear() noexcept {
    registry_.Clear();
}

ServiceRegistry &RuntimeContext::Services() noexcept {
    return registry_;
}

const ServiceRegistry &RuntimeContext::Services() const noexcept {
    return registry_;
}

} // namespace mhf::runtime
