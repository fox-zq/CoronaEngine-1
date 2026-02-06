#include "browser_renderer.h"

#include <SDL3/SDL.h>

#include "../sdl/sdl_mouse_utils.h"
#include "browser_manager.h"
#include "browser_types.h"
#include "cef_client.h"  // Added to get full definition of OffscreenCefClient

namespace Corona::Systems::UI {

// 静态鼠标状态管理器
static MouseUtils::MouseStateManager mouse_state;

void BrowserRenderer::setup_window_transform(BrowserTab* tab,
                                             ImGuiID dock_space_id,
                                             bool is_main_tab) {
    if (is_main_tab) {
        ImGui::SetNextWindowDockID(dock_space_id, ImGuiCond_Always);
    } else {
        // 计算浮动窗口位置
        ImGuiViewport* browser_viewport = ImGui::GetMainViewport();
        ImVec2 work_pos = browser_viewport->WorkPos;
        ImVec2 work_size = browser_viewport->WorkSize;

        auto target_w = static_cast<float>(tab->dock_width);
        auto target_h = static_cast<float>(tab->dock_height);

        ImVec2 final_pos = work_pos;

        if (tab->docking_pos == "left_top") {
            final_pos.x = work_pos.x;
            final_pos.y = work_pos.y + 50.f;
        } else if (tab->docking_pos == "left_bottom") {
            final_pos.x = work_pos.x;
            final_pos.y = work_pos.y + work_size.y - target_h;
        } else if (tab->docking_pos == "right_top") {
            final_pos.x = work_pos.x + work_size.x - target_w;
            final_pos.y = work_pos.y + 50.f;
        } else if (tab->docking_pos == "right_bottom") {
            final_pos.x = work_pos.x + work_size.x - target_w;
            final_pos.y = work_pos.y + work_size.y - target_h;
        } else if (tab->docking_pos == "bottom_left") {
            final_pos.x = work_pos.x + 300.f;
            final_pos.y = work_pos.y + work_size.y - target_h;
        } else if (tab->docking_pos == "bottom_right") {
            final_pos.x = work_pos.x + work_size.x - target_w - 300.f;
            final_pos.y = work_pos.y + work_size.y - target_h;
        } else {
            final_pos.x = work_pos.x + 300.f;
            final_pos.y = work_pos.y + 50.f;
        }

        ImGui::SetNextWindowPos(final_pos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(target_w, target_h), ImGuiCond_FirstUseEver);

        tab->dock_initialized = true;
    }
}

void BrowserRenderer::handle_browser_mouse_events(BrowserTab* tab,
                                                  int tab_id,
                                                  int& active_tab_id,
                                                  int& url_input_active_tab,
                                                  ImGuiIO* io) {
    bool browser_hovered = ImGui::IsItemHovered();
    bool browser_active = (active_tab_id == tab_id);

    // 处理鼠标离开浏览器区域的情况
    if (!browser_hovered && mouse_state.is_mouse_down() && browser_active) {
        if (tab->client && tab->client->GetBrowser()) {
            ImVec2 mouse_pos = ImGui::GetMousePos();
            ImVec2 item_pos = ImGui::GetItemRectMin();

            CefMouseEvent mouse_event = MouseUtils::create_mouse_event(mouse_pos, item_pos);

            if (mouse_event.x >= 0 && mouse_event.x < tab->width &&
                mouse_event.y >= 0 && mouse_event.y < tab->height) {
                tab->client->GetBrowser()->GetHost()->SendMouseClickEvent(
                    mouse_event, MBT_LEFT, true, mouse_state.get_click_count());
            }
        }
        mouse_state.set_mouse_down(false);
        mouse_state.set_dragging(false);
    }

    // 处理鼠标点击
    if (browser_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        active_tab_id = tab_id;
        url_input_active_tab = -1;

        ImVec2 current_pos = ImGui::GetMousePos();
        ImVec2 item_pos = ImGui::GetItemRectMin();
        Uint32 current_time = SDL_GetTicks();

        int click_count = mouse_state.handle_mouse_click(current_pos, current_time);
        mouse_state.set_mouse_down(true);

        if (tab->client && tab->client->GetBrowser()) {
            tab->client->GetBrowser()->GetHost()->SetFocus(true);

            MouseUtils::send_mouse_click(
                tab->client->GetBrowser(), current_pos, item_pos,
                MBT_LEFT, false, click_count);
        }
    }

    // 处理鼠标抬起
    if (browser_hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (mouse_state.is_mouse_down()) {
            if (tab->client && tab->client->GetBrowser()) {
                ImVec2 mouse_pos = ImGui::GetMousePos();
                ImVec2 item_pos = ImGui::GetItemRectMin();

                MouseUtils::send_mouse_click(
                    tab->client->GetBrowser(), mouse_pos, item_pos,
                    MBT_LEFT, true, mouse_state.get_click_count());
            }

            mouse_state.set_mouse_down(false);
            mouse_state.set_dragging(false);
        }
    }

    // 全局鼠标抬起事件处理
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && mouse_state.is_mouse_down() && browser_active) {
        if (tab->client && tab->client->GetBrowser()) {
            ImVec2 mouse_pos = ImGui::GetMousePos();
            ImVec2 item_pos = ImGui::GetItemRectMin();

            CefMouseEvent mouse_event = MouseUtils::create_mouse_event(mouse_pos, item_pos);

            if (mouse_event.x >= 0 && mouse_event.x < tab->width &&
                mouse_event.y >= 0 && mouse_event.y < tab->height) {
                tab->client->GetBrowser()->GetHost()->SendMouseClickEvent(
                    mouse_event, MBT_LEFT, true, mouse_state.get_click_count());
            }
        }
        mouse_state.set_mouse_down(false);
        mouse_state.set_dragging(false);
    }

    // 处理鼠标移动和滚轮
    if (browser_hovered) {
        if (CefRefPtr<CefBrowser> browser = tab->client ? tab->client->GetBrowser() : nullptr) {
            ImVec2 mouse_pos = ImGui::GetMousePos();
            ImVec2 item_pos = ImGui::GetItemRectMin();

            MouseUtils::send_mouse_move(browser, mouse_pos, item_pos, false);

            // 处理鼠标拖动
            if (mouse_state.is_mouse_down()) {
                ImVec2 drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
                float drag_distance = drag_delta.x * drag_delta.x + drag_delta.y * drag_delta.y;

                if (drag_distance > 4.0f) {
                    mouse_state.set_dragging(true);
                }
            }

            // 处理鼠标滚轮
            float wheel = io->MouseWheel;
            if (wheel != 0) {
                MouseUtils::send_mouse_wheel(browser, mouse_pos, item_pos, wheel);
            }
        }
    }
}

void BrowserRenderer::render_single_tab(int tab_id,
                                        ImGuiID dock_space_id,
                                        int& active_tab_id,
                                        int& url_input_active_tab,
                                        ImGuiIO* io) {
    auto* tab = BrowserManager::instance().get_tab(tab_id);
    if (!tab || !tab->open) {
        return;
    }

    BrowserManager::instance().update_texture(tab_id);

    std::string window_id = tab->name + "##" + std::to_string(tab_id);

    ImGuiWindowFlags browser_window_flags = ImGuiWindowFlags_NoTitleBar |
                                            ImGuiWindowFlags_NoScrollbar |
                                            ImGuiWindowFlags_NoNavInputs |
                                            ImGuiWindowFlags_NoNavFocus;

    bool is_main_tab = (tab->docking_pos == "main");

    if (is_main_tab) {
        browser_window_flags |= ImGuiWindowFlags_NoMove;
    }

    setup_window_transform(tab, dock_space_id, is_main_tab);

    if (ImGui::Begin(window_id.c_str(), &tab->open, browser_window_flags)) {
        ImGui::PushItemWidth(-200);
        ImGui::SameLine();
        ImGui::PopItemWidth();
        ImGui::SameLine();

        // 刷新按钮
        ImGui::SameLine();
        if (ImGui::Button("Refresh")) {
            if (tab->client && tab->client->GetBrowser()) {
                tab->client->GetBrowser()->Reload();
            }
        }

        // 浏览器内容区域
        ImVec2 avail_size = ImGui::GetContentRegionAvail();
        int new_width = static_cast<int>(avail_size.x);
        int new_height = static_cast<int>(avail_size.y);

        // 调整大小
        if (new_width > 0 && new_height > 0 &&
            (new_width != tab->width || new_height != tab->height)) {
            BrowserManager::instance().resize_tab(tab_id, new_width, new_height);
        }

        // 渲染浏览器纹理
        if (tab->texture_id != VK_NULL_HANDLE) {
            ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(tab->texture_id)), avail_size);
            handle_browser_mouse_events(tab, tab_id, active_tab_id, url_input_active_tab, io);
        }
    }
    ImGui::End();
}

std::vector<int> BrowserRenderer::render_browser_tabs(ImGuiID dock_space_id,
                                                      int& active_tab_id,
                                                      int& url_input_active_tab,
                                                      ImGuiIO* io) {
    std::vector<int> tabs_to_close;
    auto& tabs = BrowserManager::instance().get_tabs();

    for (auto& [tab_id, tab] : tabs) {
        if (!tab->open) {
            tabs_to_close.push_back(tab_id);
            continue;
        }

        render_single_tab(tab_id, dock_space_id, active_tab_id, url_input_active_tab, io);
    }

    return tabs_to_close;
}

}  // namespace Corona::Systems::UI
