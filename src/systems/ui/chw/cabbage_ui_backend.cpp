#include <corona/kernel/core/i_logger.h>
#include <corona/systems/ui/cabbage_ui_backend.h>

namespace Corona::Systems {

bool CabbageUiBackend::initialize(SDL_Window* window) {
    if (window == nullptr) {
        CFW_LOG_ERROR("CabbageUiBackend: initialize failed, window is null");
        return false;
    }

    window_ = window;
    displayer_ = std::make_unique<HardwareDisplayer>(window_);
    initialized_ = true;
    rebuild_needed_ = false;

    CFW_LOG_INFO("CabbageUiBackend: initialized");
    return true;
}

void CabbageUiBackend::shutdown() {
    displayer_.reset();
    initialized_ = false;
    rebuild_needed_ = false;
    window_ = nullptr;

    CFW_LOG_INFO("CabbageUiBackend: shutdown");
}

void CabbageUiBackend::new_frame() {
    if (!initialized_) {
        return;
    }
    // 提交 1/2 仅做骨架，真正 ImGui DrawData 渲染在后续提交实现。
}

void CabbageUiBackend::render_frame(ImDrawData* draw_data) {
    if (!initialized_ || draw_data == nullptr) {
        return;
    }
    // 提交 1/2 仅做骨架，后续接入 ImGui 自定义 renderer。
}

void CabbageUiBackend::present_frame() {
    if (!initialized_) {
        return;
    }
    // 提交 1/2 仅做骨架。
}

void CabbageUiBackend::rebuild(int width, int height) {
    if (!initialized_) {
        return;
    }
    if (width <= 0 || height <= 0) {
        return;
    }

    rebuild_needed_ = false;
    // 提交 1/2 仅做骨架。后续如果 Cabbage 需要重建目标图像/显示面，在此实现。
}

}  // namespace Corona::Systems