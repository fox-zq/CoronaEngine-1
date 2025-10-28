#include <ResourceTypes.h>
#include <corona/systems/RenderingSystem.h>

#include <filesystem>
#include <memory>
#include <corona/core/detail/SystemHubs.h>
#include "Model.h"
#include "Shader.h"
#include "corona/core/Engine.h"

using namespace Corona;

RenderingSystem::RenderingSystem()
    : ThreadedSystem("RenderingSystem") {}

void RenderingSystem::configure(const Interfaces::SystemContext& context) {
    ThreadedSystem::configure(context);
    resource_service_ = services().try_get<Interfaces::IResourceService>();
    scheduler_ = services().try_get<Interfaces::ICommandScheduler>();
    data_cache_hub_ = data_caches();
    if (scheduler_) {
        system_queue_handle_ = scheduler_->get_queue(name());
        if (!system_queue_handle_) {
            system_queue_handle_ = scheduler_->create_queue(name());
        }
    }
}

void RenderingSystem::onStart() {
    init();
    load_shaders_async();
}

void RenderingSystem::load_shaders_async() {
    if (!scheduler_ || !system_queue_handle_ || !resource_service_) {
        CE_LOG_WARN("[RenderingSystem] 无法异步加载shader: 缺少必要的服务");
        return;
    }

    const auto assets_root = (std::filesystem::current_path() / "assets").string();
    auto shaderId = ResourceId::from("shader", assets_root);
    auto queue_handle = system_queue_handle_;
    resource_service_->load_once_async(
        shaderId,
        [queue_handle](const ResourceId&, std::shared_ptr<IResource> r) {
            if (!r) {
                CE_LOG_ERROR("[RenderingSystem] 异步加载shader失败: 资源为空");
                return;
            }

            auto shader = std::dynamic_pointer_cast<Shader>(r);
            if (!shader) {
                CE_LOG_ERROR("[RenderingSystem] 异步加载shader失败: 资源类型错误");
                return;
            }

            queue_handle->enqueue([shader, queue_handle]() {
                auto *render_system = &Engine::instance().get_system<RenderingSystem>();
                render_system->init_shader(shader);
            });
        });
}

void RenderingSystem::onTick() {
    if (auto* queue = command_queue()) {
        int spun = 0;
        while (spun < 128 && !queue->empty()) {
            if (!queue->try_execute()) {
                continue;
            }
            ++spun;
        }
    }

    update_engine();
}

void RenderingSystem::onStop() {
    // 清理资源和状态
}

void RenderingSystem::init() {
    // 初始化渲染尺寸
    gbufferSize = {1920, 1080};

    gbufferPostionImage = HardwareImage(gbufferSize.x, gbufferSize.y, ImageFormat::RGBA16_FLOAT, ImageUsage::StorageImage);
    gbufferBaseColorImage = HardwareImage(gbufferSize.x, gbufferSize.y, ImageFormat::RGBA16_FLOAT, ImageUsage::StorageImage);
    gbufferNormalImage = HardwareImage(gbufferSize.x, gbufferSize.y, ImageFormat::RGBA16_FLOAT, ImageUsage::StorageImage);
    gbufferMotionVectorImage = HardwareImage(gbufferSize.x, gbufferSize.y, ImageFormat::RG32_FLOAT, ImageUsage::StorageImage);

    uniformBuffer = HardwareBuffer(sizeof(UniformBufferObject), BufferUsage::UniformBuffer);
    gbufferUniformBuffer = HardwareBuffer(sizeof(gbufferUniformBufferObject), BufferUsage::UniformBuffer);

    finalOutputImage = HardwareImage(gbufferSize.x, gbufferSize.y, ImageFormat::RGBA16_FLOAT, ImageUsage::StorageImage);
}

void RenderingSystem::init_shader(std::shared_ptr<Shader> shader) {
    rasterizerPipeline = RasterizerPipeline(shader->vertCode, shader->fragCode);
    computePipeline = ComputePipeline(shader->computeCode);
    shaderHasInit = true;
    
    CE_LOG_INFO("[RenderingSystem] 成功初始化shader并设置渲染管线");
}

void RenderingSystem::update_engine() {
    if (!shaderHasInit) {
        return;
    }
    
    gbuffer_pipeline();
    composite_pipeline();
}

void RenderingSystem::gbuffer_pipeline() {
    // uniformBufferObjects.eyePosition = cam.pos;
    // uniformBufferObjects.eyeDir = ktm::normalize(cam.forward);
    // uniformBufferObjects.eyeViewMatrix = ktm::look_at_lh(cam.pos, ktm::normalize(cam.forward), cam.worldUp);
    // uniformBufferObjects.eyeProjMatrix = ktm::perspective_lh(ktm::radians(cam.fov), static_cast<float>(gbufferSize.x) / static_cast<float>(gbufferSize.y), 0.1f, 100.0f);

    gbufferUniformBufferObjects.viewProjMatrix = uniformBufferObjects.eyeProjMatrix * uniformBufferObjects.eyeViewMatrix;
    gbufferUniformBuffer.copyFromData(&gbufferUniformBufferObjects, sizeof(gbufferUniformBufferObjects));

    auto* caches = data_cache_hub_;
    if (!caches) {
        return;
    }
    auto& model_cache = caches->get<Model>();
    model_cache.safe_loop_foreach(model_cache_keys_, [&](std::shared_ptr<Model> model) {
        if (!model) {
            return;
        }

        model->getModelMatrix();
        ktm::fmat4x4 actorMatrix = model->modelMatrix;
        rasterizerPipeline["pushConsts.modelMatrix"] = actorMatrix;

        rasterizerPipeline["pushConsts.uniformBufferIndex"] = gbufferUniformBuffer.storeDescriptor();

        rasterizerPipeline["gbufferPostion"] = gbufferPostionImage;
        rasterizerPipeline["gbufferBaseColor"] = gbufferBaseColorImage;
        rasterizerPipeline["gbufferNormal"] = gbufferNormalImage;
        rasterizerPipeline["gbufferMotionVector"] = gbufferMotionVectorImage;

        for (auto& m : model->meshes) {
            // 录制与提交绘制命令
            // if (shaderHasInit) {
            //     executor(HardwareExecutor::ExecutorType::Graphics)
            //         << rasterizerPipeline(gbufferSize.x, gbufferSize.y) << rasterizerPipeline.record(m.meshDevice->indexBuffer)
            //         << executor.commit();
            // }
        }
    });
}

void RenderingSystem::composite_pipeline(ktm::fvec3 sunDir) {
    computePipeline["pushConsts.gbufferSize"] = gbufferSize;
    computePipeline["pushConsts.gbufferPostionImage"] = gbufferPostionImage.storeDescriptor();
    computePipeline["pushConsts.gbufferBaseColorImage"] = gbufferBaseColorImage.storeDescriptor();
    computePipeline["pushConsts.gbufferNormalImage"] = gbufferNormalImage.storeDescriptor();
    computePipeline["pushConsts.gbufferDepthImage"] = rasterizerPipeline.getDepthImage().storeDescriptor();

    computePipeline["pushConsts.finalOutputImage"] = finalOutputImage.storeDescriptor();

    computePipeline["pushConsts.sun_dir"] = ktm::normalize(sunDir);
    computePipeline["pushConsts.lightColor"] = ktm::fvec3(23.47f, 21.31f, 20.79f);

    uniformBuffer.copyFromData(&uniformBufferObjects, sizeof(uniformBufferObjects));
    computePipeline["pushConsts.uniformBufferIndex"] = uniformBuffer.storeDescriptor();

    // 提交计算管线命令
    if (shaderHasInit) {
        executor
            // << rasterizerPipeline(1920, 1080)
            << computePipeline(1920 / 8, 1080 / 8, 1)
            << executor.commit();
    }
}

void RenderingSystem::watch_model(uint64_t id) {
    model_cache_keys_.insert(id);
}

void RenderingSystem::unwatch_model(uint64_t id) {
    model_cache_keys_.erase(id);
}