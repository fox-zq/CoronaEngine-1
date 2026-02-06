#pragma once

#include <SDL3/SDL.h>
#include <imgui.h>

#include "ui_layout_manager.h"
#include "cef/browser_renderer.h"
#include "cef/browser_input_handler.h"
#include "sdl/sdl_event_handler.h"

namespace Corona::Systems {
class VulkanBackend;
}

namespace Corona::Systems::UI {

struct UiFrameContext {
    SDL_Window* window = nullptr;
    ImGuiIO* io = nullptr;
    VulkanBackend* vulkan_backend = nullptr;
    int* active_tab_id = nullptr;
    bool* running = nullptr;
    bool* window_size_changed = nullptr;
};

class UiFrameRunner {
   public:
    UiFrameRunner() = default;

    void run_frame(UiFrameContext& context);

   private:
    int url_input_active_tab_ = -1;

    UILayoutManager layout_manager_{};
    BrowserRenderer browser_renderer_{};
    BrowserInputHandler input_handler_{};
    SDLEventHandler event_handler_{};
};

}  // namespace Corona::Systems::UI
