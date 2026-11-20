#include "core/ServiceRegistry.h"

namespace mhf::runtime {

ServiceRegistry::ServiceRegistry(ServiceRegistry &&other) noexcept {
    std::unique_lock source_lock(other.mutex_);
    services_ = std::move(other.services_);
}

ServiceRegistry &ServiceRegistry::operator=(ServiceRegistry &&other) noexcept {
    if (this != &other) {
        std::unique_lock destination_lock(mutex_);
        std::unique_lock source_lock(other.mutex_);
        services_ = std::move(other.services_);
    }
    return *this;
}

bool ServiceRegistry::Register(std::type_index type, std::shared_ptr<void> service) {
    std::unique_lock lock(mutex_);
    services_[type] = std::move(service);
    return true;
}

void ServiceRegistry::Clear() noexcept {
    std::unique_lock lock(mutex_);
    services_.clear();
}

} // namespace mhf::runtime
