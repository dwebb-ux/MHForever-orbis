#include "PspFileSystem.h"
#include "PspNetworkSystem.h"
#include "PspRuntimeBootstrap.h"
#include "interfaces/IAudioSystem.h"
#include "interfaces/IInputSystem.h"
#include "interfaces/ILogger.h"
#include "interfaces/IRenderer.h"

#include "check.hpp"

#include <array>
#include <filesystem>
#include <span>
#include <string>

int main() {
    mhf::runtime::RuntimeContext psp_context;
    mhf::psp::adapters::PspRuntimeBootstrap::Initialize(psp_context);
    MHF_CHECK(psp_context.Contains<mhf::runtime::ILogger>());
    MHF_CHECK(psp_context.Contains<mhf::runtime::IFileSystem>());
    MHF_CHECK(psp_context.Contains<mhf::runtime::INetworkSystem>());
#if defined(MHF_RUNTIME_PSP_HAS_SDL)
    MHF_CHECK(psp_context.Contains<mhf::runtime::IRenderer>());
    MHF_CHECK(psp_context.Contains<mhf::runtime::IInputSystem>());
    MHF_CHECK(psp_context.Contains<mhf::runtime::IAudioSystem>());
#else
    MHF_CHECK(!psp_context.Contains<mhf::runtime::IRenderer>());
    MHF_CHECK(!psp_context.Contains<mhf::runtime::IInputSystem>());
    MHF_CHECK(!psp_context.Contains<mhf::runtime::IAudioSystem>());
#endif
    mhf::psp::adapters::PspRuntimeBootstrap::Shutdown(psp_context);
    MHF_CHECK(!psp_context.Contains<mhf::runtime::ILogger>());
    MHF_CHECK(!psp_context.Contains<mhf::runtime::IFileSystem>());
    MHF_CHECK(!psp_context.Contains<mhf::runtime::IRenderer>());
    MHF_CHECK(!psp_context.Contains<mhf::runtime::IInputSystem>());
    MHF_CHECK(!psp_context.Contains<mhf::runtime::IAudioSystem>());
    MHF_CHECK(!psp_context.Contains<mhf::runtime::INetworkSystem>());

    const std::filesystem::path root = std::filesystem::temp_directory_path() / "mhf_psp_adapter_test";
    std::filesystem::remove_all(root);

    mhf::psp::adapters::PspFileSystem files(root);
    MHF_CHECK(files.CreateDirectory("PSP"));
    const std::array<std::byte, 4> source{
        std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    const auto written = files.WriteFile("PSP/test.bin", source);
    MHF_CHECK(written.error == mhf::runtime::FileError::None);
    MHF_CHECK(written.bytes == source.size());
    MHF_CHECK(files.Exists("PSP/test.bin"));

    std::array<std::byte, 4> destination{};
    const auto read = files.ReadFile("PSP/test.bin", destination);
    MHF_CHECK(read.error == mhf::runtime::FileError::None);
    MHF_CHECK(read.bytes == destination.size());
    MHF_CHECK(destination == source);
    MHF_CHECK(!files.Exists("../outside.bin"));

    mhf::psp::adapters::PspNetworkSystem network("mhf-adapter-test");
    MHF_CHECK(network.Initialize());
    const std::array<std::uint8_t, 3> packet{1, 2, 3};
    MHF_CHECK(network.Send(packet, mhf::runtime::NetworkDelivery::Reliable) ==
             mhf::runtime::NetworkError::None);
    MHF_CHECK(network.Receive().data.empty());
    network.Shutdown();

    std::filesystem::remove_all(root);
    return mhf::test::finish("psp_runtime_adapters");
}
