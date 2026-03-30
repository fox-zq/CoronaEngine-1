#version 460
#extension GL_EXT_nonuniform_qualifier : enable

layout(push_constant) uniform PushConsts
{
    layout(offset = 0) vec2 scale;
    layout(offset = 8) vec2 translate;
    layout(offset = 16) vec4 clip_rect;
    layout(offset = 32) uint texture_index;
} pushConsts;

layout(set = 0, binding = 0) uniform sampler2D textures[];

layout(location = 0) in vec2 frag_uv;
layout(location = 1) in vec4 frag_color;

layout(location = 0) out vec4 out_color;

void main()
{
    vec2 p = gl_FragCoord.xy;
    float inside_x = step(pushConsts.clip_rect.x, p.x) * (1.0 - step(pushConsts.clip_rect.z, p.x));
    float inside_y = step(pushConsts.clip_rect.y, p.y) * (1.0 - step(pushConsts.clip_rect.w, p.y));
    float inside = inside_x * inside_y;

    vec4 tex_color = texture(textures[nonuniformEXT(pushConsts.texture_index)], frag_uv);
    vec4 linear_vert_color = vec4(pow(frag_color.rgb, vec3(2.2)), frag_color.a);
    out_color = linear_vert_color * tex_color * inside;
}
