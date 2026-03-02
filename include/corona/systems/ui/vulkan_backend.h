#pragma once

#include <CabbageHardware.h>
#include <SDL3/SDL.h>
#include <imgui.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace Corona::Systems {
class VulkanBackend {
   public:
    explicit VulkanBackend(SDL_Window* window);
    ~VulkanBackend() = default;

    bool initialize();
    void shutdown();

    void new_frame();
    void render_frame(ImDrawData* draw_data);
    void present_frame();

    [[nodiscard]] bool is_rebuild_needed() const { return rebuild_needed_; }
    void request_rebuild() { rebuild_needed_ = true; }
    void rebuild(int width, int height);

   private:
    struct ImGuiGpuVertex {
        float pos[2]{};
        float uv[2]{};
        float color[4]{};
    };

    bool ensure_render_target(uint32_t width, uint32_t height);
    bool ensure_imgui_pipeline();
    bool ensure_font_texture();

   private:
    SDL_Window* window_ = nullptr;
    bool initialized_ = false;
    bool rebuild_needed_ = false;

    bool frame_ready_ = false;
    bool imgui_pipeline_ready_ = false;
    bool font_ready_ = false;

    uint32_t render_target_width_ = 0;
    uint32_t render_target_height_ = 0;

    HardwareImage render_target_;
    HardwareImage font_atlas_image_;
    RasterizerPipeline imgui_pipeline_;

    std::vector<uint8_t> clear_pixels_;

    HardwareDisplayer displayer_;
    HardwareExecutor executor_;
};

}  // namespace Corona::Systems