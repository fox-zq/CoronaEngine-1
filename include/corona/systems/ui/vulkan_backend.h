#pragma once

#include <CabbageHardware.h>
#include <SDL3/SDL.h>
#include <imgui.h>

#include <memory>

namespace Corona::Systems 
{
    class VulkanBackend 
    {
    public:
        VulkanBackend(SDL_Window* window);
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
        SDL_Window* window_ = nullptr;
        bool initialized_ = false;
        bool rebuild_needed_ = false;

        HardwareDisplayer displayer_;
        HardwareExecutor executor_;
    };

}  // namespace Corona::Systems