#pragma once

#include <SDL3/SDL.h>
#include <vector>
#include <string>
#include <functional>

namespace Corona::Systems::UI {

// SDL 事件处理结果
struct EventProcessResult {
    bool should_quit = false;
    bool window_resized = false;
    int url_input_active_tab = -1;
};

// SDL 事件处理器
class SDLEventHandler {
public:
    SDLEventHandler() = default;

    // 回调函数类型定义
    using KeyEventCallback = std::function<void(const SDL_Event&)>;

    // 处理所有 SDL 事件
    EventProcessResult process_events(SDL_Window* window,
                                      int current_url_input_active_tab,
                                      KeyEventCallback on_key_event = nullptr,
                                      KeyEventCallback on_text_event = nullptr,
                                      KeyEventCallback on_ime_event = nullptr);

private:
    // 判断是否为输入法切换组合键
    bool is_input_method_switch(const SDL_Event& event);

    // 判断是否应该传递给 ImGui
    bool should_process_in_imgui(const SDL_Event& event, int url_input_active_tab);
};

}  // namespace Corona::Systems::UI
