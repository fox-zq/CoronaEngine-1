#pragma once

#include <SDL3/SDL.h>
#include <imgui.h>

namespace Corona::Systems {

class IUiRenderBackend {
   public:
    virtual ~IUiRenderBackend() = default;

    virtual bool initialize(SDL_Window* window) = 0;
    virtual void shutdown() = 0;

    virtual void new_frame() = 0;
    virtual void render_frame(ImDrawData* draw_data) = 0;
    virtual void present_frame() = 0;

    virtual bool is_rebuild_needed() const = 0;
    virtual void request_rebuild() = 0;
    virtual void rebuild(int width, int height) = 0;
};

}  // namespace Corona::Systems