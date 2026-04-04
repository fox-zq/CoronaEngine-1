#include <corona/events/display_system_events.h>
#include <corona/events/optics_system_events.h>
#include <corona/kernel/core/i_logger.h>
#include <corona/kernel/event/i_event_bus.h>
#include <corona/kernel/event/i_event_stream.h>
#include <corona/resource/resource_manager.h>
#include <corona/resource/types/image.h>
#include <corona/shared_data_hub.h>
#include <corona/systems/optics/optics_system.h>

#include <exception>
#include <filesystem>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "hardware.h"

#undef CORONA_ENABLE_VISION

#ifdef CORONA_ENABLE_VISION
#include "base/import/importer.h"
#include "base/mgr/pipeline.h"
#include "rhi/context.h"
#endif

namespace
{
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

    bool OpticsSystem::initialize_vision_backend_if_enabled()
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
        return true;
    }

    bool OpticsSystem::initialize_hardware_resources()
    {
        try
        {
            hardware_ = std::make_unique<Hardware>();
            image_handle_ = SharedDataHub::instance().image_storage().allocate();
            if (auto accessor = SharedDataHub::instance().image_storage().acquire_write(image_handle_)) {
                // Keep storage entry alive; per-frame image/executor values are updated after render submit.
            } else {
                CFW_LOG_ERROR("[OpticsSystem] Failed to acquire write access to image storage");
                SharedDataHub::instance().image_storage().deallocate(image_handle_);
                image_handle_ = 0;
                return false;
            }

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

            hardware_->gbufferObjectIDImage = HardwareImage(hardware_->gbufferSize.x, hardware_->gbufferSize.y,
                                                            ImageFormat::RGBA16_FLOAT, ImageUsage::StorageImage);
            hardware_->objectIDOutputImage = HardwareImage(hardware_->gbufferSize.x, hardware_->gbufferSize.y,
                                                           ImageFormat::RGBA16_FLOAT, ImageUsage::StorageImage);

            hardware_->uniformBuffer =
                HardwareBuffer(sizeof(Hardware::UniformBufferObject), BufferUsage::StorageBuffer);
            hardware_->gbufferUniformBuffer = HardwareBuffer(sizeof(Hardware::gbufferUniformBufferObject),
                                                             BufferUsage::StorageBuffer);
            hardware_->computeUniformBuffer = HardwareBuffer(sizeof(Hardware::ComputeUniformBufferObject),
                                                             BufferUsage::StorageBuffer);

            hardware_->finalOutputImage = HardwareImage(hardware_->gbufferSize.x, hardware_->gbufferSize.y,
                                                        ImageFormat::RGBA16_FLOAT, ImageUsage::StorageImage);
        }
        catch (const std::exception&)
        {
            CFW_LOG_CRITICAL("OpticsSystem: Failed to initialize hardware resources");
            return false;
        }

        return true;
    }

    bool OpticsSystem::initialize_render_pipelines()
    {
        try
        {
            hardware_->rasterizerPipeline.emplace();
            hardware_->computePipeline.emplace();
            hardware_->shaderHasInit = true;
            CFW_LOG_INFO("OpticsSystem: Typed shader pipelines created successfully");
        }
        catch (const std::exception& e)
        {
            CFW_LOG_CRITICAL("OpticsSystem: Failed to initialize typed pipelines: {}", e.what());
            return false;
        }

        return true;
    }

    bool OpticsSystem::initialize(Kernel::ISystemContext* ctx)
    {
        (void)ctx;

        if (!initialize_vision_backend_if_enabled())
        {
            return false;
        }

        CFW_LOG_NOTICE("OpticsSystem: Initializing...");

        if (!initialize_hardware_resources())
        {
            return false;
        }

        if (!initialize_render_pipelines())
        {
            return false;
        }

        if (auto* event_bus = ctx->event_bus())
        {
            screenshot_request_sub_id_ = event_bus->subscribe<Events::ScreenshotRequestEvent>(
                [this](const Events::ScreenshotRequestEvent& event) {
                    if (event.surface == nullptr || event.file_path.empty()) {
                        return;
                    }
                    std::lock_guard<std::mutex> lock(screenshot_mutex_);
                    pending_screenshots_.push_back({event.surface, event.file_path, event.buffer_type});
                });
        }

        return true;
    }

    void OpticsSystem::update()
    {
        if (!hardware_->shaderHasInit || !hardware_->rasterizerPipeline || !hardware_->computePipeline)
        {
            return;
        }

        static float frame_count = 0.0f;
        static uint64_t frame_index = 0;

        float dt = delta_time();
        frame_count += dt;
        ++frame_index;

        optics_pipeline(frame_count, frame_index);
    }

    void OpticsSystem::optics_pipeline(float frame_count, uint64_t frame_index)
    {
        // CFW_LOG_DEBUG("OpticsSystem: Rendering pipeline temporarily disabled - waiting for new Storage API");
        auto& rasterizer = *hardware_->rasterizerPipeline;
        auto& compute = *hardware_->computePipeline;

        // 遍历场景存储并使用 acquire_read 访问相关句柄
        for (const auto& scene : SharedDataHub::instance().scene_storage())
        {
            for (auto cam_handle : scene.camera_handles)
            {
                if (auto camera = SharedDataHub::instance().camera_storage().acquire_read(cam_handle))
                {
                        hardware_->uniformBufferObjects.eyePosition = camera->position;
                        hardware_->uniformBufferObjects.eyeDir = camera->forward;
                        hardware_->uniformBufferObjects.eyeViewMatrix = camera->compute_view_matrix();
                        hardware_->uniformBufferObjects.eyeProjMatrix = camera->compute_projection_matrix();
                        hardware_->gbufferUniformBufferObjects.viewProjMatrix = camera->compute_view_proj_matrix();
                        hardware_->gbufferUniformBuffer.copyFromData(&hardware_->gbufferUniformBufferObjects,
                                                                     sizeof(hardware_->gbufferUniformBufferObjects));

                        rasterizer.gbufferPostion = hardware_->gbufferPostionImage;
                        rasterizer.gbufferBaseColor = hardware_->gbufferBaseColorImage;
                        rasterizer.gbufferNormal = hardware_->gbufferNormalImage;
                        rasterizer.gbufferMotionVector = hardware_->gbufferMotionVectorImage;
                        rasterizer.gbufferObjectID = hardware_->gbufferObjectIDImage;
                        rasterizer.setDepthImage(hardware_->gbufferDepthImage);

                        // 遍历所有光学设备
                        uint32_t object_id = 1;
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
                                    rasterizer.pushConsts.modelMatrix = model_matrix;
                                    rasterizer.pushConsts.uniformBufferIndex = hardware_->gbufferUniformBuffer.
                                        storeDescriptor();
                                    // 检查纹理是否有效，避免对未初始化的 HardwareImage 调用 storeDescriptor()
                                    if (m.textureBuffer)
                                    {
                                        rasterizer[test_frag_glsl::pushConsts::textureIndex] = m.textureBuffer.storeDescriptor();
                                    }
                                    else
                                    {
                                        rasterizer[test_frag_glsl::pushConsts::textureIndex] = static_cast<uint32_t>(0);
                                    }
                                    // 传递材质颜色到着色器
                                    ktm::fvec4 materialColor{
                                        m.materialColor[0], m.materialColor[1], m.materialColor[2], m.materialColor[3]
                                    };
                                    rasterizer[test_frag_glsl::pushConsts::materialColor] = materialColor;
                                    rasterizer[test_frag_glsl::pushConsts::objectID] = object_id;
                                    rasterizer.record(m.indexBuffer, m.vertexBuffer);
                                }
                            }
                            ++object_id;
                        }

                        compute.pushConsts.gbufferSize = hardware_->gbufferSize;
                        compute.pushConsts.gbufferPostionImage = hardware_->gbufferPostionImage.storeDescriptor();
                        compute.pushConsts.gbufferBaseColorImage = hardware_->gbufferBaseColorImage.storeDescriptor();
                        compute.pushConsts.gbufferNormalImage = hardware_->gbufferNormalImage.storeDescriptor();
                        compute.pushConsts.gbufferDepthImage = rasterizer.getDepthImage().storeDescriptor();

                        compute.pushConsts.gbufferObjectIDImage = hardware_->gbufferObjectIDImage.storeDescriptor();
                        compute.pushConsts.objectIDOutputImage = hardware_->objectIDOutputImage.storeDescriptor();

                        compute.pushConsts.finalOutputImage = hardware_->finalOutputImage.storeDescriptor();

                        ktm::fvec3 sun_dir;
                        sun_dir.x = 1.0f;
                        sun_dir.y = 1.0f;
                        sun_dir.z = 1.0f;
                        std::uint32_t floor_grid_enabled = 1;
                        if (scene.environment != 0)
                        {
                            if (auto env = SharedDataHub::instance().environment_storage().acquire_read(
                                scene.environment))
                            {
                                sun_dir = env->sun_position;
                                floor_grid_enabled = env->floor_grid_enabled;
                            }
                        }

                        compute.pushConsts.sun_dir = ktm::normalize(sun_dir);
                        compute.pushConsts.floor_grid_enabled = floor_grid_enabled;
                        {
                            // 调整为黄昏颜色 (Dusk)
                            static const ktm::fvec3 lightColor{
                                190.0f,
                                120.0f,
                                60.0f
                            };

                            compute.pushConsts.lightColor = lightColor;
                        }

                        hardware_->uniformBuffer.copyFromData(&hardware_->uniformBufferObjects,
                                                              sizeof(hardware_->uniformBufferObjects));
                        compute.pushConsts.uniformBufferIndex = hardware_->uniformBuffer.storeDescriptor();

                        // GPU sync: wait for Display to finish consuming our image
                        // before we overwrite it with new rendering output.
                        if (image_handle_ != 0) {
                            if (auto consumed_device = SharedDataHub::instance().image_storage().acquire_write(image_handle_)) {
                                hardware_->executor.wait(consumed_device->consumed_executor);
                            }
                        }

                        hardware_->executor << rasterizer(1920, 1080)
                            << compute(1920 / 8, 1080 / 8, 1)
                            << hardware_->executor.commit();

                        if (image_handle_ != 0)
                        {
                            if (auto image_device = SharedDataHub::instance().image_storage().acquire_write(image_handle_))
                            {
                                image_device->image = hardware_->finalOutputImage;
                                image_device->executor = hardware_->executor;
                            }

                            if (camera->surface != nullptr)
                            {
                                process_pending_screenshots(camera->surface);

                                if (auto* event_bus = context()->event_bus())
                                {
                                    event_bus->publish<Events::OpticsFrameReadyEvent>({
                                        camera->surface,
                                        image_handle_,
                                        frame_index,
                                        hardware_->gbufferSize.x,
                                        hardware_->gbufferSize.y
                                    });
                                }
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

    namespace {

    // Convert IEEE 754 half-precision float (16-bit) to single-precision float.
    float half_to_float(uint16_t h) {
        const uint32_t sign     = (h >> 15) & 0x1;
        const uint32_t exponent = (h >> 10) & 0x1F;
        const uint32_t mantissa = h & 0x3FF;

        float result;
        if (exponent == 0) {
            result = std::ldexp(static_cast<float>(mantissa), -24);  // denorm or zero
        } else if (exponent == 31) {
            result = (mantissa == 0) ? INFINITY : NAN;
        } else {
            result = std::ldexp(static_cast<float>(mantissa | 0x400), static_cast<int>(exponent) - 25);
        }
        return sign ? -result : result;
    }

    }  // namespace

    void OpticsSystem::process_pending_screenshots(void* surface)
    {
        std::vector<PendingScreenshot> matched;
        {
            std::lock_guard<std::mutex> lock(screenshot_mutex_);
            auto it = std::remove_if(pending_screenshots_.begin(), pending_screenshots_.end(),
                [surface](const PendingScreenshot& req) { return req.surface == surface; });
            matched.assign(std::make_move_iterator(it), std::make_move_iterator(pending_screenshots_.end()));
            pending_screenshots_.erase(it, pending_screenshots_.end());
        }

        if (matched.empty()) {
            return;
        }

        const uint32_t w = hardware_->gbufferSize.x;
        const uint32_t h = hardware_->gbufferSize.y;
        if (w == 0 || h == 0) {
            CFW_LOG_WARNING("OpticsSystem: Cannot take screenshot - zero render dimensions");
            return;
        }

        // Select source image based on buffer_type
        // Group by buffer_type so we only readback each image once if there are multiple requests
        const uint64_t pixel_count = static_cast<uint64_t>(w) * h;
        std::unordered_map<std::string, std::vector<PendingScreenshot*>> by_type;
        for (auto& req : matched) {
            by_type[req.buffer_type.empty() ? "final_color" : req.buffer_type].push_back(&req);
        }

        for (auto& [buf_type, reqs] : by_type) {
            // Select the HardwareImage for this buf_type
            HardwareImage* src_image = &hardware_->finalOutputImage;
            bool is_normal = false;
            if (buf_type == "object_id") {
                src_image = &hardware_->objectIDOutputImage;
            } else if (buf_type == "base_color") {
                src_image = &hardware_->gbufferBaseColorImage;
            } else if (buf_type == "normal") {
                src_image = &hardware_->gbufferNormalImage;
                is_normal = true;
            } else if (buf_type == "position") {
                src_image = &hardware_->gbufferPostionImage;
            }

            const uint64_t buffer_size = pixel_count * 8;  // RGBA16F = 4 channels * 2 bytes
            HardwareBuffer staging_buffer(static_cast<uint32_t>(buffer_size), BufferUsage::StorageBuffer);
            if (!staging_buffer) {
                CFW_LOG_ERROR("OpticsSystem: Failed to create staging buffer for {} screenshot", buf_type);
                continue;
            }

            hardware_->executor << src_image->copyTo(staging_buffer)
                                << hardware_->executor.commit();

            std::vector<uint16_t> half_data(pixel_count * 4);
            if (!staging_buffer.copyToData(half_data.data(), buffer_size)) {
                CFW_LOG_ERROR("OpticsSystem: Failed to read {} data from GPU", buf_type);
                continue;
            }

            // Convert RGBA16F to RGBA8 with buffer-specific normalization
            std::vector<uint8_t> rgba8(pixel_count * 4);
            for (uint64_t i = 0; i < pixel_count * 4; ++i) {
                float v = half_to_float(half_data[i]);
                if (is_normal) {
                    v = v * 0.5f + 0.5f;  // [-1,1] -> [0,1]
                }
                v = std::fmax(0.0f, std::fmin(1.0f, v));
                rgba8[i] = static_cast<uint8_t>(v * 255.0f + 0.5f);
            }

            for (const auto* req : reqs) {
                std::filesystem::path file_path(req->file_path);
                auto image = std::make_shared<Resource::Image>(file_path);
                image->set_data(rgba8.data(), static_cast<int>(w), static_cast<int>(h), 4);

                auto rid = Resource::IResource::generate_uid(file_path);
                auto& manager = Resource::ResourceManager::get_instance();
                manager.add_resource(rid, image);

                if (manager.export_sync(rid, file_path)) {
                    CFW_LOG_INFO("OpticsSystem: {} screenshot saved to {}", buf_type, req->file_path);
                } else {
                    CFW_LOG_ERROR("OpticsSystem: Failed to save {} screenshot to {}", buf_type, req->file_path);
                }
            }
        }
    }

    void OpticsSystem::shutdown()
    {
        CFW_LOG_NOTICE("OpticsSystem: Shutting down...");

        if (auto* event_bus = context()->event_bus())
        {
            if (screenshot_request_sub_id_ != 0)
            {
                event_bus->unsubscribe(screenshot_request_sub_id_);
            }
        }

        if (image_handle_ != 0) {
            SharedDataHub::instance().image_storage().deallocate(image_handle_);
            image_handle_ = 0;
        }

        hardware_.reset();

        CFW_LOG_INFO("OpticsSystem: Hardware resources released");
    }
} // namespace Corona::Systems
