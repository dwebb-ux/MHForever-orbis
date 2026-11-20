#pragma once

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>

namespace mhf::runtime {

class ServiceRegistry {
public:
    ServiceRegistry() = default;
    ServiceRegistry(const ServiceRegistry &) = delete;
    ServiceRegistry &operator=(const ServiceRegistry &) = delete;
    ServiceRegistry(ServiceRegistry &&) noexcept;
    ServiceRegistry &operator=(ServiceRegistry &&) noexcept;

    // Raw-pointer registrations are non-owning; keep the object alive until it is unregistered and all handles are released.
    template <typename T>
    bool Register(T *service) {
        if (service == nullptr) {
            return false;
        }
        return Register(std::type_index(typeid(T)),
                        std::shared_ptr<void>(service, [](void *) noexcept {}));
    }

    template <typename T, typename U>
    bool Register(std::unique_ptr<U> service) {
        static_assert(std::is_base_of_v<T, U>, "Registered implementation must derive from service interface");
        std::shared_ptr<T> owned = std::shared_ptr<U>(std::move(service));
        return Register<T>(std::move(owned));
    }

    template <typename T>
    bool Register(std::shared_ptr<T> service) {
        if (!service) {
            return false;
        }
        return Register(std::type_index(typeid(T)), std::shared_ptr<void>(std::move(service)));
    }

    template <typename T>
    std::shared_ptr<T> Get() noexcept {
        std::shared_lock lock(mutex_);
        const auto found = services_.find(std::type_index(typeid(T)));
        if (found == services_.end()) {
            return {};
        }
        return std::static_pointer_cast<T>(found->second);
    }

    template <typename T>
    std::shared_ptr<const T> Get() const noexcept {
        std::shared_lock lock(mutex_);
        const auto found = services_.find(std::type_index(typeid(T)));
        if (found == services_.end()) {
            return {};
        }
        return std::static_pointer_cast<const T>(found->second);
    }

    template <typename T>
    [[nodiscard]] bool Contains() const noexcept {
        std::shared_lock lock(mutex_);
        return services_.find(std::type_index(typeid(T))) != services_.end();
    }

    template <typename T>
    bool Unregister() {
        return Unregister(std::type_index(typeid(T)));
    }

    void Clear() noexcept;

private:
    bool Register(std::type_index type, std::shared_ptr<void> service);
    bool Unregister(std::type_index type);

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::type_index, std::shared_ptr<void>> services_;
};

} // namespace mhf::runtime
