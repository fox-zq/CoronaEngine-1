#include "browser_input_handler.h"
#include <SDL3/SDL.h>
#include "../sdl/sdl_key_utils.h"

namespace Corona::Systems::UI {

void BrowserInputHandler::clear_pending_events() {
    pending_key_events_.clear();
}

void BrowserInputHandler::process_sdl_key_event(const SDL_Event& event) {
    bool pressed = (event.type == SDL_EVENT_KEY_DOWN);
    int key_code = static_cast<int>(event.key.key);
    int scan_code = static_cast<int>(event.key.scancode);
    int modifiers = 0;

    // 转换SDL modifiers到CEF modifiers
    Uint32 sdl_mod = event.key.mod;
    if (sdl_mod & SDL_KMOD_CTRL) modifiers |= EVENTFLAG_CONTROL_DOWN;
    if (sdl_mod & SDL_KMOD_SHIFT) modifiers |= EVENTFLAG_SHIFT_DOWN;
    if (sdl_mod & SDL_KMOD_ALT) modifiers |= EVENTFLAG_ALT_DOWN;
    if (sdl_mod & SDL_KMOD_GUI) modifiers |= EVENTFLAG_COMMAND_DOWN;
    if (sdl_mod & SDL_KMOD_CAPS) modifiers |= EVENTFLAG_CAPS_LOCK_ON;
    if (sdl_mod & SDL_KMOD_NUM) modifiers |= EVENTFLAG_NUM_LOCK_ON;

    // 检测常见的编辑组合键
    bool is_common_edit_shortcut = false;
    if (modifiers & EVENTFLAG_CONTROL_DOWN) {
        switch (key_code) {
            case SDLK_A:  // Ctrl+A (全选)
            case SDLK_C:  // Ctrl+C (复制)
            case SDLK_V:  // Ctrl+V (粘贴)
            case SDLK_Z:  // Ctrl+Z (撤销)
            case SDLK_Y:  // Ctrl+Y (重做/复原)
                is_common_edit_shortcut = true;
                break;
            default:
                break;
        }
    }

    // 对于Ctrl/Alt+字母等组合键，需要特殊处理
    bool is_modifier_combo = (modifiers & (EVENTFLAG_CONTROL_DOWN | EVENTFLAG_ALT_DOWN)) &&
                             ((key_code >= 'a' && key_code <= 'z') ||
                              (key_code >= 'A' && key_code <= 'Z') ||
                              (key_code >= '0' && key_code <= '9'));

    // 存储键盘事件
    PendingKeyEvent key_event(PendingKeyEvent::kMKeyEvent);
    key_event.key_code = key_code;
    key_event.scan_code = scan_code;
    key_event.modifiers = modifiers;
    key_event.pressed = pressed;
    key_event.is_modifier_combo = is_modifier_combo || is_common_edit_shortcut;  // 标记为组合键

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

void BrowserInputHandler::send_key_events_to_browser(CefRefPtr<CefBrowser> browser) {
    if (!browser) return;

    for (const auto& pending_event : pending_key_events_) {
        if (pending_event.type == PendingKeyEvent::kMKeyEvent) {
            CefKeyEvent cef_key_event;

            // 设置事件类型
            cef_key_event.type = pending_event.pressed ? KEYEVENT_RAWKEYDOWN : KEYEVENT_KEYUP;

            // 转换键码
            cef_key_event.windows_key_code = KeyUtils::convert_sdl_key_code_to_windows(pending_event.key_code);
            cef_key_event.native_key_code = pending_event.scan_code;

            // 设置修饰键
            cef_key_event.modifiers = pending_event.modifiers;

            // 对于组合键（如Ctrl+C），需要特殊处理
            cef_key_event.character = pending_event.key_code;
            cef_key_event.unmodified_character = pending_event.key_code;

            // 对于常见的编辑组合键，发送完整的键序列
            bool is_common_edit_shortcut = false;
            if (pending_event.modifiers & EVENTFLAG_CONTROL_DOWN) {
                switch (pending_event.key_code) {
                    case SDLK_A:  // Ctrl+A (全选)
                    case SDLK_C:  // Ctrl+C (复制)
                    case SDLK_V:  // Ctrl+V (粘贴)
                    case SDLK_Z:  // Ctrl+Z (撤销)
                    case SDLK_Y:  // Ctrl+Y (重做/复原)
                        is_common_edit_shortcut = true;
                        break;
                    default:
                        break;
                }
            }

            // 发送RAWKEYDOWN或KEYUP事件
            browser->GetHost()->SendKeyEvent(cef_key_event);

            // 特殊处理回车键：总是发送CHAR事件
            if (pending_event.pressed &&
                (pending_event.key_code == SDLK_RETURN || pending_event.key_code == SDLK_KP_ENTER)) {
                // 对于回车键，需要发送CHAR事件以便浏览器能处理换行
                CefKeyEvent char_event = cef_key_event;
                char_event.type = KEYEVENT_CHAR;
                char_event.character = 0x0D;  // 回车符的ASCII码
                char_event.unmodified_character = 0x0D;
                browser->GetHost()->SendKeyEvent(char_event);
            }

            // 对于组合键，需要发送CHAR事件以确保浏览器能正确处理
            if (pending_event.pressed && pending_event.is_modifier_combo) {
                // 对于编辑组合键，发送CHAR事件
                if (is_common_edit_shortcut) {
                    cef_key_event.type = KEYEVENT_CHAR;
                    browser->GetHost()->SendKeyEvent(cef_key_event);
                } else {
                    // 对于其他组合键，根据原始逻辑处理
                    switch (pending_event.key_code) {
                        case SDLK_RETURN:
                        case SDLK_KP_ENTER:
                        case SDLK_TAB:
                        case SDLK_BACKSPACE:
                        case SDLK_DELETE:
                        case SDLK_ESCAPE:
                            // 这些特殊键需要发送CHAR事件
                            cef_key_event.type = KEYEVENT_CHAR;
                            browser->GetHost()->SendKeyEvent(cef_key_event);
                            break;

                        default:
                            // 对于字母、数字、符号等常规组合键，不发送CHAR事件
                            // 这些字符将通过TEXT_EVENT处理
                            break;
                    }
                }
            }
        } else if (pending_event.type == PendingKeyEvent::kTextEvent) {
            // 处理文本输入 - 所有字符输入都通过这里处理
            const std::string& text = pending_event.text;
            if (!text.empty()) {
                // 检查文本中是否包含控制字符
                bool has_control_chars = false;
                for (char c : text) {
                    if (c == '\b' || c == '\t' || c == '\n' || c == '\r') {
                        has_control_chars = true;
                        break;
                    }
                }

                if (!has_control_chars) {
                    // 处理普通文本字符
                    bool is_ascii = true;
                    for (char c : text) {
                        if (static_cast<unsigned char>(c) >= 128) {
                            is_ascii = false;
                            break;
                        }
                    }

                    if (is_ascii) {
                        // ASCII文本，直接发送
                        for (char c : text) {
                            if (c >= 32 && c < 127) {  // 可打印字符
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
                        // 非ASCII文本（中文），使用UTF-16转换
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

    // 清空待处理事件
    clear_pending_events();
}

} // namespace Corona::Systems::UI
