#include <corona/core/Engine.h>

#include <stdexcept>
#include <string>
#include <utility>

#include "Model.h"
#include "Shader.h"
#include "services/CommandSchedulerService.h"
#include "services/LoggerService.h"
#include "services/ResourceServiceAdapter.h"

namespace Corona {

EngineFacade::EngineFacade()
    : services_(std::make_shared<Interfaces::ServiceLocator>()),
      kernel_(std::make_unique<EngineKernel>(services_)),
      resource_manager_(nullptr),
      resource_service_(std::make_shared<Core::ResourceServiceAdapter>(resource_manager_)),
      logger_service_(std::make_shared<Core::LoggerService>()),
      command_scheduler_(std::make_shared<Core::CommandSchedulerService>()) {
    register_core_services();
}

EngineFacade& EngineFacade::instance() {
    static EngineFacade inst;
    return inst;
}

std::atomic<DataId::id_type> DataId::counter_ = 0;

DataId::id_type DataId::next() {
    return counter_.fetch_add(1, std::memory_order_relaxed);
}

void EngineFacade::register_core_services() {
    if (!services_) {
        services_ = std::make_shared<Interfaces::ServiceLocator>();
    }
    if (!kernel_) {
        kernel_ = std::make_unique<EngineKernel>(services_);
    }
    if (!logger_service_) {
        logger_service_ = std::make_shared<Core::LoggerService>();
    }
    services_->register_service<Interfaces::ILogger>(logger_service_);

    if (!command_scheduler_) {
        command_scheduler_ = std::make_shared<Core::CommandSchedulerService>();
    }
    services_->register_service<Interfaces::ICommandScheduler>(command_scheduler_);

    if (!resource_service_) {
        resource_service_ = std::make_shared<Core::ResourceServiceAdapter>(resource_manager_);
    } else {
        resource_service_->set_manager(resource_manager_);
    }
    services_->register_service<Interfaces::IResourceService>(resource_service_);
}

void EngineFacade::init(const LogConfig& cfg) {
    if (initialized_) {
        return;
    }

    Logger::init(cfg);

    if (!resource_manager_) {
        resource_manager_ = std::make_shared<ResourceManager>();
    }
    resource_service_->set_manager(resource_manager_);
    register_core_services();

    resource_manager_->register_loader(std::make_shared<ModelLoader>());
    resource_manager_->register_loader(std::make_shared<ShaderLoader>());

    initialized_ = true;
}

void EngineFacade::shutdown() {
    if (!initialized_) {
        return;
    }

    stop_systems();

    if (command_scheduler_) {
        command_scheduler_->clear();
    }

    system_queue_handles_.clear();

    if (resource_manager_) {
        resource_manager_->wait();
        resource_manager_->clear();
    }

    resource_service_->set_manager(nullptr);
    resource_manager_.reset();

    Logger::shutdown();
    initialized_ = false;
}

ResourceManager& EngineFacade::resources() {
    if (!resource_manager_) {
        throw std::runtime_error("Resource manager not initialized");
    }
    return *resource_manager_;
}

void EngineFacade::start_systems() {
    if (!kernel_) {
        return;
    }
    kernel_->start_all();
}

void EngineFacade::stop_systems() {
    if (!kernel_) {
        return;
    }
    kernel_->stop_all();
}

SafeCommandQueue& EngineFacade::get_queue(const std::string& name) const {
    if (!command_scheduler_) {
        throw std::runtime_error("Command scheduler not initialized");
    }
    try {
        return command_scheduler_->require_queue(name);
    } catch (const std::exception&) {
        CE_LOG_CRITICAL("Command queue not found: {}", name);
        throw;
    }
}

void EngineFacade::add_queue(const std::string& name, std::unique_ptr<SafeCommandQueue> queue) {
    if (!queue) {
        CE_LOG_ERROR("Cannot add null command queue for '{}'", name);
        return;
    }
    if (!command_scheduler_) {
        command_scheduler_ = std::make_shared<Core::CommandSchedulerService>();
        services_->register_service<Interfaces::ICommandScheduler>(command_scheduler_);
    }
    if (command_scheduler_->contains(name)) {
        CE_LOG_WARN("Cannot add command queue for '{}' twice", name);
        return;
    }
    command_scheduler_->adopt_queue(name, std::shared_ptr<SafeCommandQueue>(std::move(queue)));
    CE_LOG_DEBUG("Added command queue for '{}'", name);
}

EngineKernel& EngineFacade::kernel() {
    if (!kernel_) {
        kernel_ = std::make_unique<EngineKernel>(services_);
    }
    return *kernel_;
}

const EngineKernel& EngineFacade::kernel() const {
    if (!kernel_) {
        throw std::runtime_error("Engine kernel not initialized");
    }
    return *kernel_;
}

Interfaces::ServiceLocator& EngineFacade::services() {
    if (!services_) {
        services_ = std::make_shared<Interfaces::ServiceLocator>();
    }
    return *services_;
}

const Interfaces::ServiceLocator& EngineFacade::services() const {
    if (!services_) {
        throw std::runtime_error("Service locator not initialized");
    }
    return *services_;
}

bool EngineFacade::adopt_system(std::shared_ptr<ISystem> system) {
    if (!system) {
        CE_LOG_WARN("Cannot adopt null system instance");
        return false;
    }

    auto* raw = system.get();
    if (!kernel().add_system_instance(std::move(system))) {
        CE_LOG_WARN("System '{}' is already registered", raw ? raw->name() : "<unnamed>");
        return false;
    }

    auto* queue_ptr = ensure_system_queue(raw ? raw->name() : "");
    configure_system(*raw, queue_ptr);
    CE_LOG_DEBUG("Adopted system {}", raw ? raw->name() : "<unnamed>");
    return true;
}

Interfaces::ICommandQueue* EngineFacade::ensure_system_queue(const char* name) {
    if (!name || *name == '\0') {
        return nullptr;
    }

    auto scheduler = services().try_get<Interfaces::ICommandScheduler>();
    if (!scheduler) {
        CE_LOG_WARN("Command scheduler service unavailable while configuring system '{}'", name);
        return nullptr;
    }

    std::string key{name};
    auto handle_it = system_queue_handles_.find(key);
    if (handle_it == system_queue_handles_.end() || !(handle_it->second)) {
        Interfaces::ICommandScheduler::QueueHandle handle;
        if (scheduler->contains(key)) {
            handle = scheduler->get_queue(key);
            if (!handle) {
                CE_LOG_ERROR("Existing command queue '{}' could not be retrieved", key);
                return nullptr;
            }
        } else {
            handle = scheduler->create_queue(key);
            if (!handle) {
                CE_LOG_ERROR("Failed to create command queue '{}'", key);
                return nullptr;
            }
        }
        handle_it = system_queue_handles_.emplace(key, std::move(handle)).first;
    }
    return handle_it->second ? handle_it->second.get() : nullptr;
}

void EngineFacade::configure_system(ISystem& system, Interfaces::ICommandQueue* queue_ptr) {
    auto context = kernel().make_context(queue_ptr, &data_hub_);
    system.configure(context);
}

}  // namespace Corona