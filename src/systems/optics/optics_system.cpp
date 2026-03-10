#include <corona/events/display_system_events.h>
#include <corona/events/optics_system_events.h>
#include <corona/kernel/core/i_logger.h>
#include <corona/kernel/event/i_event_bus.h>
#include <corona/kernel/event/i_event_stream.h>
#include <corona/resource/resource_manager.h>
#include <corona/shared_data_hub.h>
#include <corona/systems/optics/optics_system.h>

#include <filesystem>

#include "corona/resource/types/text.h"
#include "hardware.h"

#undef CORONA_ENABLE_VISION

#ifdef CORONA_ENABLE_VISION
#include "base/import/importer.h"
#include "base/mgr/pipeline.h"
#include "rhi/context.h"
#endif

namespace
{
    Corona::Resource::TResourceID load_shader(const std::filesystem::path& shader_path)
    {
        auto shader = Corona::Resource::ResourceManager::get_instance().import_sync(shader_path);
        return shader;
    }

#ifdef CORONA_ENABLE_VISION
    HardwareBuffer importedViewBuffer;
    HardwareImage importedViewImage;
    SP<vision::Pipeline> renderPipeline;
    vision::Device visionDevice = RHIContext::instance().create_device("cuda");
#endif
} // namespace

namespace Corona::Systems
{
    OpticsSystem::OpticsSystem()
    {
        set_target_fps(60);
    }

    OpticsSystem::~OpticsSystem() = default;

    bool OpticsSystem::initialize(Kernel::ISystemContext* ctx)
    {
        {
#ifdef CORONA_ENABLE_VISION
            visionDevice.init_rtx();
            vision::Global::instance().set_device(&visionDevice);
            vision::Global::instance().set_scene_path("E:\\CoronaTestScenes\\test_vision\\render_scene\\kitchen");
            auto str = "E:\\CoronaTestScenes\\test_vision\\render_scene\\kitchen\\vision_scene.json";
            renderPipeline = vision::Importer::import_scene(str);
            renderPipeline->init();
            renderPipeline->prepare();
            renderPipeline->display(1 / 30);

            uint2 imageSize = renderPipeline->frame_buffer()->raytracing_resolution();
            importedViewImage = HardwareImage(imageSize.x, imageSize.y, ImageFormat::RGBA32_FLOAT,
                                              ImageUsage::StorageImage);

            RegistrableBuffer<float4>* cudaViewBuffer = &renderPipeline->frame_buffer()->view_buffer();
            uint64_t viewBufferHandleWin = visionDevice.export_handle(cudaViewBuffer->handle());

            ExternalHandle handle;
            handle.handle = reinterpret_cast<HANDLE>(viewBufferHandleWin);
            importedViewBuffer = HardwareBuffer(handle, imageSize.x * imageSize.y, sizeof(float) * 4,
                                                visionDevice.get_aligned_memory_size(cudaViewBuffer->handle()),
                                                BufferUsage::StorageBuffer);
#endif
        }

        CFW_LOG_NOTICE("OpticsSystem: Initializing...");

        try
        {
            hardware_ = std::make_unique<Hardware>();

            // 修正：避免聚合初始化引发的赋值问题
            hardware_->gbufferSize.x = 1920;
            hardware_->gbufferSize.y = 1080;

            hardware_->gbufferPostionImage = HardwareImage(hardware_->gbufferSize.x, hardware_->gbufferSize.y,
                                                           ImageFormat::RGBA16_FLOAT, ImageUsage::StorageImage);
            hardware_->gbufferBaseColorImage = HardwareImage(hardware_->gbufferSize.x, hardware_->gbufferSize.y,
                                                             ImageFormat::RGBA16_FLOAT, ImageUsage::StorageImage);
            hardware_->gbufferNormalImage = HardwareImage(hardware_->gbufferSize.x, hardware_->gbufferSize.y,
                                                          ImageFormat::RGBA16_FLOAT, ImageUsage::StorageImage);
            hardware_->gbufferMotionVectorImage = HardwareImage(hardware_->gbufferSize.x, hardware_->gbufferSize.y,
                                                                ImageFormat::RG32_FLOAT, ImageUsage::StorageImage);
            hardware_->gbufferDepthImage = HardwareImage(hardware_->gbufferSize.x, hardware_->gbufferSize.y,
                                                         ImageFormat::D32_FLOAT, ImageUsage::DepthImage);

            hardware_->uniformBuffer =
                HardwareBuffer(sizeof(Hardware::UniformBufferObject), BufferUsage::StorageBuffer);
            hardware_->gbufferUniformBuffer = HardwareBuffer(sizeof(Hardware::gbufferUniformBufferObject),
                                                             BufferUsage::StorageBuffer);
            hardware_->computeUniformBuffer = HardwareBuffer(sizeof(Hardware::ComputeUniformBufferObject),
                                                             BufferUsage::StorageBuffer);

            hardware_->finalOutputImage = HardwareImage(hardware_->gbufferSize.x, hardware_->gbufferSize.y,
                                                        ImageFormat::RGBA16_FLOAT, ImageUsage::StorageImage);
        }
        catch (const std::exception& e)
        {
            CFW_LOG_CRITICAL("OpticsSystem: Failed to initialize hardware resources: {}", e.what());
            return false;
        }

        auto vert_id = load_shader(std::filesystem::current_path() / "assets" / "shaders" / "test.vert.glsl");
        auto frag_id = load_shader(std::filesystem::current_path() / "assets" / "shaders" / "test.frag.glsl");
        auto compute_id = load_shader(std::filesystem::current_path() / "assets" / "shaders" / "test.comp.glsl");

        auto vert_code = Resource::ResourceManager::get_instance().acquire_read<Resource::Text>(vert_id);
        auto frag_code = Resource::ResourceManager::get_instance().acquire_read<Resource::Text>(frag_id);
        auto compute_code = Resource::ResourceManager::get_instance().acquire_read<Resource::Text>(compute_id);

        if (!vert_code || !frag_code || !compute_code)
        {
            CFW_LOG_CRITICAL("OpticsSystem: Failed to load required shader files");
            return false;
        }

        try
        {
            hardware_->rasterizerPipeline = RasterizerPipeline(vert_code->text, frag_code->text);
            hardware_->computePipeline = ComputePipeline(compute_code->text);
            hardware_->shaderHasInit = true;
            CFW_LOG_INFO("OpticsSystem: Shaders compiled successfully");
        }
        catch (const std::exception& e)
        {
            CFW_LOG_CRITICAL("OpticsSystem: Failed to compile shaders: {}", e.what());
            return false;
        }

        CFW_LOG_WARNING("OpticsSystem: Shader compilation temporarily disabled - waiting for Resource API update");
        hardware_->shaderHasInit = true;

        return true;
    }

    void OpticsSystem::update()
    {
        if (!hardware_->shaderHasInit)
        {
            return;
        }

        static float frame_count = 0.0f;
        static uint64_t frame_index = 0;

        float dt = delta_time();
        frame_count += dt;
        ++frame_index;

        // 离屏渲染下不需要display，拆离到display线程去
        optics_pipeline(frame_count, frame_index);
    }

    void OpticsSystem::optics_pipeline(float frame_count, uint64_t frame_index)
    {
        // CFW_LOG_DEBUG("OpticsSystem: Rendering pipeline temporarily disabled - waiting for new Storage API");

        // 遍历场景存储并使用 acquire_read 访问相关句柄
        for (const auto& scene : SharedDataHub::instance().scene_storage())
        {
            for (auto vp_handle : scene.viewport_handles)
            {
                if (auto viewport = SharedDataHub::instance().viewport_storage().acquire_read(vp_handle))
                {
                    if (viewport->camera == 0)
                    {
                        continue;
                    }
                    if (auto camera = SharedDataHub::instance().camera_storage().acquire_read(viewport->camera))
                    {
                        hardware_->uniformBufferObjects.eyePosition = camera->position;
                        hardware_->uniformBufferObjects.eyeDir = camera->forward;
                        hardware_->uniformBufferObjects.eyeViewMatrix = camera->compute_view_matrix();
                        hardware_->uniformBufferObjects.eyeProjMatrix = camera->compute_projection_matrix();
                        hardware_->gbufferUniformBufferObjects.viewProjMatrix = camera->compute_view_proj_matrix();
                        hardware_->gbufferUniformBuffer.copyFromData(&hardware_->gbufferUniformBufferObjects,
                                                                     sizeof(hardware_->gbufferUniformBufferObjects));

                        hardware_->rasterizerPipeline["gbufferPostion"] = hardware_->gbufferPostionImage;
                        hardware_->rasterizerPipeline["gbufferBaseColor"] = hardware_->gbufferBaseColorImage;
                        hardware_->rasterizerPipeline["gbufferNormal"] = hardware_->gbufferNormalImage;
                        hardware_->rasterizerPipeline["gbufferMotionVector"] = hardware_->gbufferMotionVectorImage;
                        hardware_->rasterizerPipeline.setDepthImage(hardware_->gbufferDepthImage);

                        // 遍历所有光学设备
                        for (const auto& optics : SharedDataHub::instance().optics_storage())
                        {
                            if (auto geom = SharedDataHub::instance().geometry_storage().acquire_write(
                                optics.geometry_handle))
                            {
                                // 获取模型的全局变换矩阵
                                ktm::fmat4x4 model_matrix{ktm::fmat4x4::from_eye()};
                                if (auto transform = SharedDataHub::instance().model_transform_storage().acquire_read(
                                    geom->transform_handle))
                                {
                                    model_matrix = transform->compute_matrix();
                                }

                                // 每个 submesh 都需要完整设置所有 push constants
                                // 因为 record() 会在保存后重置 tempPushConstant
                                // 注意：节点累积变换已在加载时"烘焙"到顶点数据中
                                for (auto& m : geom->mesh_handles)
                                {
                                    hardware_->rasterizerPipeline["pushConsts.modelMatrix"] = model_matrix;
                                    hardware_->rasterizerPipeline["pushConsts.uniformBufferIndex"] = hardware_->
                                        gbufferUniformBuffer.storeDescriptor();
                                    // 检查纹理是否有效，避免对未初始化的 HardwareImage 调用 storeDescriptor()
                                    if (m.textureBuffer)
                                    {
                                        hardware_->rasterizerPipeline["pushConsts.textureIndex"] = m.textureBuffer.
                                            storeDescriptor();
                                    }
                                    else
                                    {
                                        hardware_->rasterizerPipeline["pushConsts.textureIndex"] = static_cast<uint32_t>
                                            (0);
                                    }
                                    // 传递材质颜色到着色器
                                    ktm::fvec4 materialColor{
                                        m.materialColor[0], m.materialColor[1], m.materialColor[2], m.materialColor[3]
                                    };
                                    hardware_->rasterizerPipeline["pushConsts.materialColor"] = materialColor;
                                    hardware_->rasterizerPipeline.record(m.indexBuffer, m.vertexBuffer);
                                }
                            }
                        }

                        hardware_->computePipeline["pushConsts.gbufferSize"] = hardware_->gbufferSize;
                        hardware_->computePipeline["pushConsts.gbufferPostionImage"] = hardware_->gbufferPostionImage.
                            storeDescriptor();
                        hardware_->computePipeline["pushConsts.gbufferBaseColorImage"] = hardware_->
                            gbufferBaseColorImage.storeDescriptor();
                        hardware_->computePipeline["pushConsts.gbufferNormalImage"] = hardware_->gbufferNormalImage.
                            storeDescriptor();
                        hardware_->computePipeline["pushConsts.gbufferDepthImage"] = hardware_->rasterizerPipeline.
                            getDepthImage().storeDescriptor();

                        hardware_->computePipeline["pushConsts.finalOutputImage"] = hardware_->finalOutputImage.
                            storeDescriptor();

                        ktm::fvec3 sun_dir;
                        sun_dir.x = 1.0f;
                        sun_dir.y = 1.0f;
                        sun_dir.z = 1.0f;
                        if (scene.environment != 0)
                        {
                            if (auto env = SharedDataHub::instance().environment_storage().acquire_read(
                                scene.environment))
                            {
                                sun_dir = env->sun_position;
                            }
                        }

                        hardware_->computePipeline["pushConsts.sun_dir"] = ktm::normalize(sun_dir);
                        {
                            // 调整为黄昏颜色 (Dusk)
                            static const ktm::fvec3 lightColor{
                                190.0f,
                                120.0f,
                                60.0f
                            };

                            hardware_->computePipeline["pushConsts.lightColor"] = lightColor;
                        }

                        hardware_->uniformBuffer.copyFromData(&hardware_->uniformBufferObjects,
                                                              sizeof(hardware_->uniformBufferObjects));
                        hardware_->computePipeline["pushConsts.uniformBufferIndex"] = hardware_->uniformBuffer.
                            storeDescriptor();

                        hardware_->executor << hardware_->rasterizerPipeline(1920, 1080)
                            << hardware_->computePipeline(1920 / 8, 1080 / 8, 1)
                            << hardware_->executor.commit();

                        if (camera->surface != nullptr)
                        {
                            if (auto* event_bus = context()->event_bus())
                            {
                                event_bus->publish<Events::DisplayFrameReadyEvent>({
                                    camera->surface,
                                    &hardware_->finalOutputImage,
                                    &hardware_->executor,
                                    frame_index,
                                    Events::DisplayFrameSource::optics});
                            }
                        }

#ifdef CORONA_ENABLE_VISION
                        // if (hardware_->displayers_.contains(reinterpret_cast<uint64_t>(camera->surface)))
                        // {
                        //     renderPipeline->display(1 / 30);
                        //     importedViewImage.copyFromBuffer(importedViewBuffer);
                        //     hardware_->displayers_.at(reinterpret_cast<uint64_t>(camera->surface)).wait(
                        //         hardware_->executor) << importedViewImage;
                        // }
#else
                        // if (hardware_->displayers_.contains(reinterpret_cast<uint64_t>(camera->surface)))
                        // {
                        //     hardware_->displayers_.at(reinterpret_cast<uint64_t>(camera->surface)).wait(
                        //         hardware_->executor) << hardware_->finalOutputImage;
                        // }
#endif
                    }
                }
            }
        }
    }

    void OpticsSystem::shutdown()
    {
        CFW_LOG_NOTICE("OpticsSystem: Shutting down...");

        hardware_.reset();
        CFW_LOG_INFO("OpticsSystem: Hardware resources released");
    }
} // namespace Corona::Systems
