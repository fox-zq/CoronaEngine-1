#pragma once

#include <SDL3/SDL_events.h>
#include <cef_browser.h>
#include <imgui.h>
#include <include/internal/cef_types.h>

namespace Corona::Systems::UI::MouseUtils {

// 鼠标状态管理器
class MouseStateManager {
   public:
    MouseStateManager() = default;

    // 处理鼠标点击事件
    int handle_mouse_click(const ImVec2& current_pos, Uint32 current_time);

    // 设置鼠标按下状态
    void set_mouse_down(bool down) { is_left_down_ = down; }
    bool is_mouse_down() const { return is_left_down_; }

    // 获取当前点击次数
    int get_click_count() const { return click_count_; }

    // 设置和获取拖拽状态
    void set_dragging(bool dragging) { is_dragging_ = dragging; }
    bool is_dragging() const { return is_dragging_; }

    // 重置状态
    void reset();

   private:
    Uint32 last_click_time_ = 0;
    ImVec2 last_click_pos_{0, 0};
    int click_count_ = 0;
    bool is_left_down_ = false;
    bool is_dragging_ = false;

    static constexpr Uint32 kDoubleClickTime = 500;
    static constexpr float kDoubleClickDist = 5.0f;
};

// 转换 SDL 鼠标按钮到 CEF
CefBrowserHost::MouseButtonType convert_mouse_button(Uint8 sdl_button);

// 创建 CEF 鼠标事件
CefMouseEvent create_mouse_event(const ImVec2& mouse_pos, const ImVec2& item_pos,
                                 uint32_t modifiers = 0);

// 获取当前修饰键状态
uint32_t get_modifiers(bool is_left_down = false);

// 发送鼠标点击事件
void send_mouse_click(CefRefPtr<CefBrowser> browser, const ImVec2& mouse_pos,
                      const ImVec2& item_pos, CefBrowserHost::MouseButtonType button,
                      bool mouse_up, int click_count);

// 发送鼠标移动事件
void send_mouse_move(CefRefPtr<CefBrowser> browser, const ImVec2& mouse_pos,
                     const ImVec2& item_pos, bool mouse_leave = false);

// 发送鼠标滚轮事件
void send_mouse_wheel(CefRefPtr<CefBrowser> browser, const ImVec2& mouse_pos,
                      const ImVec2& item_pos, float wheel_delta);

}  // namespace Corona::Systems::UI::MouseUtils
