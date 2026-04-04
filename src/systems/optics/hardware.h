#pragma once

#include <corona/shader_include.h>
#include <CabbageHardware.h>
#include <optional>

#include GLSL(../../../assets/shaders/test.vert.glsl)
#include GLSL(../../../assets/shaders/test.frag.glsl)
#include GLSL(../../../assets/shaders/lighting.comp.glsl)
#include GLSL(../../../assets/shaders/sky.comp.glsl)
#include GLSL(../../../assets/shaders/tonemap.comp.glsl)

struct Hardware {
    HardwareImage gbufferPostionImage;
    HardwareImage gbufferBaseColorImage;
    HardwareImage gbufferNormalImage;
    HardwareImage gbufferMotionVectorImage;
    HardwareImage gbufferDepthImage;

    HardwareImage gbufferObjectIDImage;

    HardwareImage finalOutputImage;
    HardwareExecutor executor;

    HardwareBuffer uniformBuffer;
    HardwareBuffer gbufferUniformBuffer;

    bool shaderHasInit = false;
    std::optional<RasterizerPipeline<test_vert_glsl, test_frag_glsl>> rasterizerPipeline;
    std::optional<ComputePipeline<lighting_comp_glsl>> lightingPipeline;
    std::optional<ComputePipeline<sky_comp_glsl>> skyPipeline;
    std::optional<ComputePipeline<tonemap_comp_glsl>> tonemapPipeline;

    struct UniformBufferObject {
        // Light data (for shadow mapping, etc.)
        ktm::fvec3 lightPosition;
        float padding0;
        ktm::fmat4x4 lightViewMatrix;
        ktm::fmat4x4 lightProjMatrix;

        // Eye/Camera data
        ktm::fvec3 eyePosition;
        float padding1;
        ktm::fvec3 eyeDir;
        float padding2;
        ktm::fmat4x4 eyeViewMatrix;
        ktm::fmat4x4 eyeProjMatrix;
    } uniformBufferObjects{};

    struct gbufferUniformBufferObject {
        ktm::fmat4x4 viewProjMatrix;
    } gbufferUniformBufferObjects{};

    // 渲染大小
    ktm::uvec2 gbufferSize{};
};
