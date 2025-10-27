#pragma once

#include <CabbageHardware.h>
#include <Pipeline/ComputePipeline.h>
#include <Pipeline/RasterizerPipeline.h>
#include <corona/interfaces/Services.h>
#include <corona/interfaces/ThreadedSystem.h>

#include <memory>
#include <optional>
#include <unordered_set>

namespace Corona {
class DataCacheHub;
}  // namespace Corona

namespace Corona {
class Shader;
class Model;

class RenderingSystem final : public ThreadedSystem {
   public:
    RenderingSystem();
    void configure(const Interfaces::SystemContext& context) override;

    // 保持与现有 API 兼容的模型观测接口
    void watch_model(uint64_t id);
    void unwatch_model(uint64_t id);

    // 显式初始化着色器/管线（可通过外部事件或资源回调触发）
    void init_shader(std::shared_ptr<Shader> shader);

   protected:
    void onStart() override;
    void onTick() override;
    void onStop() override;

   private:
    void init();
    void update_engine();

    struct CameraSnapshot {
        float fov = 45.0f;
        ktm::fvec3 pos{};
        ktm::fvec3 forward{};
        ktm::fvec3 worldUp{};
    };
    struct SceneSnapshot {
        CameraSnapshot camera{};
        ktm::fvec3 sunDir{};
        void* surface = nullptr;
    };

    struct TRS {
        std::optional<ktm::fvec3> pos;
        std::optional<ktm::fvec3> rot;
        std::optional<ktm::fvec3> scale;
    };

    void gbuffer_pipeline(const CameraSnapshot& cam);
    void composite_pipeline(ktm::fvec3 sunDir);

    // 渲染资源
    HardwareImage gbufferPostionImage;
    HardwareImage gbufferBaseColorImage;
    HardwareImage gbufferNormalImage;
    HardwareImage gbufferMotionVectorImage;
    HardwareImage finalOutputImage;

    HardwareBuffer uniformBuffer;
    HardwareBuffer gbufferUniformBuffer;

    bool shaderHasInit = false;
    RasterizerPipeline rasterizerPipeline;
    ComputePipeline computePipeline;
    HardwareExecutor executor;

    struct UniformBufferObject {
        ktm::fvec3 eyePosition;
        float padding0;
        ktm::fvec3 eyeDir;
        float padding1;
        ktm::fmat4x4 eyeViewMatrix;
        ktm::fmat4x4 eyeProjMatrix;
    } uniformBufferObjects{};

    struct gbufferUniformBufferObject {
        ktm::fmat4x4 viewProjMatrix;
    } gbufferUniformBufferObjects{};

    // 渲染大小
    ktm::uvec2 gbufferSize{};

    // 被观测的模型
    std::unordered_set<uint64_t> model_cache_keys_{};
    
    std::shared_ptr<Interfaces::IResourceService> resource_service_{};
    std::shared_ptr<Interfaces::ICommandScheduler> scheduler_{};
    Interfaces::ICommandScheduler::QueueHandle system_queue_handle_{};
    DataCacheHub* data_cache_hub_ = nullptr;
};
}  // namespace Corona