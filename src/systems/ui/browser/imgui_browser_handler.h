#pragma once
#include "browser_manager.h"
#include <SDL3/SDL_events.h>
#include <imgui.h>
#include <vector>
#include <string>

namespace Corona::Systems::UI {

class ImGuiBrowserHandler {
public:
    // 渲染所有浏览器标签页
    static void render_browser_tabs(int& active_tab_id, int& url_input_active_tab);

    // 处理浏览器相关的 SDL 事件
    static void process_browser_input(const SDL_Event& event, int active_tab_id);

    // 发送键盘事件到活动浏览器
    static void send_key_events(int tab_id);

    // 清理所有浏览器资源
    static void shutdown_all_browsers();

    // 添加待处理的键盘事件
    static void add_pending_key_event(const SDL_Event& event);

    // 清空待处理事件
    static void clear_pending_events();

private:
    // 键盘事件类型
    struct PendingKeyEvent {
        enum Type { kMKeyEvent, kTextEvent, kImeComposition };

        Type type;
        int key_code = 0;
        int scan_code = 0;
        int modifiers = 0;
        bool pressed = false;
        bool is_modifier_combo = false;
        std::string text;
        int ime_start = 0;
        int ime_length = 0;

        explicit PendingKeyEvent(Type t) : type(t) {}
    };

    static std::vector<PendingKeyEvent> pending_key_events_;

    // 鼠标双击状态
    static bool is_left_mouse_down_;
    static bool is_mouse_dragging_;
    static int manual_click_count_;
    static Uint32 last_click_time_;
    static ImVec2 last_click_pos_;
    static constexpr Uint32 kDoubleClickTime = 500;
    static constexpr float kDoubleClickDist = 5.0f;

    // 渲染单个标签页
    static void render_single_tab(int tab_id, UI::BrowserTab* tab,
                                   int& active_tab_id, int& url_input_active_tab);

    // 处理浏览器鼠标事件
    static void handle_browser_mouse_events(UI::BrowserTab* tab, int tab_id,
                                            int& active_tab_id, int& url_input_active_tab);
};

} // namespace Corona::Systems::UI
