#include "core/RuntimeBootstrap.h"

namespace mhf::runtime {

RuntimeContext RuntimeBootstrap::Create() {
    return {};
}

void RuntimeBootstrap::Shutdown(RuntimeContext &context) noexcept {
    context.Clear();
}

} // namespace mhf::runtime
