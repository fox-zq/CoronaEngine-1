#include "ui_frame_runner.h"

#include <imgui_impl_sdl3.h>

#include "cef/browser_manager.h"
#include "cef/browser_renderer.h"
#include "cef/browser_input_handler.h"
#include "cef/cef_client.h"
#include "imgui/ui_layout_manager.h"
#include "sdl/sdl_event_handler.h"
#include <corona/systems/ui/vulkan_backend.h>

namespace Corona::Systems::UI {

void UiFrameRunner::run_frame(UiFrameContext& context) {
    if (!context.running || !context.active_tab_id || !context.vulkan_backend) {
        return;
    }

    auto result = event_handler_.process_events(
        context.window,
        url_input_active_tab_,
        [&](const SDL_Event& e) { input_handler_.process_sdl_key_event(e); },
        [&](const SDL_Event& e) { input_handler_.process_sdl_text_event(e); },
        [&](const SDL_Event& e) { input_handler_.process_sdl_ime_event(e); });

    if (result.should_quit) {
        *context.running = false;
    }

    if (result.window_resized && context.window && context.window_size_changed) {
        *context.window_size_changed = true;
        context.vulkan_backend->set_swap_chain_rebuild(true);
    }

    if (context.vulkan_backend->is_swap_chain_rebuild()) {
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(context.window, &width, &height);
        context.vulkan_backend->rebuild_swap_chain(width, height);
    }

    context.vulkan_backend->new_frame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    ImGuiID dock_space_id = layout_manager_.setup_dockspace();

    std::vector<int> tabs_to_close = browser_renderer_.render_browser_tabs(
        dock_space_id, *context.active_tab_id, url_input_active_tab_, context.io);

    layout_manager_.end_dockspace();

    if (*context.active_tab_id != -1 && url_input_active_tab_ == -1) {
        auto* tab = BrowserManager::instance().get_tab(*context.active_tab_id);
        if (tab && tab->client && tab->client->GetBrowser()) {
            input_handler_.send_key_events_to_browser(tab->client->GetBrowser());
        } else {
            input_handler_.clear_pending_events();
        }
    } else {
        input_handler_.clear_pending_events();
    }

    for (auto tab_id : tabs_to_close) {
        BrowserManager::instance().remove_tab(tab_id);
        if (tab_id == *context.active_tab_id) {
            *context.active_tab_id = -1;
        }
        if (tab_id == url_input_active_tab_) {
            url_input_active_tab_ = -1;
        }
    }

    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
    if (!is_minimized) {
        context.vulkan_backend->render_frame(draw_data);
        context.vulkan_backend->present_frame();
    }

    if (context.io && (context.io->ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

}  // namespace Corona::Systems::UI
