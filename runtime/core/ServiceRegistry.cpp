#include "core/ServiceRegistry.h"

namespace mhf::runtime {

ServiceRegistry::ServiceRegistry(ServiceRegistry &&other) noexcept {
    std::unique_lock source_lock(other.mutex_);
    services_ = std::move(other.services_);
}

ServiceRegistry &ServiceRegistry::operator=(ServiceRegistry &&other) noexcept {
    if (this != &other) {
        std::unordered_map<std::type_index, std::shared_ptr<void>> displaced;
        {
            std::scoped_lock lock(mutex_, other.mutex_);
            displaced.swap(services_);
            services_.swap(other.services_);
        }
    }
    return *this;
}

bool ServiceRegistry::Register(std::type_index type, std::shared_ptr<void> service) {
    if (!service) {
        return false;
    }
    {
        std::unique_lock lock(mutex_);
        const auto found = services_.find(type);
        if (found == services_.end()) {
            services_.emplace(type, std::move(service));
        } else {
            found->second.swap(service);
        }
    }
    return true;
}

bool ServiceRegistry::Unregister(std::type_index type) {
    std::shared_ptr<void> removed;
    {
        std::unique_lock lock(mutex_);
        const auto found = services_.find(type);
        if (found == services_.end()) {
            return false;
        }
        removed = std::move(found->second);
        services_.erase(found);
    }
    return true;
}

void ServiceRegistry::Clear() noexcept {
    std::unordered_map<std::type_index, std::shared_ptr<void>> removed;
    {
        std::unique_lock lock(mutex_);
        removed.swap(services_);
    }
}

} // namespace mhf::runtime
