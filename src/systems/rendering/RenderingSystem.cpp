#include <ResourceTypes.h>
#include <corona/systems/RenderingSystem.h>

#include <filesystem>
#include <memory>
#include <corona/core/detail/SystemHubs.h>
#include "Model.h"
#include "Shader.h"

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
}

void RenderingSystem::update_engine() {
    if (!shaderHasInit) {
        return;
    }

    // 简单的渲染测试，使用默认相机参数
    CameraSnapshot defaultCamera;
    defaultCamera.fov = 60.0f;
    defaultCamera.pos = ktm::fvec3(0.0f, 0.0f, 5.0f);
    defaultCamera.forward = ktm::fvec3(0.0f, 0.0f, -1.0f);
    defaultCamera.worldUp = ktm::fvec3(0.0f, 1.0f, 0.0f);
    
    ktm::fvec3 defaultSunDir = ktm::fvec3(-1.0f, -1.0f, -1.0f);
    
    gbuffer_pipeline(defaultCamera);
    composite_pipeline(defaultSunDir);
}

void RenderingSystem::gbuffer_pipeline(const CameraSnapshot& cam) {
    uniformBufferObjects.eyePosition = cam.pos;
    uniformBufferObjects.eyeDir = ktm::normalize(cam.forward);
    uniformBufferObjects.eyeViewMatrix = ktm::look_at_lh(cam.pos, ktm::normalize(cam.forward), cam.worldUp);
    uniformBufferObjects.eyeProjMatrix = ktm::perspective_lh(ktm::radians(cam.fov), static_cast<float>(gbufferSize.x) / static_cast<float>(gbufferSize.y), 0.1f, 100.0f);

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
            // 录制与提交绘制命令（按需开启）
            // executor(HardwareExecutor::ExecutorType::Graphics)
            //     << rasterizerPipeline(gbufferSize.x, gbufferSize.y) << rasterizerPipeline.record(m.meshDevice->indexBuffer)
            //     << executor.commit();
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

    // executor(HardwareExecutor::ExecutorType::Graphics)
    //     << computePipeline(1920 / 8, 1080 / 8, 1)
    //     << executor.commit();
}

void RenderingSystem::watch_model(uint64_t id) {
    model_cache_keys_.insert(id);
}

void RenderingSystem::unwatch_model(uint64_t id) {
    model_cache_keys_.erase(id);
}