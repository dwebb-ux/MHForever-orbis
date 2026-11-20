#include "core/RuntimeBootstrap.h"
#include "core/RuntimeContext.h"
#include "interfaces/IAudioSystem.h"
#include "interfaces/IFileSystem.h"
#include "interfaces/IInputSystem.h"
#include "interfaces/ILogger.h"
#include "interfaces/INetworkSystem.h"
#include "interfaces/IRenderer.h"

#include "check.hpp"

#include <atomic>
#include <barrier>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>

namespace {

class TestLogger final : public mhf::runtime::ILogger {
public:
    void Log(mhf::runtime::LogLevel, std::string_view, std::string_view) noexcept override {}
};

class TrackedLogger final : public mhf::runtime::ILogger {
public:
    explicit TrackedLogger(std::shared_ptr<std::atomic<int>> destruction_count)
        : destruction_count_(std::move(destruction_count)) {}

    ~TrackedLogger() override {
        destruction_count_->fetch_add(1, std::memory_order_relaxed);
    }

    void Log(mhf::runtime::LogLevel, std::string_view, std::string_view) noexcept override {}

private:
    std::shared_ptr<std::atomic<int>> destruction_count_;
};

class RegistryCheckingLogger final : public mhf::runtime::ILogger {
public:
    RegistryCheckingLogger(mhf::runtime::RuntimeContext &context,
                           std::atomic<bool> &destroyed_outside_lock,
                           bool expected_presence)
        : context_(context),
          destroyed_outside_lock_(destroyed_outside_lock),
          expected_presence_(expected_presence) {}

    ~RegistryCheckingLogger() override {
        destroyed_outside_lock_.store(
            context_.Contains<mhf::runtime::ILogger>() == expected_presence_,
            std::memory_order_relaxed);
    }

    void Log(mhf::runtime::LogLevel, std::string_view, std::string_view) noexcept override {}

private:
    mhf::runtime::RuntimeContext &context_;
    std::atomic<bool> &destroyed_outside_lock_;
    bool expected_presence_;
};

} // namespace

int main() {
    mhf::runtime::RuntimeContext created_context = mhf::runtime::RuntimeBootstrap::Create();
    MHF_CHECK(!created_context.Contains<mhf::runtime::ILogger>());
    MHF_CHECK(!created_context.Contains<mhf::runtime::IFileSystem>());
    MHF_CHECK(!created_context.Contains<mhf::runtime::IInputSystem>());
    MHF_CHECK(!created_context.Contains<mhf::runtime::IAudioSystem>());
    MHF_CHECK(!created_context.Contains<mhf::runtime::INetworkSystem>());
    MHF_CHECK(!created_context.Contains<mhf::runtime::IRenderer>());

    TestLogger bootstrap_logger;
    MHF_CHECK(created_context.Register<mhf::runtime::ILogger>(&bootstrap_logger));
    mhf::runtime::RuntimeBootstrap::Shutdown(created_context);
    MHF_CHECK(!created_context.Contains<mhf::runtime::ILogger>());

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
    MHF_CHECK(context.Get<mhf::runtime::ILogger>().get() == &logger);
    MHF_CHECK(context.Require<mhf::runtime::ILogger>().get() == &logger);
    const mhf::runtime::RuntimeContext &const_context = context;
    MHF_CHECK(const_context.Get<mhf::runtime::ILogger>().get() == &logger);
    MHF_CHECK(const_context.Require<mhf::runtime::ILogger>().get() == &logger);

    MHF_CHECK(!context.Register<mhf::runtime::ILogger>(
        static_cast<mhf::runtime::ILogger *>(nullptr)));
    MHF_CHECK(!context.Register<mhf::runtime::ILogger>(
        std::shared_ptr<mhf::runtime::ILogger>{}));
    MHF_CHECK(context.Get<mhf::runtime::ILogger>().get() == &logger);

    MHF_CHECK(context.Register<mhf::runtime::ILogger>(
        std::make_shared<TestLogger>()));
    MHF_CHECK(context.Get<mhf::runtime::ILogger>() != nullptr);

    MHF_CHECK(context.Register<mhf::runtime::ILogger>(
        std::unique_ptr<TestLogger>(new TestLogger())));
    MHF_CHECK(context.Get<mhf::runtime::ILogger>() != nullptr);

    MHF_CHECK(context.Unregister<mhf::runtime::ILogger>());
    MHF_CHECK(!context.Contains<mhf::runtime::ILogger>());
    MHF_CHECK(!context.Unregister<mhf::runtime::ILogger>());

    std::atomic<bool> replacement_destroyed_outside_lock = false;
    MHF_CHECK(context.Register<mhf::runtime::ILogger>(
        std::make_unique<RegistryCheckingLogger>(context, replacement_destroyed_outside_lock, true)));
    MHF_CHECK(context.Register<mhf::runtime::ILogger>(std::make_unique<TestLogger>()));
    MHF_CHECK(replacement_destroyed_outside_lock.load(std::memory_order_relaxed));

    std::atomic<bool> removal_destroyed_outside_lock = false;
    MHF_CHECK(context.Register<mhf::runtime::ILogger>(
        std::make_unique<RegistryCheckingLogger>(context, removal_destroyed_outside_lock, false)));
    MHF_CHECK(context.Unregister<mhf::runtime::ILogger>());
    MHF_CHECK(removal_destroyed_outside_lock.load(std::memory_order_relaxed));

    auto destruction_count = std::make_shared<std::atomic<int>>(0);
    auto first = std::make_unique<TrackedLogger>(destruction_count);
    TrackedLogger *first_address = first.get();
    MHF_CHECK(context.Register<mhf::runtime::ILogger>(std::move(first)));
    MHF_CHECK(!first);

    std::shared_ptr<mhf::runtime::ILogger> first_lease = context.Get<mhf::runtime::ILogger>();
    MHF_CHECK(first_lease.get() == first_address);
    MHF_CHECK(context.Register<mhf::runtime::ILogger>(
        std::make_unique<TrackedLogger>(destruction_count)));
    MHF_CHECK(context.Get<mhf::runtime::ILogger>().get() != first_address);
    MHF_CHECK(destruction_count->load(std::memory_order_relaxed) == 0);

    MHF_CHECK(context.Unregister<mhf::runtime::ILogger>());
    MHF_CHECK(!context.Contains<mhf::runtime::ILogger>());
    MHF_CHECK(destruction_count->load(std::memory_order_relaxed) == 1);
    first_lease.reset();
    MHF_CHECK(destruction_count->load(std::memory_order_relaxed) == 2);

    auto shared_logger = std::make_shared<TestLogger>();
    std::weak_ptr<TestLogger> shared_logger_lifetime = shared_logger;
    MHF_CHECK(context.Register<mhf::runtime::ILogger>(shared_logger));
    shared_logger.reset();
    MHF_CHECK(!shared_logger_lifetime.expired());
    std::shared_ptr<mhf::runtime::ILogger> shared_lease = context.Require<mhf::runtime::ILogger>();
    MHF_CHECK(context.Unregister<mhf::runtime::ILogger>());
    MHF_CHECK(!shared_logger_lifetime.expired());
    shared_lease.reset();
    MHF_CHECK(shared_logger_lifetime.expired());

    std::barrier start(3);
    std::thread writer([&context, &start] {
        start.arrive_and_wait();
        for (int i = 0; i < 1000; ++i) {
            (void)context.Register<mhf::runtime::ILogger>(std::make_unique<TestLogger>());
            if ((i % 3) == 0) {
                (void)context.Unregister<mhf::runtime::ILogger>();
            }
        }
    });
    std::thread reader([&context, &start] {
        start.arrive_and_wait();
        for (int i = 0; i < 1000; ++i) {
            if (const auto service = context.Get<mhf::runtime::ILogger>()) {
                service->Log(mhf::runtime::LogLevel::Info, "test", "concurrent access");
            }
            (void)context.Contains<mhf::runtime::ILogger>();
            try {
                (void)context.Require<mhf::runtime::ILogger>();
            } catch (const std::logic_error &) {
            }
        }
    });
    start.arrive_and_wait();
    writer.join();
    reader.join();

    return mhf::test::finish("runtime_context");
}
