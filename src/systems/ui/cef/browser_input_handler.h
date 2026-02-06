#pragma once

#include <SDL3/SDL.h>
#include <include/internal/cef_types.h>
#include <vector>
#include <string>
#include <memory>
#include <include/cef_browser.h>

#include "pending_key_event.h"

namespace Corona::Systems::UI {

class BrowserInputHandler {
public:
    BrowserInputHandler() = default;

    // 清除待处理事件
    void clear_pending_events();

    // 处理SDL键盘事件
    void process_sdl_key_event(const SDL_Event& event);

    // 处理SDL文本输入事件
    void process_sdl_text_event(const SDL_Event& event);

    // 处理SDL IME事件
    void process_sdl_ime_event(const SDL_Event& event);

    // 发送键盘事件到浏览器
    void send_key_events_to_browser(CefRefPtr<CefBrowser> browser);

private:
    std::vector<PendingKeyEvent> pending_key_events_;
};

} // namespace Corona::Systems::UI
