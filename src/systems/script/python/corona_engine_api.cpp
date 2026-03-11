#include <atomic>
#include <CabbageHardware.h>
#include <corona/events/display_system_events.h>
#include <corona/kernel/core/kernel_context.h>
#include <corona/kernel/event/i_event_bus.h>
#include <corona/resource/resource_manager.h>
#include <corona/resource/types/scene.h>
#include <corona/shared_data_hub.h>
#include <corona/systems/script/corona_engine_api.h>
#include <corona/utils/path_utils.h>

#include "corona/resource/types/image.h"

namespace
{
    std::atomic<void*> g_default_surface{nullptr};
}

// ########################
//          Scene
// ########################
Corona::API::Scene::Scene()
    : handle_(0) {
    handle_ = SharedDataHub::instance().scene_storage().allocate();
}

Corona::API::Scene::~Scene() {
    if (handle_ != 0) {
        SharedDataHub::instance().scene_storage().deallocate(handle_);
        handle_ = 0;
    }
}

void Corona::API::Scene::set_environment(Environment* env) {
    if (handle_ == 0) {
        CFW_LOG_WARNING("[Scene::set_environment] Invalid scene handle");
        return;
    }

    if (env == nullptr) {
        CFW_LOG_WARNING("[Scene::set_environment] Null environment pointer");
        return;
    }

    environment_ = env;

    if (auto accessor = SharedDataHub::instance().scene_storage().acquire_write(handle_)) {
        accessor->environment = env->get_handle();
    } else {
        CFW_LOG_ERROR("[Scene::set_environment] Failed to acquire write access to scene storage");
    }
}

Corona::API::Environment* Corona::API::Scene::get_environment() {
    return environment_;
}

bool Corona::API::Scene::has_environment() const {
    return environment_ != nullptr;
}

void Corona::API::Scene::remove_environment() {
    if (handle_ == 0) return;

    environment_ = nullptr;

    if (auto accessor = SharedDataHub::instance().scene_storage().acquire_write(handle_)) {
        accessor->environment = 0;
    }
}

void Corona::API::Scene::add_actor(Actor* actor) {
    if (handle_ == 0) {
        CFW_LOG_WARNING("[Scene::add_actor] Invalid scene handle");
        return;
    }

    if (actor == nullptr) {
        CFW_LOG_WARNING("[Scene::add_actor] Null actor pointer");
        return;
    }

    auto it = std::ranges::find(actors_, actor);
    if (it != actors_.end()) {
        CFW_LOG_WARNING("[Scene::add_actor] Actor already exists in scene, handle: {}", actor->get_handle());
        return;
    }

    actors_.push_back(actor);

    if (auto accessor = SharedDataHub::instance().scene_storage().acquire_write(handle_)) {
        accessor->actor_handles.push_back(actor->get_handle());
    } else {
        CFW_LOG_ERROR("[Scene::add_actor] Failed to acquire write access to scene storage");
    }
}

void Corona::API::Scene::remove_actor(Actor* actor) {
    if (handle_ == 0) return;

    if (actor == nullptr) {
        CFW_LOG_WARNING("[Scene::remove_actor] Null actor pointer");
        return;
    }

    auto removed = std::erase(actors_, actor);
    if (removed == 0) {
        CFW_LOG_WARNING("[Scene::remove_actor] Actor not found in scene, handle: {}", actor->get_handle());
    }

    if (auto accessor = SharedDataHub::instance().scene_storage().acquire_write(handle_)) {
        std::erase(accessor->actor_handles, actor->get_handle());
    }
}

void Corona::API::Scene::clear_actors() {
    if (handle_ == 0) return;

    CFW_LOG_INFO("[Scene::clear_actors] Clearing {} actors", actors_.size());

    actors_.clear();

    if (auto accessor = SharedDataHub::instance().scene_storage().acquire_write(handle_)) {
        accessor->actor_handles.clear();
    }
}

std::size_t Corona::API::Scene::actor_count() const {
    return actors_.size();
}

bool Corona::API::Scene::has_actor(const Actor* actor) const {
    if (actor == nullptr) return false;
    return std::ranges::find(actors_, actor) != actors_.end();
}

void Corona::API::Scene::add_viewport(Viewport* viewport) {
    if (handle_ == 0) {
        CFW_LOG_WARNING("[Scene::add_viewport] Invalid scene handle");
        return;
    }

    if (viewport == nullptr) {
        CFW_LOG_WARNING("[Scene::add_viewport] Null viewport pointer");
        return;
    }

    auto it = std::ranges::find(viewports_, viewport);
    if (it != viewports_.end()) {
        CFW_LOG_WARNING("[Scene::add_viewport] Viewport already exists in scene");
        return;
    }

    viewports_.push_back(viewport);

    if (auto accessor = SharedDataHub::instance().scene_storage().acquire_write(handle_)) {
        accessor->viewport_handles.push_back(viewport->get_handle());
    } else {
        CFW_LOG_ERROR("[Scene::add_viewport] Failed to acquire write access to scene storage");
    }
}

void Corona::API::Scene::remove_viewport(Viewport* viewport) {
    if (handle_ == 0) return;

    if (viewport == nullptr) {
        CFW_LOG_WARNING("[Scene::remove_viewport] Null viewport pointer");
        return;
    }

    auto removed = std::erase(viewports_, viewport);
    if (removed == 0) {
        CFW_LOG_WARNING("[Scene::remove_viewport] Viewport not found in scene");
        return;
    }

    if (auto accessor = SharedDataHub::instance().scene_storage().acquire_write(handle_)) {
        std::erase(accessor->viewport_handles, viewport->get_handle());
    }
}

void Corona::API::Scene::clear_viewports() {
    if (handle_ == 0) return;

    CFW_LOG_INFO("[Scene::clear_viewports] Clearing {} viewports", viewports_.size());

    viewports_.clear();

    if (auto accessor = SharedDataHub::instance().scene_storage().acquire_write(handle_)) {
        accessor->viewport_handles.clear();
    }
}

std::size_t Corona::API::Scene::viewport_count() const {
    return viewports_.size();
}

bool Corona::API::Scene::has_viewport(const Viewport* viewport) const {
    if (viewport == nullptr) return false;
    return std::ranges::find(viewports_, viewport) != viewports_.end();
}

// ########################
//      Environment
// ########################
Corona::API::Environment::Environment()
    : handle_(0) {
    handle_ = SharedDataHub::instance().environment_storage().allocate();
    if (handle_ != 0) {
        if (auto accessor = SharedDataHub::instance().environment_storage().acquire_write(handle_)) {
            accessor->sun_position.x = 1.0f;
            accessor->sun_position.y = 1.0f;
            accessor->sun_position.z = 1.0f;
            // accessor->floor_grid_enabled = true;

            CFW_LOG_INFO("[Environment::Environment] Created {} handle", handle_);
        }
    }
}

Corona::API::Environment::~Environment() {
    if (handle_ != 0) {
        SharedDataHub::instance().environment_storage().deallocate(handle_);
        handle_ = 0;
    }
}

void Corona::API::Environment::set_sun_direction(const std::array<float, 3>& direction) {
    if (handle_ == 0) {
        CFW_LOG_WARNING("[Environment::set_sun_direction] Invalid environment handle");
        return;
    }

    if (auto accessor = SharedDataHub::instance().environment_storage().acquire_write(handle_)) {
        accessor->sun_position.x = direction[0];
        accessor->sun_position.y = direction[1];
        accessor->sun_position.z = direction[2];

        // CFW_LOG_INFO("[Environment::set_sun_direction] Sun direction set to: ({}, {}, {})",
        //              direction[0], direction[1], direction[2]);

    } else {
        CFW_LOG_ERROR("[Environment::set_sun_direction] Failed to acquire write access to environment storage");
    }
}

void Corona::API::Environment::set_floor_grid(bool enabled) const {
    // TODO: Implement floor grid rendering control
    CFW_LOG_WARNING("[Corona::API::Environment::set_floor_grid] Not implemented yet: {}",
                    enabled ? "enabled" : "disabled");
}

std::uintptr_t Corona::API::Environment::get_handle() const {
    return handle_;
}

// ########################
//         Geometry
// ########################
Corona::API::Geometry::Geometry(const std::string& model_path) {
    // 使用 utf8_to_path 确保 UTF-8 编码的路径在 Windows 上正确转换
    auto model_id = Resource::ResourceManager::get_instance().import_sync(Utils::utf8_to_path(model_path));
    if (model_id == 0) {
        CFW_LOG_CRITICAL("[Geometry] Failed to load model: {}", model_path);
        return;
    }

    model_resource_handle_ = SharedDataHub::instance().model_resource_storage().allocate();
    if (auto handle = SharedDataHub::instance().model_resource_storage().acquire_write(model_resource_handle_)) {
        handle->model_id = model_id;
    } else {
        CFW_LOG_ERROR("[Geometry] Failed to acquire write access to model resource storage");
        SharedDataHub::instance().model_resource_storage().deallocate(model_resource_handle_);
        model_resource_handle_ = 0;
        return;
    }

    transform_handle_ = SharedDataHub::instance().model_transform_storage().allocate();

    auto scene = Resource::ResourceManager::get_instance().acquire_read<Resource::Scene>(model_id);
    if (!scene) {
        CFW_LOG_ERROR("[Geometry] Failed to acquire read access to scene resource");
        SharedDataHub::instance().model_resource_storage().deallocate(model_resource_handle_);
        SharedDataHub::instance().model_transform_storage().deallocate(transform_handle_);
        model_resource_handle_ = 0;
        transform_handle_ = 0;
        return;
    }

    if (scene->data.meshes.empty()) {
        CFW_LOG_WARNING("[Geometry] Scene has no meshes, checking nodes for mesh references...");
        for (std::uint32_t i = 0; i < scene->data.nodes.size(); ++i) {
            const auto& node = scene->data.nodes[i];
            CFW_LOG_DEBUG("  - Node {}: mesh_index={}", i, node.mesh_index);
        }
    }

    // 输出模型的基础变换数据
    CFW_LOG_INFO("[Geometry] Model loaded from: {}", model_path);
    CFW_LOG_INFO("[Geometry] Scene contains {} nodes, {} meshes, {} materials",
                 scene->data.nodes.size(), scene->data.meshes.size(), scene->data.materials.size());

    if (!scene->data.nodes.empty()) {
        const auto& root_node = scene->data.nodes[0];
        const auto& t = root_node.transform;
        CFW_LOG_INFO("[Geometry] Root Node: '{}' | Pos({:.3f}, {:.3f}, {:.3f}) | Rot({:.3f}, {:.3f}, {:.3f}) | Scale({:.3f}, {:.3f}, {:.3f})",
                     scene->get_node_name(0),
                     t.position[0], t.position[1], t.position[2],
                     t.rotation[0], t.rotation[1], t.rotation[2],
                     t.scale[0], t.scale[1], t.scale[2]);
    }

    // 输出网格的归一化信息（如果有）
    if (!scene->data.meshes.empty()) {
        CFW_LOG_INFO("[Geometry] Mesh Normalization Data:");
        for (std::uint32_t i = 0; i < scene->data.meshes.size(); ++i) {
            const auto& mesh = scene->data.meshes[i];
            if (mesh.is_normalized) {
                CFW_LOG_INFO("  - Mesh {}: Normalized | Original Center({:.3f}, {:.3f}, {:.3f}) | Scale Factor: {:.3f}",
                             i,
                             mesh.original_center[0], mesh.original_center[1], mesh.original_center[2],
                             mesh.original_scale_factor);
            } else {
                CFW_LOG_INFO("  - Mesh {}: Original Size (not normalized)", i);
            }
        }
    }

    std::vector<MeshDevice> mesh_devices;
    mesh_devices.reserve(scene->data.meshes.size());

    // 用于批量纹理上传的数据结构
    struct PendingTextureUpload {
        std::uint32_t mesh_idx;
        HardwareImage* texture;
        std::vector<unsigned char> rgba_data;  // 保持数据存活直到上传完成
        unsigned char* data_ptr;
    };
    std::vector<PendingTextureUpload> pending_uploads;
    pending_uploads.reserve(scene->data.meshes.size());

    // 获取或创建共享的占位纹理（只创建一次）
    static HardwareImage shared_placeholder_texture = []() {
        HardwareImageCreateInfo placeholder_info{};
        placeholder_info.width = 1;
        placeholder_info.height = 1;
        placeholder_info.format = ImageFormat::RGBA8_SRGB;
        placeholder_info.usage = ImageUsage::SampledImage;
        placeholder_info.arrayLayers = 1;
        placeholder_info.mipLevels = 1;

        static const unsigned char white_pixel[4] = {255, 255, 255, 255};
        HardwareImage texture(placeholder_info);
        HardwareExecutor temp_executor;
        temp_executor << texture.copyFrom(white_pixel) << temp_executor.commit();
        CFW_LOG_INFO("[Geometry] Created shared default white placeholder texture (1x1)");
        return texture;
    }();

    // 第一阶段：创建所有mesh设备和纹理对象（不提交GPU命令）
    for (std::uint32_t mesh_idx = 0; mesh_idx < scene->data.meshes.size(); ++mesh_idx) {
        const auto& mesh = scene->data.meshes[mesh_idx];
        MeshDevice dev{};

        dev.vertexBuffer = HardwareBuffer(scene->get_mesh_vertices(mesh_idx), BufferUsage::VertexBuffer);
        dev.indexBuffer = HardwareBuffer(scene->get_mesh_indices(mesh_idx), BufferUsage::IndexBuffer);

        dev.materialIndex = (mesh.material_index != Resource::InvalidIndex)
                                ? mesh.material_index
                                : 0;

        // 读取材质颜色
        if (mesh.material_index != Resource::InvalidIndex &&
            mesh.material_index < scene->data.materials.size()) {
            const auto& material = scene->data.materials[mesh.material_index];
            dev.materialColor = material.base_color;
            CFW_LOG_DEBUG("[Geometry] Mesh {} using material color: ({}, {}, {}, {})",
                          mesh_idx, dev.materialColor[0], dev.materialColor[1],
                          dev.materialColor[2], dev.materialColor[3]);
        }

        bool texture_created = false;
        HardwareImageCreateInfo create_info{};

        if (mesh.material_index != Resource::InvalidIndex &&
            mesh.material_index < scene->data.materials.size()) {
            auto texture_id = scene->data.materials[mesh.material_index].albedo_texture;
            CFW_LOG_DEBUG("[Geometry] Mesh {} material {} texture_id: {}, InvalidTextureId: {}",
                          mesh_idx, mesh.material_index, texture_id, Resource::InvalidTextureId);

            if (texture_id != Resource::InvalidTextureId) {
                auto texture_data = Resource::ResourceManager::get_instance().acquire_read<Resource::Image>(texture_id);
                if (texture_data && texture_data->get_data() != nullptr) {
                    const int tex_width = texture_data->get_width();
                    const int tex_height = texture_data->get_height();
                    const int tex_channels = texture_data->get_channels();
                    CFW_LOG_DEBUG("[Geometry] Mesh {} texture info: {}x{} channels={}",
                                  mesh_idx, tex_width, tex_height, tex_channels);

                    if (tex_width > 0 && tex_height > 0 && tex_channels > 0) {
                        constexpr bool use_compressed = false;  // TODO: 测试模型兼容性  先不走压缩纹理

                        if (texture_data->is_compressed()) {
                            // 获取压缩数据和格式信息
                            const auto& compressed = texture_data->get_compressed_data();

                            // 根据实际压缩格式选择正确的 ImageFormat
                            create_info.width = tex_width;
                            create_info.height = tex_height;
                            create_info.usage = ImageUsage::SampledImage;
                            create_info.arrayLayers = 1;
                            create_info.mipLevels = 1;

                            // 根据压缩格式类型选择对应的 GPU 格式
                            if (compressed.format == Resource::CompressedData::Format::BC1) {
                                create_info.format = ImageFormat::BC1_RGB_SRGB;
                            } else if (compressed.format == Resource::CompressedData::Format::BC3) {
                                create_info.format = ImageFormat::BC3_RGBA_SRGB;
                            } else if (compressed.format == Resource::CompressedData::Format::ASTC_4x4) {
                                create_info.format = ImageFormat::ASTC_4x4_SRGB;
                            } else {
                                CFW_LOG_WARNING("[Geometry] Unsupported compressed format, falling back to RGBA8");
                                // 如果遇到未知格式，应该使用非压缩路径而非猜测
                            }

                            // 复制压缩数据以避免悬空指针（texture_data 可能在上传前失效）
                            PendingTextureUpload upload{mesh_idx, nullptr, {}, nullptr};
                            upload.rgba_data.assign(compressed.data.begin(), compressed.data.end());
                            upload.data_ptr = upload.rgba_data.data();

                            dev.textureBuffer = HardwareImage(create_info);
                            upload.texture = &dev.textureBuffer;
                            pending_uploads.push_back(std::move(upload));
                            texture_created = true;
                        } else {
                            // 使用非压缩 RGBA8 格式，需要确保数据通道匹配
                            create_info.width = tex_width;
                            create_info.height = tex_height;
                            create_info.format = ImageFormat::RGBA8_SRGB;
                            create_info.usage = ImageUsage::SampledImage;
                            create_info.arrayLayers = 1;
                            create_info.mipLevels = 1;

                            unsigned char* src_data = texture_data->get_data();
                            PendingTextureUpload upload{mesh_idx, nullptr, {}, nullptr};

                            if (tex_channels == 4) {
                                // 已经是 RGBA，需要复制数据（因为texture_data可能在之后失效）
                                upload.rgba_data.assign(src_data, src_data + static_cast<size_t>(tex_width) * tex_height * 4);
                                upload.data_ptr = upload.rgba_data.data();
                            } else if (tex_channels == 3) {
                                // RGB -> RGBA 转换
                                upload.rgba_data.resize(static_cast<size_t>(tex_width) * tex_height * 4);
                                for (int i = 0; i < tex_width * tex_height; ++i) {
                                    upload.rgba_data[i * 4 + 0] = src_data[i * 3 + 0];  // R
                                    upload.rgba_data[i * 4 + 1] = src_data[i * 3 + 1];  // G
                                    upload.rgba_data[i * 4 + 2] = src_data[i * 3 + 2];  // B
                                    upload.rgba_data[i * 4 + 3] = 255;                  // A
                                }
                                upload.data_ptr = upload.rgba_data.data();
                            } else if (tex_channels == 1) {
                                // Grayscale -> RGBA 转换
                                upload.rgba_data.resize(static_cast<size_t>(tex_width) * tex_height * 4);
                                for (int i = 0; i < tex_width * tex_height; ++i) {
                                    upload.rgba_data[i * 4 + 0] = src_data[i];  // R
                                    upload.rgba_data[i * 4 + 1] = src_data[i];  // G
                                    upload.rgba_data[i * 4 + 2] = src_data[i];  // B
                                    upload.rgba_data[i * 4 + 3] = 255;          // A
                                }
                                upload.data_ptr = upload.rgba_data.data();
                            } else {
                                CFW_LOG_WARNING("[Geometry] Unsupported texture channel count: {}", tex_channels);
                            }

                            if (upload.data_ptr != nullptr) {
                                dev.textureBuffer = HardwareImage(create_info);
                                upload.texture = &dev.textureBuffer;
                                pending_uploads.push_back(std::move(upload));
                                texture_created = true;
                            }
                        }
                    }
                }
            }
        }

        // 为没有纹理的网格使用全局共享的默认 1x1 白色占位纹理
        if (!texture_created) {
            dev.textureBuffer = shared_placeholder_texture;
        }

        mesh_devices.emplace_back(std::move(dev));
    }

    // 第二阶段：批量上传所有纹理（减少GPU命令提交次数）
    if (!pending_uploads.empty()) {
        CFW_LOG_INFO("[Geometry] Batch uploading {} textures...", pending_uploads.size());

        // 分批提交，每批最多处理一定数量的纹理，避免单次命令过大
        constexpr size_t kBatchSize = 32;
        for (size_t batch_start = 0; batch_start < pending_uploads.size(); batch_start += kBatchSize) {
            size_t batch_end = std::min(batch_start + kBatchSize, pending_uploads.size());

            HardwareExecutor batch_executor;
            for (size_t i = batch_start; i < batch_end; ++i) {
                auto& upload = pending_uploads[i];
                // 更新texture指针（因为mesh_devices可能已经移动）
                HardwareImage& tex = mesh_devices[upload.mesh_idx].textureBuffer;
                batch_executor << tex.copyFrom(upload.data_ptr);
            }
            batch_executor << batch_executor.commit();

            // 强制等待每一批上传完成，防止短时间内提交过多 CommandBuffer 导致 Device Lost (TDR) 或内存问题
            batch_executor.waitForDeferredResources();

            CFW_LOG_DEBUG("[Geometry] Uploaded texture batch {}-{}/{}",
                          batch_start, batch_end - 1, pending_uploads.size());
        }

        CFW_LOG_INFO("[Geometry] Texture batch upload complete");
    }

    handle_ = SharedDataHub::instance().geometry_storage().allocate();
    if (auto handle = SharedDataHub::instance().geometry_storage().acquire_write(handle_)) {
        handle->transform_handle = transform_handle_;
        handle->model_resource_handle = model_resource_handle_;
        handle->mesh_handles = std::move(mesh_devices);
        CFW_LOG_INFO("[Geometry] Successfully created geometry with {} meshes from: {}",
                     handle->mesh_handles.size(), model_path);
    } else {
        CFW_LOG_CRITICAL("[Geometry] Failed to acquire write access to geometry storage");
        // 清理已分配的资源
        SharedDataHub::instance().model_transform_storage().deallocate(transform_handle_);
        SharedDataHub::instance().model_resource_storage().deallocate(model_resource_handle_);
        SharedDataHub::instance().geometry_storage().deallocate(handle_);
        handle_ = 0;
        transform_handle_ = 0;
        model_resource_handle_ = 0;
        return;
    }
}

Corona::API::Geometry::~Geometry() {
    if (handle_ != 0) {
        SharedDataHub::instance().geometry_storage().deallocate(handle_);
    }
    if (transform_handle_ != 0) {
        SharedDataHub::instance().model_transform_storage().deallocate(transform_handle_);
    }
    if (model_resource_handle_ != 0) {
        SharedDataHub::instance().model_resource_storage().deallocate(model_resource_handle_);
    }
}

void Corona::API::Geometry::set_position(const std::array<float, 3>& pos) {
    if (transform_handle_ == 0) {
        CFW_LOG_WARNING("[Geometry::set_position] Invalid transform handle");
        return;
    }

    if (auto accessor = SharedDataHub::instance().model_transform_storage().acquire_write(transform_handle_)) {
        accessor->position.x = pos[0];
        accessor->position.y = pos[1];
        accessor->position.z = pos[2];
    } else {
        CFW_LOG_ERROR("[Geometry::set_position] Failed to acquire write access to transform storage");
    }
}

void Corona::API::Geometry::set_rotation(const std::array<float, 3>& euler) {
    if (transform_handle_ == 0) {
        CFW_LOG_WARNING("[Geometry::set_rotation] Invalid transform handle");
        return;
    }

    // 直接写入容器中的局部旋转参数（欧拉角 ZYX 顺序）
    if (auto accessor = SharedDataHub::instance().model_transform_storage().acquire_write(transform_handle_)) {
        accessor->euler_rotation.x = euler[0];  // Pitch
        accessor->euler_rotation.y = euler[1];  // Yaw
        accessor->euler_rotation.z = euler[2];  // Roll
    } else {
        CFW_LOG_ERROR("[Geometry::set_rotation] Failed to acquire write access to transform storage");
    }
}

void Corona::API::Geometry::set_scale(const std::array<float, 3>& scl) {
    if (transform_handle_ == 0) {
        CFW_LOG_WARNING("[Geometry::set_scale] Invalid transform handle");
        return;
    }

    // 直接写入容器中的局部缩放参数
    if (auto accessor = SharedDataHub::instance().model_transform_storage().acquire_write(transform_handle_)) {
        accessor->scale.x = scl[0];
        accessor->scale.y = scl[1];
        accessor->scale.z = scl[2];
    } else {
        CFW_LOG_ERROR("[Geometry::set_scale] Failed to acquire write access to transform storage");
    }
}

std::array<float, 3> Corona::API::Geometry::get_position() const {
    if (transform_handle_ == 0) {
        CFW_LOG_WARNING("[Geometry::get_position] Invalid transform handle");
        return {0.0f, 0.0f, 0.0f};
    }

    // 从容器中读取局部位置参数
    std::array<float, 3> result = {0.0f, 0.0f, 0.0f};
    if (auto accessor = SharedDataHub::instance().model_transform_storage().acquire_read(transform_handle_)) {
        result[0] = accessor->position.x;
        result[1] = accessor->position.y;
        result[2] = accessor->position.z;
    } else {
        CFW_LOG_ERROR("[Geometry::get_position] Failed to acquire read access to transform storage");
    }

    return result;
}

std::array<float, 3> Corona::API::Geometry::get_rotation() const {
    if (transform_handle_ == 0) {
        CFW_LOG_WARNING("[Geometry::get_rotation] Invalid transform handle");
        return {0.0f, 0.0f, 0.0f};
    }

    // 从容器中读取局部旋转参数（欧拉角 ZYX 顺序）
    std::array<float, 3> result = {0.0f, 0.0f, 0.0f};
    if (auto accessor = SharedDataHub::instance().model_transform_storage().acquire_read(transform_handle_)) {
        result[0] = accessor->euler_rotation.x;  // Pitch
        result[1] = accessor->euler_rotation.y;  // Yaw
        result[2] = accessor->euler_rotation.z;  // Roll
    } else {
        CFW_LOG_ERROR("[Geometry::get_rotation] Failed to acquire read access to transform storage");
    }

    return result;
}

std::array<float, 3> Corona::API::Geometry::get_scale() const {
    if (transform_handle_ == 0) {
        CFW_LOG_WARNING("[Geometry::get_scale] Invalid transform handle");
        return {1.0f, 1.0f, 1.0f};
    }

    // 从容器中读取局部缩放参数
    std::array<float, 3> result = {1.0f, 1.0f, 1.0f};
    if (auto accessor = SharedDataHub::instance().model_transform_storage().acquire_read(transform_handle_)) {
        result[0] = accessor->scale.x;
        result[1] = accessor->scale.y;
        result[2] = accessor->scale.z;
    } else {
        CFW_LOG_ERROR("[Geometry::get_scale] Failed to acquire read access to transform storage");
    }

    return result;
}

std::uintptr_t Corona::API::Geometry::get_handle() const {
    return handle_;
}

std::uintptr_t Corona::API::Geometry::get_transform_handle() const {
    return transform_handle_;
}

std::uintptr_t Corona::API::Geometry::get_model_resource_handle() const {
    return model_resource_handle_;
}

// ########################
//         Optics
// ########################
Corona::API::Optics::Optics(Geometry& geo)
    : geometry_(&geo), handle_(0) {
    handle_ = SharedDataHub::instance().optics_storage().allocate();
    if (auto accessor = SharedDataHub::instance().optics_storage().acquire_write(handle_)) {
        accessor->geometry_handle = geo.get_handle();
    } else {
        CFW_LOG_ERROR("[Optics] Failed to acquire write access to optics storage");
        SharedDataHub::instance().optics_storage().deallocate(handle_);
        handle_ = 0;
    }
}

Corona::API::Optics::~Optics() {
    if (handle_ != 0) {
        SharedDataHub::instance().optics_storage().deallocate(handle_);
    }
}

std::uintptr_t Corona::API::Optics::get_handle() const {
    return handle_;
}

Corona::API::Geometry* Corona::API::Optics::get_geometry() const {
    return geometry_;
}

// ########################
//       Mechanics
// ########################
Corona::API::Mechanics::Mechanics(Geometry& geo)
    : geometry_(&geo), handle_(0) {
    // 获取模型的包围盒信息
    ktm::fvec3 max_xyz;
    max_xyz.x = 0.0f;
    max_xyz.y = 0.0f;
    max_xyz.z = 0.0f;

    ktm::fvec3 min_xyz;
    min_xyz.x = 0.0f;
    min_xyz.y = 0.0f;
    min_xyz.z = 0.0f;

    if (auto geom_handle = SharedDataHub::instance().geometry_storage().try_acquire_read(geo.get_handle())) {
        if (auto res_handle = SharedDataHub::instance().model_resource_storage().try_acquire_read(geom_handle->model_resource_handle)) {
            if (res_handle->model_id) {
                if (auto scene = Resource::ResourceManager::get_instance().acquire_read<Resource::Scene>(res_handle->model_id)) {
                    auto max = scene->get_scene_aabb().max;
                    auto min = scene->get_scene_aabb().min;
                    max_xyz.x = max[0];
                    max_xyz.y = max[1];
                    max_xyz.z = max[2];
                    min_xyz.x = min[0];
                    min_xyz.y = min[1];
                    min_xyz.z = min[2];
                } else {
                    CFW_LOG_WARNING("[Mechanics] Failed to acquire scene resource; using default AABB");
                }
            }
        } else {
            CFW_LOG_WARNING("[Mechanics] Failed to read model resource; using default AABB");
        }
    } else {
        CFW_LOG_WARNING("[Mechanics] Failed to read geometry; using default AABB");
    }

    // 创建 MechanicsDevice
    handle_ = SharedDataHub::instance().mechanics_storage().allocate();
    if (auto accessor = SharedDataHub::instance().mechanics_storage().acquire_write(handle_)) {
        accessor->geometry_handle = geo.get_handle();
        accessor->max_xyz = max_xyz;
        accessor->min_xyz = min_xyz;
    } else {
        CFW_LOG_ERROR("[Mechanics] Failed to acquire write access to mechanics storage");
        SharedDataHub::instance().mechanics_storage().deallocate(handle_);
        handle_ = 0;
    }
}

Corona::API::Mechanics::~Mechanics() {
    if (handle_ != 0) {
        SharedDataHub::instance().mechanics_storage().deallocate(handle_);
    }
}

std::uintptr_t Corona::API::Mechanics::get_handle() const {
    return handle_;
}

Corona::API::Geometry* Corona::API::Mechanics::get_geometry() const {
    return geometry_;
}

// ########################
//       Acoustics
// ########################
Corona::API::Acoustics::Acoustics(Geometry& geo)
    : geometry_(&geo), handle_(0) {
    handle_ = SharedDataHub::instance().acoustics_storage().allocate();
    if (auto accessor = SharedDataHub::instance().acoustics_storage().acquire_write(handle_)) {
        accessor->geometry_handle = geo.get_handle();
    } else {
        CFW_LOG_ERROR("[Acoustics] Failed to acquire write access to acoustics storage");
        SharedDataHub::instance().acoustics_storage().deallocate(handle_);
        handle_ = 0;
    }
}

Corona::API::Acoustics::~Acoustics() {
    if (handle_ != 0) {
        SharedDataHub::instance().acoustics_storage().deallocate(handle_);
    }
}

void Corona::API::Acoustics::set_volume(float volume) {
    if (handle_ == 0) {
        CFW_LOG_WARNING("[Acoustics::set_volume] Invalid acoustics handle");
        return;
    }

    if (auto accessor = SharedDataHub::instance().acoustics_storage().acquire_write(handle_)) {
        accessor->volume = volume;
    } else {
        CFW_LOG_ERROR("[Acoustics::set_volume] Failed to acquire write access to acoustics storage");
    }
}

float Corona::API::Acoustics::get_volume() const {
    if (handle_ == 0) {
        CFW_LOG_WARNING("[Acoustics::get_volume] Invalid acoustics handle");
        return 0.0f;
    }

    float result = 0.0f;
    if (auto accessor = SharedDataHub::instance().acoustics_storage().acquire_read(handle_)) {
        result = accessor->volume;
    } else {
        CFW_LOG_ERROR("[Acoustics::get_volume] Failed to acquire read access to acoustics storage");
    }
    return result;
}

std::uintptr_t Corona::API::Acoustics::get_handle() const {
    return handle_;
}

Corona::API::Geometry* Corona::API::Acoustics::get_geometry() const {
    return geometry_;
}

// ########################
//       Kinematics
// ########################
Corona::API::Kinematics::Kinematics(Geometry& geo)
    : geometry_(&geo), handle_(0) {
    handle_ = SharedDataHub::instance().kinematics_storage().allocate();
}

Corona::API::Kinematics::~Kinematics() {
    if (handle_ != 0) {
        SharedDataHub::instance().kinematics_storage().deallocate(handle_);
    }
}

void Corona::API::Kinematics::set_animation(std::uint32_t animation_index) {
    CFW_LOG_WARNING("[Kinematics::set_animation] Not implemented yet");
}

void Corona::API::Kinematics::play_animation(float speed) {
    CFW_LOG_WARNING("[Kinematics::play_animation] Not implemented yet");
}

void Corona::API::Kinematics::stop_animation() {
    CFW_LOG_WARNING("[Kinematics::stop_animation] Not implemented yet");
}

std::uint32_t Corona::API::Kinematics::get_animation_index() const {
    CFW_LOG_WARNING("[Kinematics::get_animation_index] Not implemented yet");
    return 0;
}

float Corona::API::Kinematics::get_current_time() const {
    CFW_LOG_WARNING("[Kinematics::get_current_time] Not implemented yet");
    return 0.0f;
}

std::uintptr_t Corona::API::Kinematics::get_handle() const {
    return handle_;
}

Corona::API::Geometry* Corona::API::Kinematics::get_geometry() const {
    return geometry_;
}

// ########################
//          Actor
// ########################
Corona::API::Actor::Actor()
    : handle_(0), active_profile_handle_(0), next_profile_handle_(1) {
    handle_ = SharedDataHub::instance().actor_storage().allocate();
}

Corona::API::Actor::~Actor() {
    if (handle_ != 0) {
        SharedDataHub::instance().actor_storage().deallocate(handle_);
    }
}

Corona::API::Actor::Profile* Corona::API::Actor::add_profile(const Profile& profile) {
    if (!profile.geometry) {
        CFW_LOG_CRITICAL("[Actor::add_profile] Profile must have a valid Geometry");
        return nullptr;
    }

    if (profile.optics && profile.optics->geometry_ != profile.geometry) {
        CFW_LOG_CRITICAL("[Actor::add_profile] Optics references a different Geometry");
        return nullptr;
    }

    if (profile.mechanics && profile.mechanics->geometry_ != profile.geometry) {
        CFW_LOG_CRITICAL("[Actor::add_profile] Mechanics references a different Geometry");
        return nullptr;
    }

    if (profile.acoustics && profile.acoustics->geometry_ != profile.geometry) {
        CFW_LOG_CRITICAL("[Actor::add_profile] Acoustics references a different Geometry");
        return nullptr;
    }

    if (profile.kinematics && profile.kinematics->geometry_ != profile.geometry) {
        CFW_LOG_CRITICAL("[Actor::add_profile] Kinematics references a different Geometry");
        return nullptr;
    }

    std::uintptr_t profile_handle = next_profile_handle_++;
    profiles_[profile_handle] = profile;

    if (active_profile_handle_ == 0) {
        active_profile_handle_ = profile_handle;
    }

    // 将 Profile 写入引擎侧 ProfileStorage，ActorDevice 只保存 ProfileStorage 的句柄
    std::uintptr_t storage_profile_handle = SharedDataHub::instance().profile_storage().allocate();
    if (auto p = SharedDataHub::instance().profile_storage().acquire_write(storage_profile_handle)) {
        // Actor 作为组件类的 friend，可以读取组件句柄；但不能直接调用 Geometry 的受保护 get_handle()
        p->optics_handle = profile.optics ? profile.optics->get_handle() : 0;
        p->acoustics_handle = profile.acoustics ? profile.acoustics->get_handle() : 0;
        p->mechanics_handle = profile.mechanics ? profile.mechanics->get_handle() : 0;
        p->kinematics_handle = profile.kinematics ? profile.kinematics->get_handle() : 0;
        p->geometry_handle = 0;
    } else {
        CFW_LOG_ERROR("[Actor::add_profile] Failed to acquire write access to profile storage");
        SharedDataHub::instance().profile_storage().deallocate(storage_profile_handle);
        storage_profile_handle = 0;
    }

    if (handle_ != 0) {
        if (auto accessor = SharedDataHub::instance().actor_storage().acquire_write(handle_)) {
            if (storage_profile_handle != 0) {
                accessor->profile_handles.push_back(storage_profile_handle);
            }
        } else {
            CFW_LOG_ERROR("[Actor::add_profile] Failed to acquire write access to actor storage");
        }
    }

    return &profiles_[profile_handle];
}

void Corona::API::Actor::remove_profile(const Profile* profile) {
    if (handle_ == 0 || profile == nullptr) {
        CFW_LOG_WARNING("[Actor::remove_profile] Invalid actor handle or null profile");
        return;
    }

    std::uintptr_t profile_handle = 0;
    for (const auto& [handle, prof] : profiles_) {
        if (&prof == profile) {
            profile_handle = handle;
            break;
        }
    }

    if (profile_handle == 0) {
        CFW_LOG_WARNING("[Actor::remove_profile] Profile not found in this actor");
        return;
    }

    auto it = profiles_.find(profile_handle);
    if (it == profiles_.end()) {
        return;
    }

    if (it->second.kinematics) {
        it->second.kinematics->stop_animation();
    }

    profiles_.erase(it);

    if (active_profile_handle_ == profile_handle) {
        if (!profiles_.empty()) {
            active_profile_handle_ = profiles_.begin()->first;
            CFW_LOG_INFO("[Actor::remove_profile] Switched to first available profile");
        } else {
            active_profile_handle_ = 0;
            CFW_LOG_INFO("[Actor::remove_profile] No profiles remaining");
        }
    }

    if (auto accessor = SharedDataHub::instance().actor_storage().acquire_write(handle_)) {
        std::erase(accessor->profile_handles, profile_handle);
    }
}

void Corona::API::Actor::set_active_profile(const Profile* profile) {
    if (profile == nullptr) {
        CFW_LOG_WARNING("[Actor::set_active_profile] Null profile pointer");
        return;
    }

    for (const auto& [handle, prof] : profiles_) {
        if (&prof == profile) {
            active_profile_handle_ = handle;
            return;
        }
    }

    CFW_LOG_WARNING("[Actor::set_active_profile] Profile not found in this actor");
}

Corona::API::Actor::Profile* Corona::API::Actor::get_active_profile() {
    if (active_profile_handle_ == 0) return nullptr;
    auto it = profiles_.find(active_profile_handle_);
    return (it != profiles_.end()) ? &it->second : nullptr;
}

std::size_t Corona::API::Actor::profile_count() const {
    return profiles_.size();
}

std::uintptr_t Corona::API::Actor::get_handle() const {
    return handle_;
}

// ########################
//          Camera
// ########################
Corona::API::Camera::Camera()
    : handle_(0) {
    ktm::fvec3 pos_vec;
    pos_vec.x = 0.0f;
    pos_vec.y = 0.0f;
    pos_vec.z = -5.0f;

    ktm::fvec3 fwd_vec;
    fwd_vec.x = 0.0f;
    fwd_vec.y = 0.0f;
    fwd_vec.z = 1.0f;

    ktm::fvec3 up_vec;
    up_vec.x = 0.0f;
    up_vec.y = 1.0f;
    up_vec.z = 0.0f;

    float fov = 45.0f;

    handle_ = SharedDataHub::instance().camera_storage().allocate();
    if (auto accessor = SharedDataHub::instance().camera_storage().acquire_write(handle_)) {
        accessor->position = pos_vec;
        accessor->forward = fwd_vec;
        accessor->world_up = up_vec;
        accessor->fov = fov;
        accessor->surface = get_default_surface();
    } else {
        CFW_LOG_ERROR("[Camera] Failed to acquire write access to camera storage");
        SharedDataHub::instance().camera_storage().deallocate(handle_);
        handle_ = 0;
    }
}

Corona::API::Camera::Camera(const std::array<float, 3>& position, const std::array<float, 3>& forward, const std::array<float, 3>& world_up, float fov)
    : handle_(0) {
    ktm::fvec3 pos_vec;
    pos_vec.x = position[0];
    pos_vec.y = position[1];
    pos_vec.z = position[2];

    ktm::fvec3 fwd_vec;
    fwd_vec.x = forward[0];
    fwd_vec.y = forward[1];
    fwd_vec.z = forward[2];

    ktm::fvec3 up_vec;
    up_vec.x = world_up[0];
    up_vec.y = world_up[1];
    up_vec.z = world_up[2];

    handle_ = SharedDataHub::instance().camera_storage().allocate();
    if (auto accessor = SharedDataHub::instance().camera_storage().acquire_write(handle_)) {
        accessor->position = pos_vec;
        accessor->forward = fwd_vec;
        accessor->world_up = up_vec;
        accessor->fov = fov;
        accessor->surface = get_default_surface();
    } else {
        CFW_LOG_ERROR("[Camera] Failed to acquire write access to camera storage");
        SharedDataHub::instance().camera_storage().deallocate(handle_);
        handle_ = 0;
    }
}

Corona::API::Camera::~Camera() {
    if (handle_) {
        SharedDataHub::instance().camera_storage().deallocate(handle_);
        handle_ = 0;
    }
}

void Corona::API::Camera::set(const std::array<float, 3>& position, const std::array<float, 3>& forward, const std::array<float, 3>& world_up, float fov) {
    if (handle_ == 0) {
        CFW_LOG_WARNING("[Camera::set] Invalid camera handle");
        return;
    }

    ktm::fvec3 pos_vec;
    pos_vec.x = position[0];
    pos_vec.y = position[1];
    pos_vec.z = position[2];

    ktm::fvec3 fwd_vec;
    fwd_vec.x = forward[0];
    fwd_vec.y = forward[1];
    fwd_vec.z = forward[2];

    ktm::fvec3 up_vec;
    up_vec.x = world_up[0];
    up_vec.y = world_up[1];
    up_vec.z = world_up[2];

    if (auto accessor = SharedDataHub::instance().camera_storage().acquire_write(handle_)) {
        accessor->position = pos_vec;
        accessor->forward = fwd_vec;
        accessor->world_up = up_vec;
        accessor->fov = fov;
    } else {
        CFW_LOG_ERROR("[Camera::set] Failed to acquire write access to camera storage");
    }
}

std::array<float, 3> Corona::API::Camera::get_position() const {
    if (handle_ == 0) {
        CFW_LOG_WARNING("[Camera::get_position] Invalid camera handle");
        return {0.0f, 0.0f, 0.0f};
    }

    std::array result = {0.0f, 0.0f, 0.0f};
    if (auto accessor = SharedDataHub::instance().camera_storage().acquire_read(handle_)) {
        result[0] = accessor->position.x;
        result[1] = accessor->position.y;
        result[2] = accessor->position.z;
    } else {
        CFW_LOG_ERROR("[Camera::get_position] Failed to acquire read access to camera storage");
    }

    return result;
}

std::array<float, 3> Corona::API::Camera::get_forward() const {
    if (handle_ == 0) {
        CFW_LOG_WARNING("[Camera::get_forward] Invalid camera handle");
        return {0.0f, 0.0f, 1.0f};
    }

    std::array result = {0.0f, 0.0f, 1.0f};
    if (auto accessor = SharedDataHub::instance().camera_storage().acquire_read(handle_)) {
        result[0] = accessor->forward.x;
        result[1] = accessor->forward.y;
        result[2] = accessor->forward.z;
    } else {
        CFW_LOG_ERROR("[Camera::get_forward] Failed to acquire read access to camera storage");
    }

    return result;
}

std::array<float, 3> Corona::API::Camera::get_world_up() const {
    if (handle_ == 0) {
        CFW_LOG_WARNING("[Camera::get_world_up] Invalid camera handle");
        return {0.0f, 1.0f, 0.0f};
    }

    std::array result = {0.0f, 1.0f, 0.0f};
    if (auto accessor = SharedDataHub::instance().camera_storage().acquire_read(handle_)) {
        result[0] = accessor->world_up.x;
        result[1] = accessor->world_up.y;
        result[2] = accessor->world_up.z;
    } else {
        CFW_LOG_ERROR("[Camera::get_world_up] Failed to acquire read access to camera storage");
    }

    return result;
}

float Corona::API::Camera::get_fov() const {
    if (handle_ == 0) {
        CFW_LOG_WARNING("[Camera::get_fov] Invalid camera handle");
        return 45.0f;
    }

    float result = 45.0f;
    if (auto accessor = SharedDataHub::instance().camera_storage().acquire_read(handle_)) {
        result = accessor->fov;
    } else {
        CFW_LOG_ERROR("[Camera::get_fov] Failed to acquire read access to camera storage");
    }

    return result;
}

std::uintptr_t Corona::API::Camera::get_handle() const {
    return handle_;
}

void Corona::API::Camera::set_surface(void* surface) {
    if (handle_ == 0) {
        CFW_LOG_WARNING("[Camera::set_surface] Invalid camera handle");
        return;
    }

    if (auto accessor = SharedDataHub::instance().camera_storage().acquire_write(handle_)) {
        accessor->surface = surface;
    } else {
        CFW_LOG_ERROR("[Camera::set_surface] Failed to acquire write access to camera storage");
        return;
    }

    if (auto* event_bus = Kernel::KernelContext::instance().event_bus()) {
        event_bus->publish<Events::DisplaySurfaceChangedEvent>({reinterpret_cast<uint64_t>(surface)});
    }
}

// ########################
//      ImageEffects
// ########################
Corona::API::ImageEffects::ImageEffects()
    : handle_(0) {
}

Corona::API::ImageEffects::~ImageEffects() {
    if (handle_ != 0) {
        // Corona::SharedDataHub::instance().image_effects_storage().deallocate(handle_);
        handle_ = 0;
    }
}

// ########################
//        Viewport
// ########################
Corona::API::Viewport::Viewport()
    : handle_(0) {
    handle_ = SharedDataHub::instance().viewport_storage().allocate();
    if (auto accessor = SharedDataHub::instance().viewport_storage().acquire_write(handle_)) {
        accessor->camera = 0;  // 初始无 Camera
    } else {
        CFW_LOG_ERROR("[Viewport] Failed to acquire write access to viewport storage");
        SharedDataHub::instance().viewport_storage().deallocate(handle_);
        handle_ = 0;
    }
}

Corona::API::Viewport::Viewport(int width, int height, bool light_field)
    : handle_(0), width_(width), height_(height) {
    handle_ = SharedDataHub::instance().viewport_storage().allocate();
    if (auto accessor = SharedDataHub::instance().viewport_storage().acquire_write(handle_)) {
        accessor->camera = 0;  // 初始无 Camera
    } else {
        CFW_LOG_ERROR("[Viewport] Failed to acquire write access to viewport storage");
        SharedDataHub::instance().viewport_storage().deallocate(handle_);
        handle_ = 0;
    }
}

Corona::API::Viewport::~Viewport() {
    if (handle_ != 0) {
        SharedDataHub::instance().viewport_storage().deallocate(handle_);
        handle_ = 0;
    }
}

void Corona::API::Viewport::set_camera(Camera* camera) {
    if (handle_ == 0) {
        CFW_LOG_WARNING("[Viewport::set_camera] Invalid viewport handle");
        return;
    }

    camera_ = camera;

    if (auto accessor = SharedDataHub::instance().viewport_storage().acquire_write(handle_)) {
        accessor->camera = camera_ ? camera_->get_handle() : 0;
    } else {
        CFW_LOG_ERROR("[Viewport::set_camera] Failed to acquire write access to viewport storage");
        return;
    }

    if (camera_ != nullptr)
    {
        if (void* surface = get_default_surface(); surface != nullptr)
        {
            camera_->set_surface(surface);
        }
    }
}

Corona::API::Camera* Corona::API::Viewport::get_camera() {
    return camera_;
}

bool Corona::API::Viewport::has_camera() const {
    return camera_ != nullptr;
}

void Corona::API::Viewport::remove_camera() {
    if (handle_ == 0) return;

    camera_ = nullptr;

    if (auto accessor = SharedDataHub::instance().viewport_storage().acquire_write(handle_)) {
        accessor->camera = 0;
    }
}

void Corona::API::Viewport::set_image_effects(ImageEffects* effects) {
    image_effects_ = effects;
    // TODO: 如果有 image_effects_storage，在此写入
}

Corona::API::ImageEffects* Corona::API::Viewport::get_image_effects() {
    return image_effects_;
}

bool Corona::API::Viewport::has_image_effects() const {
    return image_effects_ != nullptr;
}

void Corona::API::Viewport::remove_image_effects() {
    image_effects_ = nullptr;
    // TODO: 如果有 image_effects_storage，在此清理
}

void Corona::API::Viewport::set_size(int width, int height) {
    if (handle_ == 0) {
        CFW_LOG_WARNING("[Viewport::set_size] Invalid viewport handle");
        return;
    }

    if (width <= 0 || height <= 0) {
        CFW_LOG_WARNING("[Viewport::set_size] Invalid size: {}x{}", width, height);
        return;
    }

    width_ = width;
    height_ = height;

    // Viewport 的大小可能需要在 ViewportDevice 中存储
    if (auto accessor = SharedDataHub::instance().viewport_storage().acquire_write(handle_)) {
        // TODO: 如果 ViewportDevice 有 width/height 字段，在此设置
    }
}

void Corona::API::Viewport::set_viewport_rect(int x, int y, int width, int height) {
    // TODO: Implement viewport rectangle settings
    CFW_LOG_WARNING("[Corona::API::Viewport::set_viewport_rect] Not implemented yet");
}

void Corona::API::Viewport::pick_actor_at_pixel(int x, int y) const {
    // TODO: Implement pixel picking for actor selection
    CFW_LOG_WARNING("[Corona::API::Viewport::pick_actor_at_pixel] Not implemented yet");
}

void Corona::API::Viewport::save_screenshot(const std::string& path) const {
    // TODO: Implement screenshot functionality
    CFW_LOG_WARNING("[Corona::API::Viewport::save_screenshot] Not implemented yet: {}", path);
}

std::uintptr_t Corona::API::Viewport::get_handle() const {
    return handle_;
}

namespace Corona::API
{
void set_default_surface(void* surface)
{
    g_default_surface.store(surface, std::memory_order_relaxed);

    // 句柄到达时，补写到已存在的相机，避免“先有 camera 后有 surface”的空窗。
    for (auto& camera : SharedDataHub::instance().camera_storage())
    {
        camera.surface = surface;
    }
}

void* get_default_surface()
{
    return g_default_surface.load(std::memory_order_relaxed);
}
} // namespace Corona::API


