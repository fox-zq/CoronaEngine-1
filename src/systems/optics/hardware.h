#pragma once

#include <CabbageHardware.h>

struct Hardware {
    HardwareImage gbufferPostionImage;
    HardwareImage gbufferBaseColorImage;
    HardwareImage gbufferNormalImage;
    HardwareImage gbufferMotionVectorImage;
    HardwareImage gbufferDepthImage;

    // Double-buffered output: write_index is the buffer currently being rendered to,
    // the other buffer holds the last completed frame for DisplaySystem to consume.
    static constexpr int BUFFER_COUNT = 2;
    HardwareImage finalOutputImage[BUFFER_COUNT];
    HardwareExecutor executor[BUFFER_COUNT];
    int write_index = 0;

    HardwareBuffer uniformBuffer;
    HardwareBuffer gbufferUniformBuffer;
    HardwareBuffer computeUniformBuffer;

    bool shaderHasInit = false;
    RasterizerPipeline rasterizerPipeline;
    ComputePipeline computePipeline;

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