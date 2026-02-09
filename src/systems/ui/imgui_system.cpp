#include <corona/kernel/core/i_logger.h>
#include <corona/systems/ui/imgui_system.h>

#include "cef/browser_manager.h"
#include "cef/cef_runtime.h"
#include "imgui/imgui_runtime.h"
#include "imgui/ui_frame_runner.h"

namespace Corona::Systems {

bool ImguiSystem::initialize(Kernel::ISystemContext* ctx) {
    CFW_LOG_NOTICE("ImguiSystem: Initializing...");

    if (!UI::initialize_cef()) {
        CFW_LOG_ERROR("CEF initialization failed.");
        return false;
    }

    running_ = true;
    active_tab_id_ = -1;

    return true;
}

void ImguiSystem::on_thread_started() {
    CFW_LOG_NOTICE("ImguiSystem: Thread started.");

    if (!UI::initialize_sdl_imgui(window_, io_, vulkan_backend_)) {
        UI::shutdown_cef();
        running_ = false;
    }
}

void ImguiSystem::on_thread_stopped() {
    CFW_LOG_NOTICE("ImguiSystem: Thread stopped.");
    UI::shutdown_sdl_imgui(window_, io_, vulkan_backend_);
    UI::shutdown_cef();
}

void ImguiSystem::update() {
    if (!running_) {
        return;
    }

    static UI::UiFrameRunner frame_runner;
    UI::UiFrameContext context{
        window_,
        io_,
        vulkan_backend_.get(),
        &active_tab_id_,
        &running_,
        &window_size_changed_};

    frame_runner.run_frame(context);
}

void ImguiSystem::shutdown() {
    CFW_LOG_NOTICE("ImGuiSystem: Shutting down...");
    running_ = false;

    UI::BrowserManager::instance().close_all_tabs();
}

}  // namespace Corona::Systems
