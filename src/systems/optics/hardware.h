#pragma once

#include <corona/shader_include.h>
#include <CabbageHardware.h>
#include <optional>

#include GLSL(../../../assets/shaders/test.vert.glsl)
#include GLSL(../../../assets/shaders/test.frag.glsl)
#include GLSL(../../../assets/shaders/test.comp.glsl)

struct Hardware {
    HardwareImage gbufferPostionImage;
    HardwareImage gbufferBaseColorImage;
    HardwareImage gbufferNormalImage;
    HardwareImage gbufferMotionVectorImage;
    HardwareImage gbufferDepthImage;

    HardwareImage finalOutputImage;
    HardwareExecutor executor;

    HardwareBuffer uniformBuffer;
    HardwareBuffer gbufferUniformBuffer;
    HardwareBuffer computeUniformBuffer;

    bool shaderHasInit = false;
    std::optional<RasterizerPipeline<test_vert_glsl, test_frag_glsl>> rasterizerPipeline;
    std::optional<ComputePipeline<test_comp_glsl>> computePipeline;

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

    struct ComputeUniformBufferObject {
        float time;
        ktm::fvec2 imageSize;
        uint32_t inputImageID;
        uint32_t outputImageID;
    } computeUniformBufferObjects{};

    // 渲染大小
    ktm::uvec2 gbufferSize{};
};
