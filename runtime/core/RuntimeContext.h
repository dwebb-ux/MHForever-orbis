#pragma once

#include "core/ServiceRegistry.h"

#include <memory>
#include <stdexcept>
#include <type_traits>

namespace mhf::runtime {

class RuntimeContext {
public:
    RuntimeContext() = default;
    RuntimeContext(const RuntimeContext &) = delete;
    RuntimeContext &operator=(const RuntimeContext &) = delete;
    RuntimeContext(RuntimeContext &&) noexcept = default;
    RuntimeContext &operator=(RuntimeContext &&) noexcept = default;

    template <typename T>
    bool Register(T *service) {
        return registry_.Register<T>(service);
    }

    template <typename T, typename U>
    bool Register(std::unique_ptr<U> service) {
        static_assert(std::is_base_of_v<T, U>, "Registered implementation must derive from service interface");
        std::shared_ptr<T> owned = std::shared_ptr<U>(std::move(service));
        return registry_.Register<T>(std::move(owned));
    }

    template <typename T>
    bool Register(std::shared_ptr<T> service) {
        return registry_.Register<T>(std::move(service));
    }

    template <typename T>
    std::shared_ptr<T> Get() noexcept {
        return registry_.Get<T>();
    }

    template <typename T>
    std::shared_ptr<const T> Get() const noexcept {
        return registry_.Get<T>();
    }

    template <typename T>
    [[nodiscard]] bool Contains() const noexcept {
        return registry_.Contains<T>();
    }

    template <typename T>
    std::shared_ptr<T> Require() {
        if (std::shared_ptr<T> service = Get<T>(); service) {
            return service;
        }
        throw std::logic_error("Required runtime service is not registered");
    }

    template <typename T>
    std::shared_ptr<const T> Require() const {
        if (std::shared_ptr<const T> service = Get<T>(); service) {
            return service;
        }
        throw std::logic_error("Required runtime service is not registered");
    }

    template <typename T>
    bool Unregister() {
        return registry_.Unregister<T>();
    }

    void Clear() noexcept;

private:
    ServiceRegistry registry_;
};

} // namespace mhf::runtime
