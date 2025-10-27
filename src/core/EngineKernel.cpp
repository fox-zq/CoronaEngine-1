#include <corona/core/detail/EngineKernel.h>

namespace Corona {

EngineKernel::EngineKernel(std::shared_ptr<Interfaces::ServiceLocator> services)
    : services_(std::move(services)) {
    if (!services_) {
        services_ = std::make_shared<Interfaces::ServiceLocator>();
    }
}

Interfaces::ServiceLocator& EngineKernel::services() {
    return *services_;
}

const Interfaces::ServiceLocator& EngineKernel::services() const {
    return *services_;
}

void EngineKernel::start_all() {
    for (auto& system : system_order_) {
        if (system) {
            system->start();
        }
    }
}

void EngineKernel::stop_all() {
    for (auto it = system_order_.rbegin(); it != system_order_.rend(); ++it) {
        if (*it) {
            (*it)->stop();
        }
    }
}

Interfaces::SystemContext EngineKernel::make_context(Interfaces::ICommandQueue* queue,
                                                     DataCacheHub* caches) {
    return Interfaces::SystemContext{*services_, queue, caches};
}

bool EngineKernel::add_system_instance(std::shared_ptr<ISystem> system) {
    if (!system) {
        return false;
    }
    const auto& system_ref = *system;
    const std::type_index key{typeid(system_ref)};
    if (systems_.contains(key)) {
        return false;
    }
    system_order_.push_back(system);
    systems_.emplace(key, std::move(system));
    return true;
}

}  // namespace Corona