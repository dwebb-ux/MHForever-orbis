#include "core/RuntimeContext.h"

namespace mhf::runtime {

void RuntimeContext::Clear() noexcept {
    registry_.Clear();
}

} // namespace mhf::runtime
