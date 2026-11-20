#include "Ps4RuntimeBootstrap.h"

#include "Ps4FileSystem.h"
#include "Ps4Input.h"
#include "Ps4Logger.h"

#include <memory>

namespace mhf::platform::ps4 {

void Ps4RuntimeBootstrap::Initialize(runtime::RuntimeContext &context) {
    context.Register<runtime::ILogger>(std::make_unique<Ps4Logger>());
    context.Register<runtime::IFileSystem>(std::make_unique<Ps4FileSystem>());
    context.Register<runtime::IInputSystem>(std::make_unique<Ps4Input>());
}

void Ps4RuntimeBootstrap::Shutdown(runtime::RuntimeContext &context) noexcept {
    context.Clear();
}

} // namespace mhf::platform::ps4
