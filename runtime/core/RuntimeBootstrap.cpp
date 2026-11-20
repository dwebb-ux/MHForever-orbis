#include "core/RuntimeBootstrap.h"

#if defined(MHF_RUNTIME_PLATFORM_PSP)
#include "PspFileSystem.h"
#include "PspNetworkSystem.h"
#if defined(MHF_RUNTIME_PSP_HAS_SDL)
#include "PspDesktopAdapters.h"
#endif
#elif defined(MHF_RUNTIME_PLATFORM_PS4)
#include "Ps4FileSystem.h"
#include "Ps4Input.h"
#include "Ps4Logger.h"
#else
#error "RuntimeBootstrap.cpp must be compiled for a platform backend"
#endif

#include "interfaces/IAudioSystem.h"
#include "interfaces/IFileSystem.h"
#include "interfaces/IInputSystem.h"
#include "interfaces/ILogger.h"
#include "interfaces/INetworkSystem.h"
#include "interfaces/IRenderer.h"

#include <memory>

namespace mhf::runtime {

namespace {

RuntimeContext g_context;

} // namespace

RuntimeContext &RuntimeBootstrap::Context() noexcept {
    return g_context;
}

BootstrapReport RuntimeBootstrap::Initialize() {
    g_context.Clear();

#if defined(MHF_RUNTIME_PLATFORM_PSP)
    g_context.Register<IFileSystem>(std::make_unique<psp::adapters::PspFileSystem>());
    g_context.Register<INetworkSystem>(std::make_unique<psp::adapters::PspNetworkSystem>());
#if defined(MHF_RUNTIME_PSP_HAS_SDL)
    auto renderer = std::make_unique<psp::adapters::PspRenderer>();
    host::SdlHost *host = renderer->Host();
    g_context.Register<IRenderer>(std::move(renderer));
    g_context.Register<IInputSystem>(std::make_unique<psp::adapters::PspInputSystem>(*host));
    g_context.Register<IAudioSystem>(std::make_unique<psp::adapters::PspAudioSystem>(*host));
#endif
#elif defined(MHF_RUNTIME_PLATFORM_PS4)
    g_context.Register<ILogger>(std::make_unique<platform::ps4::Ps4Logger>());
    g_context.Register<IFileSystem>(std::make_unique<platform::ps4::Ps4FileSystem>());
    g_context.Register<IInputSystem>(std::make_unique<platform::ps4::Ps4Input>());
#endif

    return {
        g_context.Contains<ILogger>(),
        g_context.Contains<IFileSystem>(),
        g_context.Contains<IRenderer>(),
        g_context.Contains<IInputSystem>(),
        g_context.Contains<IAudioSystem>(),
        g_context.Contains<INetworkSystem>(),
    };
}

void RuntimeBootstrap::Shutdown() noexcept {
    g_context.Clear();
}

} // namespace mhf::runtime
