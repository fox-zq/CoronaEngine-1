#version 460
#extension GL_EXT_nonuniform_qualifier : enable

layout(push_constant) uniform PushConsts
{    
    uint textureIndex;
    uint uniformBufferIndex;
    mat4 modelMatrix;
    vec4 materialColor;  // .a = opacity for transparent materials
} pushConsts;

layout(set = 1, binding = 0) buffer UniformBufferObject
{
    mat4 viewProjMatrix;
} uniformBufferObjects[];


layout (set = 0, binding = 0) uniform sampler2D textures[];

layout(location = 0) in vec3 fragPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;
layout(location = 3) in vec2 fragMotionVector;

layout(location = 0) out vec3 gbufferPostion;
layout(location = 1) out vec3 gbufferNormal;
layout(location = 2) out vec4 gbufferBaseColor;
layout(location = 3) out vec2 gbufferMotionVector;

void main()
{
    vec4 sampleColor = texture(textures[pushConsts.textureIndex], fragTexCoord);
    
    // 获取材质的透明度 (alpha)
    float materialAlpha = pushConsts.materialColor.a;
    float finalAlpha = sampleColor.a * materialAlpha;
    
    // Alpha-cutout: 只对完全不透明的材质 (alpha=1.0) 使用硬裁剪
    // 对于半透明材质，使用更低的阈值
    float alphaCutoff = (materialAlpha < 1.0f) ? 0.01f : 0.5f;
    if (finalAlpha < alphaCutoff) {
        discard;
    }

    // 材质颜色乘以纹理颜色
    vec3 finalColor = sampleColor.rgb * pushConsts.materialColor.rgb;
    
    // 对于玻璃材质，增加一些环境反射感
    if (materialAlpha < 1.0f) {
        // 玻璃材质：使用浅蓝/白色调
        finalColor = mix(finalColor, vec3(0.9, 0.95, 1.0), 0.3);
    }
    
    // 输出 alpha 到 gbuffer，以便后续合成
    gbufferBaseColor = vec4(finalColor, finalAlpha);
    
    // Flip normal for back-facing fragments (double-sided rendering support)
    vec3 normal = gl_FrontFacing ? fragNormal : -fragNormal;
    gbufferNormal = normalize(normal);
    
    gbufferPostion = fragPos;
    gbufferMotionVector = fragMotionVector;
}