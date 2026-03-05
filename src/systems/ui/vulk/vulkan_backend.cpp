#include <corona/kernel/core/i_logger.h>
#include <corona/systems/ui/vulkan_backend.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

namespace {
struct ImGuiGpuVertex {
    float pos[2]{};
    float uv[2]{};
    float color[4]{};
};

static constexpr const char* k_imgui_vertex_shader = R"GLSL(
#version 460

// Keep offsets in sync with CPU-side push constant writes.
layout(push_constant) uniform PushConsts
{
    layout(offset = 0) vec2 scale;
    layout(offset = 8) vec2 translate;
    layout(offset = 16) vec4 clip_rect;
    layout(offset = 32) uint texture_index;
} pushConsts;

layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;

layout(location = 0) out vec2 frag_uv;
layout(location = 1) out vec4 frag_color;

void main()
{
    frag_uv = in_uv;
    frag_color = in_color;
    gl_Position = vec4(in_pos * pushConsts.scale + pushConsts.translate, 0.0, 1.0);
}
)GLSL";

static constexpr const char* k_imgui_fragment_shader = R"GLSL(
#version 460
#extension GL_EXT_nonuniform_qualifier : enable

// Keep offsets in sync with CPU-side push constant writes.
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
    out_color = frag_color * tex_color * inside;
}
)GLSL";

inline ktm::fvec4 unpack_imgui_color(const ImU32 color) {
    constexpr float inv_255 = 1.0f / 255.0f;
    const float r = static_cast<float>((color >> IM_COL32_R_SHIFT) & 0xFFu) * inv_255;
    const float g = static_cast<float>((color >> IM_COL32_G_SHIFT) & 0xFFu) * inv_255;
    const float b = static_cast<float>((color >> IM_COL32_B_SHIFT) & 0xFFu) * inv_255;
    const float a = static_cast<float>((color >> IM_COL32_A_SHIFT) & 0xFFu) * inv_255;
    return ktm::fvec4(r, g, b, a);
}

inline uint32_t texture_id_to_descriptor(ImTextureID tex_id) {
    const ImU64 raw = static_cast<ImU64>(tex_id);
    if (raw > static_cast<ImU64>(std::numeric_limits<uint32_t>::max())) {
        CFW_LOG_ERROR("VulkanBackend: ImTextureID out of uint32 range: {}", raw);
    }
    return static_cast<uint32_t>(raw);
}
}  // namespace

namespace Corona::Systems {
VulkanBackend::VulkanBackend(SDL_Window* window)
    : window_(window) {
}

bool VulkanBackend::initialize() {
    if (initialized_) {
        return true;
    }

    if (window_ == nullptr) {
        CFW_LOG_ERROR("VulkanBackend: initialize failed, window is null");
        return false;
    }

    void* native_handle = nullptr;
#if defined(_WIN32)
    native_handle = SDL_GetPointerProperty(
        SDL_GetWindowProperties(window_),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER,
        nullptr);
#elif defined(__APPLE__)
    native_handle = SDL_GetPointerProperty(
        SDL_GetWindowProperties(window_),
        SDL_PROP_WINDOW_COCOA_WINDOW_POINTER,
        nullptr);
#endif

    if (native_handle == nullptr) {
        CFW_LOG_ERROR("VulkanBackend: failed to get native window handle from SDL");
        return false;
    }

    displayer_ = HardwareDisplayer(native_handle);

    int w = 0;
    int h = 0;
    SDL_GetWindowSize(window_, &w, &h);

    if (w > 0 && h > 0) {
        if (!ensure_render_target(static_cast<uint32_t>(w), static_cast<uint32_t>(h))) {
            CFW_LOG_ERROR("VulkanBackend: failed to create initial render target");
            return false;
        }
    } else {
        rebuild_needed_ = true;
    }

    initialized_ = true;
    CFW_LOG_INFO("VulkanBackend: initialized");
    return true;
}

void VulkanBackend::shutdown() {
    if (!initialized_) {
        return;
    }

    executor_.waitForDeferredResources();

    frame_ready_ = false;
    imgui_pipeline_ready_ = false;
    font_ready_ = false;
    rebuild_needed_ = false;

    render_target_ = HardwareImage();
    font_atlas_image_ = HardwareImage();
    imgui_pipeline_ = RasterizerPipeline();

    clear_pixels_.clear();
    render_target_width_ = 0;
    render_target_height_ = 0;

    displayer_ = HardwareDisplayer();

    initialized_ = false;
    CFW_LOG_INFO("VulkanBackend: shutdown");
}

void VulkanBackend::new_frame() {
    if (!initialized_) {
        return;
    }

    executor_.cleanupDeferredResources();
}

void VulkanBackend::render_frame(ImDrawData* draw_data) {
    uint32_t fb_width = 0;
    uint32_t fb_height = 0;
    if (!prepare_frame(draw_data, fb_width, fb_height)) {
        return;
    }

    int total_draw_cmds = 0;
    int recorded_draw_cmds = 0;
    if (!record_draw_lists(draw_data, fb_width, fb_height, total_draw_cmds, recorded_draw_cmds)) {
        return;
    }

    submit_frame(
        fb_width,
        fb_height,
        draw_data->CmdListsCount,
        total_draw_cmds,
        recorded_draw_cmds);
}

bool VulkanBackend::prepare_frame(ImDrawData* draw_data, uint32_t& fb_width, uint32_t& fb_height) {
    if (!initialized_ || draw_data == nullptr) {
        return false;
    }

    const int fb_w = static_cast<int>(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    const int fb_h = static_cast<int>(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    if (fb_w <= 0 || fb_h <= 0) {
        return false;
    }

    fb_width = static_cast<uint32_t>(fb_w);
    fb_height = static_cast<uint32_t>(fb_h);

    if (!ensure_render_target(fb_width, fb_height)) {
        rebuild_needed_ = true;
        return false;
    }

    if (!ensure_imgui_pipeline() || !ensure_font_texture()) {
        return false;
    }

    if (!clear_pixels_.empty()) {
        executor_ << render_target_.copyFrom(clear_pixels_.data());
    }

    imgui_pipeline_["out_color"] = render_target_;
    return true;
}

bool VulkanBackend::record_draw_lists(ImDrawData* draw_data,
                                      uint32_t fb_width,
                                      uint32_t fb_height,
                                      int& total_draw_cmds,
                                      int& recorded_draw_cmds) {
    if (draw_data == nullptr || draw_data->DisplaySize.x == 0.0f || draw_data->DisplaySize.y == 0.0f) {
        return false;
    }

    const float sx = 2.0f / draw_data->DisplaySize.x;
    const float sy = 2.0f / draw_data->DisplaySize.y;
    const ktm::fvec2 scale(sx, sy);
    const ktm::fvec2 translate(-1.0f - draw_data->DisplayPos.x * sx,
                               -1.0f - draw_data->DisplayPos.y * sy);

    for (int cmd_list_index = 0; cmd_list_index < draw_data->CmdListsCount; ++cmd_list_index) {
        const ImDrawList* cmd_list = draw_data->CmdLists[cmd_list_index];
        if (cmd_list == nullptr) {
            continue;
        }

        std::vector<ImGuiGpuVertex> vertices;
        vertices.reserve(static_cast<size_t>(cmd_list->VtxBuffer.Size));
        for (const ImDrawVert& v : cmd_list->VtxBuffer) {
            ImGuiGpuVertex gv{};
            gv.pos[0] = v.pos.x;
            gv.pos[1] = v.pos.y;
            gv.uv[0] = v.uv.x;
            gv.uv[1] = v.uv.y;
            const ktm::fvec4 color = unpack_imgui_color(v.col);
            gv.color[0] = color.x;
            gv.color[1] = color.y;
            gv.color[2] = color.z;
            gv.color[3] = color.w;
            vertices.push_back(gv);
        }

        if (vertices.empty()) {
            continue;
        }

        HardwareBuffer vertex_buffer(vertices, BufferUsage::VertexBuffer);
        if (!vertex_buffer) {
            continue;
        }

        std::vector<ImDrawIdx> indices;
        indices.reserve(static_cast<size_t>(cmd_list->IdxBuffer.Size));
        for (const ImDrawIdx idx : cmd_list->IdxBuffer) {
            indices.push_back(idx);
        }

        if (indices.empty()) {
            continue;
        }

        HardwareBuffer index_buffer(indices, BufferUsage::IndexBuffer);
        if (!index_buffer) {
            continue;
        }

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; ++cmd_i) {
            ++total_draw_cmds;

            const ImDrawCmd& pcmd = cmd_list->CmdBuffer[cmd_i];

            if (pcmd.UserCallback != nullptr) {
                if (pcmd.UserCallback == ImDrawCallback_ResetRenderState) {
                    imgui_pipeline_["out_color"] = render_target_;
                    continue;
                }
                pcmd.UserCallback(cmd_list, &pcmd);
                continue;
            }

            ImVec2 clip_min((pcmd.ClipRect.x - draw_data->DisplayPos.x) * draw_data->FramebufferScale.x,
                            (pcmd.ClipRect.y - draw_data->DisplayPos.y) * draw_data->FramebufferScale.y);
            ImVec2 clip_max((pcmd.ClipRect.z - draw_data->DisplayPos.x) * draw_data->FramebufferScale.x,
                            (pcmd.ClipRect.w - draw_data->DisplayPos.y) * draw_data->FramebufferScale.y);

            clip_min.x = std::max(clip_min.x, 0.0f);
            clip_min.y = std::max(clip_min.y, 0.0f);
            clip_max.x = std::min(clip_max.x, static_cast<float>(fb_width));
            clip_max.y = std::min(clip_max.y, static_cast<float>(fb_height));

            if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y) {
                continue;
            }

            uint32_t texture_index = texture_id_to_descriptor(pcmd.GetTexID());
            if (texture_index == 0 && font_atlas_image_) {
                texture_index = font_atlas_image_.storeDescriptor();
            }

            imgui_pipeline_["pushConsts.scale"] = scale;
            imgui_pipeline_["pushConsts.translate"] = translate;
            imgui_pipeline_["pushConsts.clip_rect"] = ktm::fvec4(clip_min.x, clip_min.y, clip_max.x, clip_max.y);
            imgui_pipeline_["pushConsts.texture_index"] = texture_index;

            const int32_t scissor_x = static_cast<int32_t>(std::floor(clip_min.x));
            const int32_t scissor_y = static_cast<int32_t>(std::floor(clip_min.y));
            const int32_t scissor_w = static_cast<int32_t>(std::ceil(clip_max.x)) - scissor_x;
            const int32_t scissor_h = static_cast<int32_t>(std::ceil(clip_max.y)) - scissor_y;
            if (scissor_w <= 0 || scissor_h <= 0) {
                continue;
            }

            DrawIndexedParams draw_params;
            draw_params.indexCount = static_cast<uint32_t>(pcmd.ElemCount);
            draw_params.firstIndex = static_cast<uint32_t>(pcmd.IdxOffset);
            draw_params.vertexOffset = static_cast<int32_t>(pcmd.VtxOffset);
            draw_params.indexType = sizeof(ImDrawIdx) == sizeof(uint16_t) ? IndexType::UInt16 : IndexType::UInt32;
            draw_params.enableScissor = true;
            draw_params.scissor = ScissorRect{
                scissor_x,
                scissor_y,
                static_cast<uint32_t>(scissor_w),
                static_cast<uint32_t>(scissor_h)};

            imgui_pipeline_.record(index_buffer, vertex_buffer, draw_params);
            ++recorded_draw_cmds;
        }
    }

    return true;
}

void VulkanBackend::submit_frame(uint32_t fb_width,
                                 uint32_t fb_height,
                                 int cmd_lists_count,
                                 int total_draw_cmds,
                                 int recorded_draw_cmds) {
    executor_ << imgui_pipeline_(static_cast<uint16_t>(fb_width), static_cast<uint16_t>(fb_height))
              << executor_.commit();

    CFW_LOG_DEBUG(
        "VulkanBackend: frame stats cmd_lists={}, total_draw_cmds={}, recorded_draw_cmds={}, fb={}x{}",
        cmd_lists_count, total_draw_cmds, recorded_draw_cmds, fb_width, fb_height);

    frame_ready_ = true;
}

void VulkanBackend::present_frame() {
    if (!initialized_ || !frame_ready_ || !render_target_) {
        return;
    }

    displayer_.wait(executor_) << render_target_;
    executor_.cleanupDeferredResources();

    frame_ready_ = false;
}

void VulkanBackend::rebuild(int width, int height) {
    if (!initialized_) {
        return;
    }

    if (width <= 0 || height <= 0) {
        return;
    }

    if (!ensure_render_target(static_cast<uint32_t>(width), static_cast<uint32_t>(height))) {
        rebuild_needed_ = true;
        return;
    }

    rebuild_needed_ = false;
}

bool VulkanBackend::ensure_render_target(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) {
        return false;
    }

    if (render_target_ && render_target_width_ == width && render_target_height_ == height) {
        return true;
    }

    HardwareImage new_target(width, height, ImageFormat::RGBA8_SRGB, ImageUsage::SampledImage);
    if (!new_target) {
        CFW_LOG_ERROR("VulkanBackend: create render target failed ({}x{})", width, height);
        return false;
    }

    render_target_ = std::move(new_target);
    render_target_width_ = width;
    render_target_height_ = height;
    clear_pixels_.assign(static_cast<size_t>(width) * static_cast<size_t>(height) * 4u, 0u);
    frame_ready_ = false;

    return true;
}

bool VulkanBackend::ensure_imgui_pipeline() {
    if (imgui_pipeline_ready_) {
        return true;
    }

    try {
        imgui_pipeline_ = RasterizerPipeline(
            std::string(k_imgui_vertex_shader),
            std::string(k_imgui_fragment_shader));
        imgui_pipeline_ready_ = (imgui_pipeline_.getRasterizerPipelineID() != 0);

        if (imgui_pipeline_ready_) {
            CFW_LOG_INFO("VulkanBackend: imgui pipeline created, pipeline_id={}",
                         imgui_pipeline_.getRasterizerPipelineID());
        } else {
            CFW_LOG_ERROR("VulkanBackend: imgui pipeline creation returned invalid pipeline id");
        }
    } catch (const std::exception& e) {
        CFW_LOG_ERROR("VulkanBackend: create imgui rasterizer pipeline failed: {}", e.what());
        imgui_pipeline_ready_ = false;
    }

    return imgui_pipeline_ready_;
}

bool VulkanBackend::ensure_font_texture() {
    if (font_ready_) {
        return true;
    }

    if (ImGui::GetCurrentContext() == nullptr) {
        return false;
    }

    ImGuiIO& io = ImGui::GetIO();
    if (io.Fonts == nullptr) {
        return false;
    }

    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    if (pixels == nullptr || width <= 0 || height <= 0) {
        CFW_LOG_ERROR("VulkanBackend: font atlas data invalid");
        return false;
    }

    font_atlas_image_ = HardwareImage(
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        ImageFormat::RGBA8_SRGB,
        ImageUsage::SampledImage);

    if (!font_atlas_image_) {
        CFW_LOG_ERROR("VulkanBackend: create font atlas image failed");
        return false;
    }

    executor_ << font_atlas_image_.copyFrom(pixels) << executor_.commit();

    const uint32_t descriptor = font_atlas_image_.storeDescriptor();
    if (descriptor == 0) {
        CFW_LOG_ERROR("VulkanBackend: font atlas descriptor is 0 (invalid)");
        return false;
    }

    io.Fonts->SetTexID(static_cast<ImTextureID>(descriptor));

    font_ready_ = true;
    CFW_LOG_INFO("VulkanBackend: font atlas uploaded ({}x{}), descriptor={}", width, height, descriptor);
    return true;
}

}  // namespace Corona::Systems