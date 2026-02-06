#include <corona/kernel/core/i_logger.h>
#include <corona/systems/ui/imgui_system.h>
#include <imgui_internal.h>

#include "cef/browser_manager.h"
#include "cef/browser_renderer.h"
#include "cef/browser_input_handler.h"
#include "cef/cef_client.h"
#include "cef/cef_runtime.h"
#include "imgui/imgui_runtime.h"
#include "imgui/ui_layout_manager.h"
#include "sdl/sdl_event_handler.h"

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

void ImguiSystem::thread_loop() {
    if (!UI::initialize_sdl_imgui(window_, io_, vulkan_backend_)) {
        UI::shutdown_cef();
        running_ = false;
        return;
    }

    using Corona::Kernel::SystemState;
    while (running_ && get_state() == SystemState::running) {
        update();
    }

    UI::shutdown_sdl_imgui(window_, io_, vulkan_backend_);
    UI::shutdown_cef();
}

void ImguiSystem::update() {
    if (!running_) {
        return;
    }

    static int url_input_active_tab = -1;
    static UI::UILayoutManager layout_manager;
    static UI::BrowserRenderer browser_renderer;
    static UI::BrowserInputHandler input_handler;
    static UI::SDLEventHandler event_handler;

    auto result = event_handler.process_events(
        window_,
        url_input_active_tab,
        [&](const SDL_Event& e) { input_handler.process_sdl_key_event(e); },
        [&](const SDL_Event& e) { input_handler.process_sdl_text_event(e); },
        [&](const SDL_Event& e) { input_handler.process_sdl_ime_event(e); });

    if (result.should_quit) {
        running_ = false;
    }

    if (result.window_resized && window_) {
        window_size_changed_ = true;
        vulkan_backend_->set_swap_chain_rebuild(true);
    }

    if (vulkan_backend_ && vulkan_backend_->is_swap_chain_rebuild()) {
        int width, height;
        SDL_GetWindowSize(window_, &width, &height);
        vulkan_backend_->rebuild_swap_chain(width, height);
    }

    vulkan_backend_->new_frame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    ImGuiID dock_space_id = layout_manager.setup_dockspace();

    std::vector<int> tabs_to_close = browser_renderer.render_browser_tabs(
        dock_space_id, active_tab_id_, url_input_active_tab, io_);

    layout_manager.end_dockspace();

    if (active_tab_id_ != -1 && url_input_active_tab == -1) {
        auto* tab = UI::BrowserManager::instance().get_tab(active_tab_id_);
        if (tab && tab->client && tab->client->GetBrowser()) {
            input_handler.send_key_events_to_browser(tab->client->GetBrowser());
        } else {
            input_handler.clear_pending_events();
        }
    } else {
        input_handler.clear_pending_events();
    }

    for (auto tab_id : tabs_to_close) {
        UI::BrowserManager::instance().remove_tab(tab_id);
        if (tab_id == active_tab_id_) {
            active_tab_id_ = -1;
        }
        if (tab_id == url_input_active_tab) {
            url_input_active_tab = -1;
        }
    }

    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
    if (!is_minimized) {
        vulkan_backend_->render_frame(draw_data);
        vulkan_backend_->present_frame();
    }

    if (io_ && (io_->ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

void ImguiSystem::shutdown() {
    CFW_LOG_NOTICE("DisplaySystem: Shutting down...");
    running_ = false;

    auto& tabs = UI::BrowserManager::instance().get_tabs();
    std::vector<int> ids;
    for (const auto& [id, tab] : tabs) {
        ids.push_back(id);
    }

    for (int id : ids) {
        UI::BrowserManager::instance().remove_tab(id);
    }
}

}  // namespace Corona::Systems
