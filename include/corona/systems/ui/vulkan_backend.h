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

    [[nodiscard]] bool is_rebuild_needed() const noexcept { return rebuild_needed_; }
    void request_rebuild() noexcept { rebuild_needed_ = true; }
    void rebuild(int width, int height);

   private:
    bool prepare_frame(ImDrawData* draw_data, uint32_t& fb_width, uint32_t& fb_height);
    bool record_draw_lists(ImDrawData* draw_data,
                           uint32_t fb_width,
                           uint32_t fb_height,
                           int& total_draw_cmds,
                           int& recorded_draw_cmds);
    void submit_frame(uint32_t fb_width,
                      uint32_t fb_height,
                      int cmd_lists_count,
                      int total_draw_cmds,
                      int recorded_draw_cmds);

    bool ensure_render_target(uint32_t width, uint32_t height);
    bool ensure_imgui_pipeline();
    bool ensure_font_texture();

   private:
    bool initialized_ = false;
    bool rebuild_needed_ = false;
    bool frame_ready_ = false;
    bool imgui_pipeline_ready_ = false;
    bool font_ready_ = false;

    uint32_t render_target_width_ = 0;
    uint32_t render_target_height_ = 0;

    SDL_Window* window_ = nullptr;
    void* surface_ = nullptr;

    // Double-buffered render targets: write_index_ is the buffer being rendered to,
    // the other buffer holds the last completed frame for DisplaySystem to consume.
    static constexpr int BUFFER_COUNT = 2;
    HardwareImage render_target_[BUFFER_COUNT];
    HardwareExecutor executor_[BUFFER_COUNT];
    int write_index_ = 0;

    HardwareImage font_atlas_image_;
    RasterizerPipeline imgui_pipeline_;

    std::vector<uint8_t> clear_pixels_;

    uint64_t frame_index_ = 0;

    HardwareBuffer vertex_buffer_;
    HardwareBuffer index_buffer_;
    size_t vertex_buffer_capacity_ = 0;
    size_t index_buffer_capacity_ = 0;
};

}  // namespace Corona::Systems