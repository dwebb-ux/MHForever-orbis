#include "core/RuntimeBootstrap.h"
#include "interfaces/IAudioSystem.h"
#include "interfaces/IFileSystem.h"
#include "interfaces/IInputSystem.h"
#include "interfaces/ILogger.h"
#include "interfaces/INetworkSystem.h"
#include "interfaces/IRenderer.h"

#include "check.hpp"

#include <memory>
#include <string_view>

namespace {

class TestLogger final : public mhf::runtime::ILogger {
public:
    void Log(mhf::runtime::LogLevel, std::string_view, std::string_view) noexcept override {}
};

} // namespace

int main() {
    mhf::runtime::RuntimeContext context;
    mhf::runtime::RuntimeBootstrap::Initialize(context);

    MHF_CHECK(context.Contains<mhf::runtime::ILogger>());
    MHF_CHECK(context.Contains<mhf::runtime::IFileSystem>());
    MHF_CHECK(context.Contains<mhf::runtime::IInputSystem>());
    MHF_CHECK(context.Contains<mhf::runtime::IAudioSystem>());
    MHF_CHECK(context.Contains<mhf::runtime::INetworkSystem>());
    MHF_CHECK(context.Contains<mhf::runtime::IRenderer>());
    MHF_CHECK(context.Get<mhf::runtime::IRenderer>() == nullptr);

    TestLogger logger;
    MHF_CHECK(context.Register<mhf::runtime::ILogger>(&logger));
    MHF_CHECK(context.Get<mhf::runtime::ILogger>() == &logger);

    MHF_CHECK(context.Register<mhf::runtime::ILogger>(
        std::make_shared<TestLogger>()));
    MHF_CHECK(context.Get<mhf::runtime::ILogger>() != nullptr);

    MHF_CHECK(context.Register<mhf::runtime::ILogger>(
        std::unique_ptr<TestLogger>(new TestLogger())));
    MHF_CHECK(context.Get<mhf::runtime::ILogger>() != nullptr);

    MHF_CHECK(context.Unregister<mhf::runtime::ILogger>());
    MHF_CHECK(!context.Contains<mhf::runtime::ILogger>());

    mhf::runtime::RuntimeContext bootstrapped =
        mhf::runtime::RuntimeBootstrap::Create();
    MHF_CHECK(bootstrapped.Contains<mhf::runtime::IRenderer>());
    mhf::runtime::RuntimeBootstrap::Shutdown(bootstrapped);
    MHF_CHECK(!bootstrapped.Contains<mhf::runtime::IRenderer>());
    return mhf::test::finish("runtime_context");
}
