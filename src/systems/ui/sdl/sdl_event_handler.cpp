#include "sdl_event_handler.h"

#include <corona/systems/ui/imgui_system.h>
#include <imgui_impl_sdl3.h>

namespace Corona::Systems::UI {

bool SDLEventHandler::is_input_method_switch(const SDL_Event& event) {
    if (event.type != SDL_EVENT_KEY_DOWN && event.type != SDL_EVENT_KEY_UP) {
        return false;
    }

    int key = static_cast<int>(event.key.key);
    bool ctrl = (event.key.mod & SDL_KMOD_CTRL) != 0;
    bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
    bool alt = (event.key.mod & SDL_KMOD_ALT) != 0;

    return ((ctrl && shift) || (alt && shift) ||
            (ctrl && key == SDLK_SPACE) ||
            (event.key.mod & SDL_KMOD_GUI && key == SDLK_SPACE));
}

bool SDLEventHandler::should_process_in_imgui(const SDL_Event& event, int url_input_active_tab) {
    // 键盘和文本输入事件的特殊处理
    if (event.type == SDL_EVENT_KEY_DOWN ||
        event.type == SDL_EVENT_KEY_UP ||
        event.type == SDL_EVENT_TEXT_INPUT ||
        event.type == SDL_EVENT_TEXT_EDITING) {
        return url_input_active_tab != -1;
    }
    return true;
}

EventProcessResult SDLEventHandler::process_events(
    SDL_Window* window,
    int current_url_input_active_tab,
    KeyEventCallback on_key_event,
    KeyEventCallback on_text_event,
    KeyEventCallback on_ime_event) {
    EventProcessResult result;
    result.url_input_active_tab = current_url_input_active_tab;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // 检查是否为输入法切换
        if (is_input_method_switch(event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            continue;
        }

        bool should_pass_to_imgui = should_process_in_imgui(event, result.url_input_active_tab);

        // 处理键盘和文本输入事件
        if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
            if (on_key_event) on_key_event(event);
        } else if (event.type == SDL_EVENT_TEXT_INPUT) {
            if (on_text_event) on_text_event(event);
        } else if (event.type == SDL_EVENT_TEXT_EDITING) {
            if (on_ime_event) on_ime_event(event);
        }

        // 将事件传递给 ImGui
        if (should_pass_to_imgui) {
            ImGui_ImplSDL3_ProcessEvent(&event);
        }

        // 处理特定事件类型
        switch (event.type) {
            case SDL_EVENT_QUIT:
                result.should_quit = true;
                if (should_pass_to_imgui) {
                    ImGui_ImplSDL3_ProcessEvent(&event);
                }
                break;

            case SDL_EVENT_WINDOW_RESIZED:
                if (event.window.windowID == SDL_GetWindowID(window)) {
                    result.window_resized = true;
                    if (should_pass_to_imgui) {
                        ImGui_ImplSDL3_ProcessEvent(&event);
                    }
                }
                break;

            case SDL_EVENT_WINDOW_FOCUS_GAINED:
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                if (should_pass_to_imgui) {
                    ImGui_ImplSDL3_ProcessEvent(&event);
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            case SDL_EVENT_MOUSE_MOTION:
            case SDL_EVENT_MOUSE_WHEEL:
                ImGui_ImplSDL3_ProcessEvent(&event);
                break;

            default:
                if (should_pass_to_imgui) {
                    ImGui_ImplSDL3_ProcessEvent(&event);
                }
                break;
        }
    }

    return result;
}

}  // namespace Corona::Systems::UI
