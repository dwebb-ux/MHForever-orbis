#include "core/RuntimeContext.h"
#include "interfaces/IAudioSystem.h"
#include "interfaces/IFileSystem.h"
#include "interfaces/IInputSystem.h"
#include "interfaces/ILogger.h"
#include "interfaces/INetworkSystem.h"
#include "interfaces/IRenderer.h"

#include "check.hpp"

#include <memory>
#include <stdexcept>
#include <string_view>

namespace {

class TestLogger final : public mhf::runtime::ILogger {
public:
    void Log(mhf::runtime::LogLevel, std::string_view, std::string_view) noexcept override {}
};

} // namespace

int main() {
    mhf::runtime::RuntimeContext context;

    MHF_CHECK(!context.Contains<mhf::runtime::ILogger>());
    MHF_CHECK(!context.Contains<mhf::runtime::IFileSystem>());
    MHF_CHECK(!context.Contains<mhf::runtime::IInputSystem>());
    MHF_CHECK(!context.Contains<mhf::runtime::IAudioSystem>());
    MHF_CHECK(!context.Contains<mhf::runtime::INetworkSystem>());
    MHF_CHECK(!context.Contains<mhf::runtime::IRenderer>());
    MHF_CHECK(context.Get<mhf::runtime::IRenderer>() == nullptr);
    MHF_CHECK(!context.Register<mhf::runtime::ILogger>(static_cast<mhf::runtime::ILogger *>(nullptr)));
    MHF_CHECK(!context.Register<mhf::runtime::ILogger>(
        std::shared_ptr<mhf::runtime::ILogger>{}));
    MHF_CHECK(!context.Register<mhf::runtime::ILogger>(
        std::unique_ptr<TestLogger>{}));
    MHF_CHECK(!context.Contains<mhf::runtime::ILogger>());

    bool missing_service_threw = false;
    try {
        (void)context.Require<mhf::runtime::IRenderer>();
    } catch (const std::logic_error &) {
        missing_service_threw = true;
    }
    MHF_CHECK(missing_service_threw);

    TestLogger logger;
    MHF_CHECK(context.Register<mhf::runtime::ILogger>(&logger));
    MHF_CHECK(context.Get<mhf::runtime::ILogger>() == &logger);
    MHF_CHECK(&context.Require<mhf::runtime::ILogger>() == &logger);
    const mhf::runtime::RuntimeContext &const_context = context;
    MHF_CHECK(const_context.Get<mhf::runtime::ILogger>() == &logger);
    MHF_CHECK(&const_context.Require<mhf::runtime::ILogger>() == &logger);

    MHF_CHECK(context.Register<mhf::runtime::ILogger>(
        std::make_shared<TestLogger>()));
    MHF_CHECK(context.Get<mhf::runtime::ILogger>() != nullptr);

    MHF_CHECK(context.Register<mhf::runtime::ILogger>(
        std::unique_ptr<TestLogger>(new TestLogger())));
    MHF_CHECK(context.Get<mhf::runtime::ILogger>() != nullptr);

    MHF_CHECK(context.Unregister<mhf::runtime::ILogger>());
    MHF_CHECK(!context.Contains<mhf::runtime::ILogger>());

    return mhf::test::finish("runtime_context");
}
