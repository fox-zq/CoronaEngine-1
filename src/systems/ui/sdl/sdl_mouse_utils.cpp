#include "sdl_mouse_utils.h"

#include <cmath>

namespace Corona::Systems::UI::MouseUtils {

int MouseStateManager::handle_mouse_click(const ImVec2& current_pos, Uint32 current_time) {
    float dx = current_pos.x - last_click_pos_.x;
    float dy = current_pos.y - last_click_pos_.y;
    float distance = std::sqrtf(dx * dx + dy * dy);

    if ((current_time - last_click_time_) < kDoubleClickTime &&
        distance < kDoubleClickDist) {
        click_count_++;
    } else {
        click_count_ = 1;
    }

    // 限制最大点击次数为3（单击、双击、三击）
    if (click_count_ > 3) {
        click_count_ = 1;
    }

    last_click_time_ = current_time;
    last_click_pos_ = current_pos;

    return click_count_;
}

void MouseStateManager::reset() {
    last_click_time_ = 0;
    last_click_pos_ = ImVec2(0, 0);
    click_count_ = 0;
    is_left_down_ = false;
    is_dragging_ = false;
}

CefBrowserHost::MouseButtonType convert_mouse_button(Uint8 sdl_button) {
    switch (sdl_button) {
        case SDL_BUTTON_LEFT:
            return MBT_LEFT;
        case SDL_BUTTON_MIDDLE:
            return MBT_MIDDLE;
        case SDL_BUTTON_RIGHT:
            return MBT_RIGHT;
        default:
            return MBT_LEFT;
    }
}

CefMouseEvent create_mouse_event(const ImVec2& mouse_pos, const ImVec2& item_pos,
                                 uint32_t modifiers) {
    CefMouseEvent mouse_event;
    mouse_event.x = static_cast<int>(mouse_pos.x - item_pos.x);
    mouse_event.y = static_cast<int>(mouse_pos.y - item_pos.y);
    mouse_event.modifiers = modifiers;
    return mouse_event;
}

uint32_t get_modifiers(bool is_left_down) {
    ImGuiIO& io = ImGui::GetIO();
    uint32_t modifiers = 0;

    if (is_left_down) {
        modifiers |= EVENTFLAG_LEFT_MOUSE_BUTTON;
    }
    if (io.KeyShift) {
        modifiers |= EVENTFLAG_SHIFT_DOWN;
    }
    if (io.KeyCtrl) {
        modifiers |= EVENTFLAG_CONTROL_DOWN;
    }
    if (io.KeyAlt) {
        modifiers |= EVENTFLAG_ALT_DOWN;
    }

    return modifiers;
}

void send_mouse_click(CefRefPtr<CefBrowser> browser, const ImVec2& mouse_pos,
                      const ImVec2& item_pos, CefBrowserHost::MouseButtonType button,
                      bool mouse_up, int click_count) {
    if (!browser) return;

    uint32_t modifiers = get_modifiers(!mouse_up);
    CefMouseEvent mouse_event = create_mouse_event(mouse_pos, item_pos, modifiers);

    browser->GetHost()->SendMouseClickEvent(mouse_event, button, mouse_up, click_count);
}

void send_mouse_move(CefRefPtr<CefBrowser> browser, const ImVec2& mouse_pos,
                     const ImVec2& item_pos, bool mouse_leave) {
    if (!browser) return;

    uint32_t modifiers = get_modifiers();
    CefMouseEvent mouse_event = create_mouse_event(mouse_pos, item_pos, modifiers);

    browser->GetHost()->SendMouseMoveEvent(mouse_event, mouse_leave);
}

void send_mouse_wheel(CefRefPtr<CefBrowser> browser, const ImVec2& mouse_pos,
                      const ImVec2& item_pos, float wheel_delta) {
    if (!browser) return;

    uint32_t modifiers = get_modifiers();
    CefMouseEvent mouse_event = create_mouse_event(mouse_pos, item_pos, modifiers);

    browser->GetHost()->SendMouseWheelEvent(
        mouse_event, 0, static_cast<int>(wheel_delta * 100));
}

}  // namespace Corona::Systems::UI::MouseUtils
