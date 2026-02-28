#include "browser_ui.h"

#include <SDL3/SDL.h>

#include "browser_manager.h"
#include "cef_client.h"
#include "sdl/sdl_utils.h"

namespace Corona::Systems::UI {

// ============================================================================
// BrowserInputHandler 实现
// ============================================================================

void BrowserInputHandler::clear_pending_events() {
    pending_key_events_.clear();
}

void BrowserInputHandler::process_sdl_key_event(const SDL_Event& event) {
    bool pressed = (event.type == SDL_EVENT_KEY_DOWN);
    int key_code = static_cast<int>(event.key.key);
    int scan_code = static_cast<int>(event.key.scancode);
    int modifiers = 0;

    Uint32 sdl_mod = event.key.mod;
    if (sdl_mod & SDL_KMOD_CTRL) modifiers |= EVENTFLAG_CONTROL_DOWN;
    if (sdl_mod & SDL_KMOD_SHIFT) modifiers |= EVENTFLAG_SHIFT_DOWN;
    if (sdl_mod & SDL_KMOD_ALT) modifiers |= EVENTFLAG_ALT_DOWN;
    if (sdl_mod & SDL_KMOD_GUI) modifiers |= EVENTFLAG_COMMAND_DOWN;
    if (sdl_mod & SDL_KMOD_CAPS) modifiers |= EVENTFLAG_CAPS_LOCK_ON;
    if (sdl_mod & SDL_KMOD_NUM) modifiers |= EVENTFLAG_NUM_LOCK_ON;

    bool is_common_edit_shortcut = false;
    if (modifiers & EVENTFLAG_CONTROL_DOWN) {
        switch (key_code) {
            case SDLK_A:
            case SDLK_C:
            case SDLK_V:
            case SDLK_Z:
            case SDLK_Y:
                is_common_edit_shortcut = true;
                break;
            default:
                break;
        }
    }

    bool is_modifier_combo = (modifiers & (EVENTFLAG_CONTROL_DOWN | EVENTFLAG_ALT_DOWN)) &&
                             ((key_code >= 'a' && key_code <= 'z') ||
                              (key_code >= 'A' && key_code <= 'Z') ||
                              (key_code >= '0' && key_code <= '9'));

    PendingKeyEvent key_event(PendingKeyEvent::kKeyEvent);
    key_event.key_code = key_code;
    key_event.scan_code = scan_code;
    key_event.modifiers = modifiers;
    key_event.pressed = pressed;
    key_event.is_modifier_combo = is_modifier_combo || is_common_edit_shortcut;

    pending_key_events_.push_back(key_event);
}

void BrowserInputHandler::process_sdl_text_event(const SDL_Event& event) {
    if (event.text.text && event.text.text[0]) {
        PendingKeyEvent text_event(PendingKeyEvent::kTextEvent);
        text_event.text = event.text.text;
        pending_key_events_.push_back(text_event);
    }
}

void BrowserInputHandler::process_sdl_ime_event(const SDL_Event& event) {
    if (event.edit.text && event.edit.text[0]) {
        PendingKeyEvent ime_event(PendingKeyEvent::kImeComposition);
        ime_event.text = event.edit.text;
        ime_event.ime_start = event.edit.start;
        ime_event.ime_length = event.edit.length;
        pending_key_events_.push_back(ime_event);
    }
}

void BrowserInputHandler::send_key_events_to_browser(const CefRefPtr<CefBrowser>& browser) {
    if (!browser) return;

    for (const auto& pending_event : pending_key_events_) {
        if (pending_event.type == PendingKeyEvent::kKeyEvent) {
            CefKeyEvent cef_key_event;
            cef_key_event.type = pending_event.pressed ? KEYEVENT_RAWKEYDOWN : KEYEVENT_KEYUP;
            cef_key_event.windows_key_code = KeyUtils::convert_sdl_key_code_to_windows(pending_event.key_code);
            cef_key_event.native_key_code = pending_event.scan_code;
            cef_key_event.modifiers = pending_event.modifiers;
            cef_key_event.character = pending_event.key_code;
            cef_key_event.unmodified_character = pending_event.key_code;

            bool is_common_edit_shortcut = false;
            if (pending_event.modifiers & EVENTFLAG_CONTROL_DOWN) {
                switch (pending_event.key_code) {
                    case SDLK_A:
                    case SDLK_C:
                    case SDLK_V:
                    case SDLK_Z:
                    case SDLK_Y:
                        is_common_edit_shortcut = true;
                        break;
                    default:
                        break;
                }
            }

            browser->GetHost()->SendKeyEvent(cef_key_event);

            if (pending_event.pressed &&
                (pending_event.key_code == SDLK_RETURN || pending_event.key_code == SDLK_KP_ENTER)) {
                CefKeyEvent char_event = cef_key_event;
                char_event.type = KEYEVENT_CHAR;
                char_event.character = 0x0D;
                char_event.unmodified_character = 0x0D;
                browser->GetHost()->SendKeyEvent(char_event);
            }

            if (pending_event.pressed && pending_event.is_modifier_combo) {
                if (is_common_edit_shortcut) {
                    cef_key_event.type = KEYEVENT_CHAR;
                    browser->GetHost()->SendKeyEvent(cef_key_event);
                } else {
                    switch (pending_event.key_code) {
                        case SDLK_RETURN:
                        case SDLK_KP_ENTER:
                        case SDLK_TAB:
                        case SDLK_BACKSPACE:
                        case SDLK_DELETE:
                        case SDLK_ESCAPE:
                            cef_key_event.type = KEYEVENT_CHAR;
                            browser->GetHost()->SendKeyEvent(cef_key_event);
                            break;
                        default:
                            break;
                    }
                }
            }
        } else if (pending_event.type == PendingKeyEvent::kTextEvent) {
            const std::string& text = pending_event.text;
            if (!text.empty()) {
                bool has_control_chars = false;
                for (char c : text) {
                    if (c == '\b' || c == '\t' || c == '\n' || c == '\r') {
                        has_control_chars = true;
                        break;
                    }
                }

                if (!has_control_chars) {
                    bool is_ascii = true;
                    for (char c : text) {
                        if (static_cast<unsigned char>(c) >= 128) {
                            is_ascii = false;
                            break;
                        }
                    }

                    if (is_ascii) {
                        for (char c : text) {
                            if (c >= 32 && c < 127) {
                                CefKeyEvent cef_text_event;
                                cef_text_event.type = KEYEVENT_CHAR;
                                cef_text_event.modifiers = 0;
                                cef_text_event.windows_key_code = static_cast<uint16_t>(c);
                                cef_text_event.native_key_code = static_cast<uint16_t>(c);
                                cef_text_event.character = static_cast<uint16_t>(c);
                                cef_text_event.unmodified_character = static_cast<uint16_t>(c);
                                browser->GetHost()->SendKeyEvent(cef_text_event);
                            }
                        }
                    } else {
                        if (char* utf16_text = SDL_iconv_string("UTF-16LE", "UTF-8", text.c_str(), text.length() + 1)) {
                            auto* utf16_chars = reinterpret_cast<uint16_t*>(utf16_text);
                            size_t utf16_len = 0;
                            while (utf16_chars[utf16_len] != 0) {
                                utf16_len++;
                            }
                            for (size_t i = 0; i < utf16_len; i++) {
                                CefKeyEvent cef_text_event;
                                cef_text_event.type = KEYEVENT_CHAR;
                                cef_text_event.modifiers = 0;
                                cef_text_event.windows_key_code = utf16_chars[i];
                                cef_text_event.native_key_code = utf16_chars[i];
                                cef_text_event.character = utf16_chars[i];
                                cef_text_event.unmodified_character = utf16_chars[i];
                                browser->GetHost()->SendKeyEvent(cef_text_event);
                            }
                            SDL_free(utf16_text);
                        }
                    }
                }
            }
        }
    }

    clear_pending_events();
}

// ============================================================================
// BrowserRenderer 实现
// ============================================================================

static MouseUtils::MouseStateManager mouse_state;

void BrowserRenderer::setup_window_transform(BrowserTab* tab,
                                             ImGuiID dock_space_id,
                                             bool is_main_tab) {
    if (is_main_tab) {
        ImGui::SetNextWindowDockID(dock_space_id, ImGuiCond_Always);
    } else {
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

void BrowserRenderer::handle_browser_mouse_events(const BrowserTab* tab,
                                                  int tab_id,
                                                  int& active_tab_id,
                                                  int& url_input_active_tab,
                                                  const ImGuiIO* io) {
    bool browser_hovered = ImGui::IsItemHovered();
    bool browser_active = (active_tab_id == tab_id);

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

    // 处理鼠标左键点击
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

    // 处理鼠标右键点击 - 关键修改
    if (browser_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        active_tab_id = tab_id;
        url_input_active_tab = -1;

        ImVec2 current_pos = ImGui::GetMousePos();
        ImVec2 item_pos = ImGui::GetItemRectMin();

        if (tab->client && tab->client->GetBrowser()) {
            tab->client->GetBrowser()->GetHost()->SetFocus(true);

            // 发送鼠标右键按下事件
            MouseUtils::send_mouse_click(
                tab->client->GetBrowser(), current_pos, item_pos,
                MBT_RIGHT, false, 1);
        }
    }

    // 处理鼠标右键释放
    if (browser_hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        if (tab->client && tab->client->GetBrowser()) {
            ImVec2 mouse_pos = ImGui::GetMousePos();
            ImVec2 item_pos = ImGui::GetItemRectMin();

            MouseUtils::send_mouse_click(
                tab->client->GetBrowser(), mouse_pos, item_pos,
                MBT_RIGHT, true, mouse_state.get_click_count());
        }
    }

    // 处理鼠标左键释放
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

    if (browser_hovered) {
        if (CefRefPtr<CefBrowser> browser = tab->client ? tab->client->GetBrowser() : nullptr) {
            ImVec2 mouse_pos = ImGui::GetMousePos();
            ImVec2 item_pos = ImGui::GetItemRectMin();

            MouseUtils::send_mouse_move(browser, mouse_pos, item_pos, false);

            if (mouse_state.is_mouse_down()) {
                ImVec2 drag_delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
                float drag_distance = drag_delta.x * drag_delta.x + drag_delta.y * drag_delta.y;

                if (drag_distance > 4.0f) {
                    mouse_state.set_dragging(true);
                }
            }

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

        ImGui::SameLine();
        if (ImGui::Button("Refresh")) {
            if (tab->client && tab->client->GetBrowser()) {
                tab->client->GetBrowser()->Reload();
            }
        }

        ImVec2 avail_size = ImGui::GetContentRegionAvail();
        int new_width = static_cast<int>(avail_size.x);
        int new_height = static_cast<int>(avail_size.y);

        if (new_width > 0 && new_height > 0 &&
            (new_width != tab->width || new_height != tab->height)) {
            BrowserManager::instance().resize_tab(tab_id, new_width, new_height);
        }

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