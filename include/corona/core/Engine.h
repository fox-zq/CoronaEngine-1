#pragma once

#include <ResourceManager.h>
#include <corona/core/SystemRegistry.h>
#include <corona/core/detail/EngineKernel.h>
#include <corona/core/detail/SystemHubs.h>
#include <corona/interfaces/ISystem.h>
#include <corona/interfaces/Services.h>
#include <corona/threading/SafeCommandQueue.h>
#include <corona_logger.h>

#include <atomic>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>

namespace Corona {
namespace Core {
class CommandSchedulerService;
class LoggerService;
class ResourceServiceAdapter;
}  // namespace Core

// 全局数据ID生成器
struct DataId {
    using id_type = uint64_t;
    static id_type next();

   private:
    static std::atomic<id_type> counter_;
};

class EngineFacade {
   public:
    static EngineFacade& instance();

    // 初始化/收尾：配置日志并注册默认资源加载器
    void init(const LogConfig& cfg);
    void shutdown();

    // 访问器
    ResourceManager& resources();

    // 系统管理（基于 ISystem）
    template <typename T>
        requires std::is_base_of_v<ISystem, T>
    void register_system() {
        auto& system = kernel().add_system<T>();
        auto* queue_ptr = ensure_system_queue(system.name());
        configure_system(system, queue_ptr);
        CE_LOG_DEBUG("Registered system {}", typeid(T).name());
    }

    template <typename T>
        requires std::is_base_of_v<ISystem, T>
    T& get_system() const {
        return kernel().get_system<T>();
    }

    template <typename T>
        requires std::is_base_of_v<ISystem, T>
    [[nodiscard]] bool has_system() const {
        return kernel().has_system<T>();
    }

    void start_systems();
    void stop_systems();

    // 命令队列（跨线程命令分发）
    SafeCommandQueue& get_queue(const std::string& name) const;
    void add_queue(const std::string& name, std::unique_ptr<SafeCommandQueue> queue);

    EngineKernel& kernel();
    const EngineKernel& kernel() const;

    Interfaces::ServiceLocator& services();
    const Interfaces::ServiceLocator& services() const;

    SystemRegistry& system_registry() { return system_registry_; }
    const SystemRegistry& system_registry() const { return system_registry_; }

    bool adopt_system(std::shared_ptr<ISystem> system);

    // 全局数据缓存中心
    template <typename T>
    SafeDataCache<T>& cache() {
        return data_hub_.get<T>();
    }

   private:
    EngineFacade();
    ~EngineFacade() = default;
    EngineFacade(const EngineFacade&) = delete;
    EngineFacade& operator=(const EngineFacade&) = delete;

    void register_core_services();

   private:
    bool initialized_ = false;
    std::shared_ptr<Interfaces::ServiceLocator> services_{};
    std::unique_ptr<EngineKernel> kernel_{};
    std::shared_ptr<ResourceManager> resource_manager_{};
    std::shared_ptr<Core::ResourceServiceAdapter> resource_service_{};
    std::shared_ptr<Core::LoggerService> logger_service_{};
    std::shared_ptr<Core::CommandSchedulerService> command_scheduler_{};
    DataCacheHub data_hub_{};
    std::unordered_map<std::string, Interfaces::ICommandScheduler::QueueHandle> system_queue_handles_{};
    SystemRegistry system_registry_{};

    Interfaces::ICommandQueue* ensure_system_queue(const char* name);
    void configure_system(ISystem& system, Interfaces::ICommandQueue* queue_ptr);
};
using Engine = EngineFacade;
}  // namespace Corona