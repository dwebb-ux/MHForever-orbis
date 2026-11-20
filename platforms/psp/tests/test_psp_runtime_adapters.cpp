#include "PspFileSystem.h"
#include "PspNetworkSystem.h"
#include "core/RuntimeBootstrap.h"
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
    const mhf::runtime::BootstrapReport bootstrap = mhf::runtime::RuntimeBootstrap::Initialize();
    mhf::runtime::RuntimeContext &context = mhf::runtime::RuntimeBootstrap::Context();
    MHF_CHECK(bootstrap.file_system);
    MHF_CHECK(bootstrap.network);
    MHF_CHECK(context.Get<mhf::runtime::IFileSystem>() != nullptr);
    MHF_CHECK(context.Get<mhf::runtime::INetworkSystem>() != nullptr);
    MHF_CHECK(context.Contains<mhf::runtime::IFileSystem>() == bootstrap.file_system);
    MHF_CHECK(context.Contains<mhf::runtime::INetworkSystem>() == bootstrap.network);
    MHF_CHECK(context.Contains<mhf::runtime::ILogger>() == bootstrap.logger);
    MHF_CHECK(context.Contains<mhf::runtime::IRenderer>() == bootstrap.renderer);
    MHF_CHECK(context.Contains<mhf::runtime::IInputSystem>() == bootstrap.input);
    MHF_CHECK(context.Contains<mhf::runtime::IAudioSystem>() == bootstrap.audio);
    MHF_CHECK((context.Get<mhf::runtime::ILogger>() != nullptr) == bootstrap.logger);
    MHF_CHECK((context.Get<mhf::runtime::IRenderer>() != nullptr) == bootstrap.renderer);
    MHF_CHECK((context.Get<mhf::runtime::IInputSystem>() != nullptr) == bootstrap.input);
    MHF_CHECK((context.Get<mhf::runtime::IAudioSystem>() != nullptr) == bootstrap.audio);
    mhf::runtime::RuntimeBootstrap::Shutdown();

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
