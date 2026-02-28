#pragma once

#include <CabbageHardware.h>
#include <SDL3/SDL.h>
#include <corona/systems/ui/ui_render_backend.h>

#include <memory>

namespace Corona::Systems {

class CabbageUiBackend final : public IUiRenderBackend {
   public:
    CabbageUiBackend() = default;
    ~CabbageUiBackend() override = default;

    bool initialize(SDL_Window* window) override;
    void shutdown() override;

    void new_frame() override;
    void render_frame(ImDrawData* draw_data) override;
    void present_frame() override;

    [[nodiscard]] bool is_rebuild_needed() const override { return rebuild_needed_; }
    void request_rebuild() override { rebuild_needed_ = true; }
    void rebuild(int width, int height) override;

   private:
    SDL_Window* window_ = nullptr;
    bool initialized_ = false;
    bool rebuild_needed_ = false;

    std::unique_ptr<HardwareDisplayer> displayer_;
    HardwareExecutor executor_;
};

}  // namespace Corona::Systems