#pragma once

#include <corona/interfaces/ISystem.h>
#include <corona/interfaces/ServiceLocator.h>
#include <corona/interfaces/SystemContext.h>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Corona {

class DataCacheHub;

class EngineKernel {
   public:
    explicit EngineKernel(std::shared_ptr<Interfaces::ServiceLocator> services = nullptr);

    Interfaces::ServiceLocator& services();
    const Interfaces::ServiceLocator& services() const;

    template <typename TSystem, typename... Args>
    TSystem& add_system(Args&&... args) {
        const std::type_index key{typeid(TSystem)};
        if (auto it = systems_.find(key); it != systems_.end()) {
            return *std::static_pointer_cast<TSystem>(it->second);
        }
        auto instance = std::make_shared<TSystem>(std::forward<Args>(args)...);
        system_order_.push_back(instance);
        systems_.emplace(key, instance);
        return *instance;
    }

    template <typename TSystem>
    TSystem& get_system() const {
        const std::type_index key{typeid(TSystem)};
        if (auto it = systems_.find(key); it != systems_.end()) {
            return *std::static_pointer_cast<TSystem>(it->second);
        }
        throw std::runtime_error("System not registered");
    }

    template <typename TSystem>
    [[nodiscard]] bool has_system() const {
        const std::type_index key{typeid(TSystem)};
        return systems_.contains(key);
    }

    void start_all();
    void stop_all();

    [[nodiscard]] std::size_t system_count() const { return systems_.size(); }

    Interfaces::SystemContext make_context(Interfaces::ICommandQueue* queue = nullptr,
                                           DataCacheHub* caches = nullptr);

    bool add_system_instance(std::shared_ptr<ISystem> system);

   private:
    std::shared_ptr<Interfaces::ServiceLocator> services_;
    std::unordered_map<std::type_index, std::shared_ptr<ISystem>> systems_;
    std::vector<std::shared_ptr<ISystem>> system_order_;
};

}  // namespace Corona