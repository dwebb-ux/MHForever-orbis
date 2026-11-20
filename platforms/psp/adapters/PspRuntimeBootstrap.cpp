#include "PspRuntimeBootstrap.h"

#include "PspFileSystem.h"
#include "PspLogger.h"
#include "PspNetworkSystem.h"
#if defined(MHF_RUNTIME_PSP_HAS_SDL)
#include "PspDesktopAdapters.h"
#endif

#include "interfaces/IAudioSystem.h"
#include "interfaces/IFileSystem.h"
#include "interfaces/IInputSystem.h"
#include "interfaces/ILogger.h"
#include "interfaces/INetworkSystem.h"
#include "interfaces/IRenderer.h"

#include <memory>

namespace mhf::psp::adapters {

void PspRuntimeBootstrap::Initialize(runtime::RuntimeContext &context) {
    context.Register<runtime::ILogger>(std::make_unique<PspLogger>());
    context.Register<runtime::IFileSystem>(std::make_unique<PspFileSystem>());
    context.Register<runtime::INetworkSystem>(std::make_unique<PspNetworkSystem>());

#if defined(MHF_RUNTIME_PSP_HAS_SDL)
    auto renderer = std::make_unique<PspRenderer>();
    host::SdlHost *host = renderer->Host();
    context.Register<runtime::IRenderer>(std::move(renderer));
    context.Register<runtime::IInputSystem>(std::make_unique<PspInputSystem>(*host));
    context.Register<runtime::IAudioSystem>(std::make_unique<PspAudioSystem>(*host));
#endif
}

void PspRuntimeBootstrap::Shutdown(runtime::RuntimeContext &context) noexcept {
    context.Clear();
}

} // namespace mhf::psp::adapters
