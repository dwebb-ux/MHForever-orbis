#include "core/RuntimeBootstrap.h"

#include "interfaces/IAudioSystem.h"
#include "interfaces/IFileSystem.h"
#include "interfaces/IInputSystem.h"
#include "interfaces/ILogger.h"
#include "interfaces/INetworkSystem.h"
#include "interfaces/IRenderer.h"

namespace mhf::runtime {

RuntimeContext RuntimeBootstrap::Create() {
    RuntimeContext context;
    Initialize(context);
    return context;
}

void RuntimeBootstrap::Initialize(RuntimeContext &context) {
    context.Register<ILogger>();
    context.Register<IFileSystem>();
    context.Register<IInputSystem>();
    context.Register<IAudioSystem>();
    context.Register<INetworkSystem>();
    context.Register<IRenderer>();
}

void RuntimeBootstrap::Shutdown(RuntimeContext &context) noexcept {
    context.Clear();
}

} // namespace mhf::runtime
