#version 460
#extension GL_EXT_nonuniform_qualifier : enable

layout (local_size_x = 8, local_size_y = 8) in;

layout (set = 0, binding = 0) uniform sampler2D textures[];
layout (set = 2, binding = 0, rgba16) uniform image2D inputImageRGBA16[];

layout(push_constant) uniform PushConsts
{
    uvec2 gbufferSize;
    uint gbufferPostionImage;
    uint gbufferBaseColorImage;
    uint gbufferNormalImage;
    uint gbufferDepthImage;

    vec3 lightColor;
    vec3 sun_dir;

    uint finalOutputImage;
    uint uniformBufferIndex;
} pushConsts;

layout(set = 1, binding = 0) buffer UniformBufferObject
{
    // Light data
    vec3 lightPosition;
    float padding0;
    mat4 lightViewMatrix;
    mat4 lightProjMatrix;

    // Eye/Camera data
    vec3 eyePosition;
    float padding1;
    vec3 eyeDir;
    float padding2;
    mat4 eyeViewMatrix;
    mat4 eyeProjMatrix;
} uniformBufferObjects[];


// ----------------------------------------------------------------------------
// PBR: GGX/Cook-Torrance BRDF
// ----------------------------------------------------------------------------

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = 3.14159265359 * denom * denom;

    return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 calculateColor(vec3 WorldPos, vec3 Normal,
    vec3 lightPos, vec3 lightColor,
    vec3 albedo,
    float metallic,
    float roughness)
{
    vec3 N = normalize(Normal);
    vec3 V = normalize(uniformBufferObjects[pushConsts.uniformBufferIndex].eyePosition - WorldPos);

    vec3 F0 = vec3(0.04);
    F0 = mix(F0, albedo, metallic);

    vec3 Lo = vec3(0.0);
    {
        vec3 L = normalize(pushConsts.sun_dir);
        vec3 H = normalize(V + L);
        float attenuation = 1.0;
        vec3 radiance = lightColor * attenuation;

        float NDF = DistributionGGX(N, H, roughness);
        float G   = GeometrySmith(N, V, L, roughness);
        vec3 F    = fresnelSchlick(clamp(dot(H, V), 0.0, 1.0), F0);

        vec3 numerator    = NDF * G * F;
        float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
        vec3 specular = numerator / denominator;

        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;

        float NdotL = max(dot(N, L), 0.0);
        Lo += (kD * albedo / 3.14159265359 + specular) * radiance * NdotL;
    }

    vec3 ambient = vec3(0.03) * albedo;
    return ambient + Lo;
}


void main()
{
    if (gl_GlobalInvocationID.x >= pushConsts.gbufferSize.x ||
        gl_GlobalInvocationID.y >= pushConsts.gbufferSize.y) {
        return;
    }

    vec2 screenUV = vec2(float(gl_GlobalInvocationID.x) / float(pushConsts.gbufferSize.x),
                         float(gl_GlobalInvocationID.y) / float(pushConsts.gbufferSize.y));
    float gbufferDepth = texture(textures[pushConsts.gbufferDepthImage], screenUV).r;

    vec3 renderResult = vec3(0.0);

    if (gbufferDepth < (1.0 - 1e-3))
    {
        vec4 gbufferPostion  = imageLoad(inputImageRGBA16[pushConsts.gbufferPostionImage],  ivec2(gl_GlobalInvocationID.xy));
        vec4 gbufferBaseColor = imageLoad(inputImageRGBA16[pushConsts.gbufferBaseColorImage], ivec2(gl_GlobalInvocationID.xy));
        vec4 gbufferNormal   = imageLoad(inputImageRGBA16[pushConsts.gbufferNormalImage],   ivec2(gl_GlobalInvocationID.xy));

        renderResult = calculateColor(gbufferPostion.xyz, gbufferNormal.xyz,
            uniformBufferObjects[pushConsts.uniformBufferIndex].lightPosition,
            pushConsts.lightColor,
            gbufferBaseColor.xyz, 0.5, 0.5);
        renderResult = max(renderResult, vec3(0.01, 0.01, 0.01));
    }

    imageStore(inputImageRGBA16[pushConsts.finalOutputImage], ivec2(gl_GlobalInvocationID.xy), vec4(renderResult, 1.0));
}
