#pragma once

#include <string>

namespace Corona::Systems::UI {

// 键盘输入事件结构体 (移动自 ImguiSystem 为了解耦)
struct PendingKeyEvent {
    enum EventType {
        kMKeyEvent,
        kTextEvent,
        kImeComposition
    };

    EventType type;
    int key_code = 0;
    int scan_code = 0;
    int modifiers = 0;
    bool pressed = false;
    std::string text;
    int ime_start = 0;
    int ime_length = 0;
    bool is_modifier_combo = false;

    explicit PendingKeyEvent(EventType t) : type(t) {}
};

} // namespace Corona::Systems::UI
