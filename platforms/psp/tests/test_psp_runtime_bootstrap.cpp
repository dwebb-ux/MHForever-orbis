#include "PspRuntimeBootstrap.h"

#include "interfaces/IAudioSystem.h"
#include "interfaces/IFileSystem.h"
#include "interfaces/IInputSystem.h"
#include "interfaces/ILogger.h"
#include "interfaces/INetworkSystem.h"
#include "interfaces/IRenderer.h"

#include "check.hpp"

int main() {
    mhf::runtime::RuntimeContext context;
    mhf::psp::adapters::PspRuntimeBootstrap::Initialize(context);

    MHF_CHECK(context.Contains<mhf::runtime::ILogger>());
    MHF_CHECK(context.Contains<mhf::runtime::IFileSystem>());
    MHF_CHECK(context.Contains<mhf::runtime::INetworkSystem>());
    MHF_CHECK(context.Contains<mhf::runtime::IRenderer>());
    MHF_CHECK(context.Contains<mhf::runtime::IInputSystem>());
    MHF_CHECK(context.Contains<mhf::runtime::IAudioSystem>());

    const auto logger = context.Require<mhf::runtime::ILogger>();
    const auto file_system = context.Require<mhf::runtime::IFileSystem>();
    const auto network = context.Require<mhf::runtime::INetworkSystem>();
    const auto renderer = context.Require<mhf::runtime::IRenderer>();
    const auto input = context.Require<mhf::runtime::IInputSystem>();
    const auto audio = context.Require<mhf::runtime::IAudioSystem>();

    MHF_CHECK(logger != nullptr);
    MHF_CHECK(file_system != nullptr);
    MHF_CHECK(network != nullptr);
    MHF_CHECK(renderer != nullptr);
    MHF_CHECK(input != nullptr);
    MHF_CHECK(audio != nullptr);
    MHF_CHECK(logger == context.Get<mhf::runtime::ILogger>());
    MHF_CHECK(file_system == context.Get<mhf::runtime::IFileSystem>());
    MHF_CHECK(network == context.Get<mhf::runtime::INetworkSystem>());
    MHF_CHECK(renderer == context.Get<mhf::runtime::IRenderer>());
    MHF_CHECK(input == context.Get<mhf::runtime::IInputSystem>());
    MHF_CHECK(audio == context.Get<mhf::runtime::IAudioSystem>());

    mhf::psp::adapters::PspRuntimeBootstrap::Shutdown(context);

    MHF_CHECK(!context.Contains<mhf::runtime::ILogger>());
    MHF_CHECK(!context.Contains<mhf::runtime::IFileSystem>());
    MHF_CHECK(!context.Contains<mhf::runtime::INetworkSystem>());
    MHF_CHECK(!context.Contains<mhf::runtime::IRenderer>());
    MHF_CHECK(!context.Contains<mhf::runtime::IInputSystem>());
    MHF_CHECK(!context.Contains<mhf::runtime::IAudioSystem>());

    return mhf::test::finish("psp_runtime_bootstrap");
}
