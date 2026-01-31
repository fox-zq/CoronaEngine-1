#pragma once
// browser_types.h

#pragma once

#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <cef_client.h>
#include <string>
#include <unordered_map>
#include <vector>

// 前向声明
namespace Corona::Systems {
class VulkanBackend;
}
namespace Corona::CefSpace {
class OffscreenCefClient;
}

// 浏览器窗口数据结构
struct BrowserTab {
    std::string name;
    std::string url;
    class OffscreenCefClient* client;  // 使用前向声明的类
    VkDescriptorSet textureId = VK_NULL_HANDLE;
    int width = 800;
    int height = 600;
    bool open = true;
    bool needsResize = false;
    char urlBuffer[1024] = "";
    std::vector<uint8_t> pixelBuffer;
    bool bufferDirty = false;

    
    bool hasFocus = false;
};

// 全局变量声明（在实际的cpp文件中定义）
extern std::unordered_map<int, BrowserTab*> g_tabs;
extern int g_tabCounter;
extern Corona::Systems::VulkanBackend* g_vulkan_backend;


// 键码转换函数
static int ConvertSDLKeyCodeToWindows(int sdl_key) {
    if (sdl_key >= SDLK_A && sdl_key <= SDLK_Z) {
        return 0x41 + (sdl_key - SDLK_A);  // A-Z: 0x41-0x5A
    }

    // 数字键映射
    if (sdl_key >= SDLK_0 && sdl_key <= SDLK_9) {
        return 0x30 + (sdl_key - SDLK_0);  // 0-9: 0x30-0x39
    }

    switch (sdl_key) {
        // 符号键映射
        case SDLK_RETURN:
            return 0x0D;  // VK_RETURN
        case SDLK_GRAVE:
            return 0xC0;
        case SDLK_MINUS:
            return 0xBD;
        case SDLK_EQUALS:
            return 0xBB;
        case SDLK_LEFTBRACKET:
            return 0xDB;
        case SDLK_RIGHTBRACKET:
            return 0xDD;
        case SDLK_BACKSLASH:
            return 0xDC;
        case SDLK_SEMICOLON:
            return 0xBA;
        case SDLK_APOSTROPHE:
            return 0xDE;
        case SDLK_COMMA:
            return 0xBC;
        case SDLK_PERIOD:
            return 0xBE;
        case SDLK_SLASH:
            return 0xBF;

        // 导航键映射
        case SDLK_LEFT:
            return 0x25;  // VK_LEFT
        case SDLK_UP:
            return 0x26;  // VK_UP
        case SDLK_RIGHT:
            return 0x27;  // VK_RIGHT
        case SDLK_DOWN:
            return 0x28;  // VK_DOWN
        case SDLK_HOME:
            return 0x24;  // VK_HOME
        case SDLK_END:
            return 0x23;  // VK_END
        case SDLK_PAGEUP:
            return 0x21;  // VK_PRIOR
        case SDLK_PAGEDOWN:
            return 0x22;  // VK_NEXT
        case SDLK_INSERT:
            return 0x2D;  // VK_INSERT
        case SDLK_DELETE:
            return 0x2E;  // VK_DELETE
        case SDLK_BACKSPACE:
            return 0x08;  // VK_BACK

        // 小键盘键
        case SDLK_KP_0:
            return 0x60;
        case SDLK_KP_1:
            return 0x61;
        case SDLK_KP_2:
            return 0x62;
        case SDLK_KP_3:
            return 0x63;
        case SDLK_KP_4:
            return 0x64;
        case SDLK_KP_5:
            return 0x65;
        case SDLK_KP_6:
            return 0x66;
        case SDLK_KP_7:
            return 0x67;
        case SDLK_KP_8:
            return 0x68;
        case SDLK_KP_9:
            return 0x69;
        case SDLK_KP_MULTIPLY:
            return 0x6A;
        case SDLK_KP_PLUS:
            return 0x6B;
        case SDLK_KP_MINUS:
            return 0x6D;
        case SDLK_KP_DECIMAL:
            return 0x6E;
        case SDLK_KP_DIVIDE:
            return 0x6F;
        case SDLK_KP_ENTER:
            return 0x0D;

        // 功能键
        case SDLK_F1:
            return 0x70;
        case SDLK_F2:
            return 0x71;
        case SDLK_F3:
            return 0x72;
        case SDLK_F4:
            return 0x73;
        case SDLK_F5:
            return 0x74;
        case SDLK_F6:
            return 0x75;
        case SDLK_F7:
            return 0x76;
        case SDLK_F8:
            return 0x77;
        case SDLK_F9:
            return 0x78;
        case SDLK_F10:
            return 0x79;
        case SDLK_F11:
            return 0x7A;
        case SDLK_F12:
            return 0x7B;

        default:
            return sdl_key;
    }
}

// 判断是否是修饰键
static bool IsModifierKey(int key) {
    return key == SDLK_LCTRL || key == SDLK_RCTRL ||
           key == SDLK_LSHIFT || key == SDLK_RSHIFT ||
           key == SDLK_LALT || key == SDLK_RALT ||
           key == SDLK_LGUI || key == SDLK_RGUI;
}

// 判断是否应该发送CHAR事件
static bool ShouldSendCharEvent(int key, int modifiers) {
    // 修饰键不发送CHAR事件
    if (IsModifierKey(key)) {
        return false;
    }

    // 功能键不发送CHAR事件
    if ((key >= SDLK_F1 && key <= SDLK_F12)) {
        return false;
    }

    // 回车键需要发送CHAR事件以便浏览器处理换行
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        return true;
    }

    // Ctrl+字母组合键（用于快捷键）应发送CHAR事件
    if (modifiers & EVENTFLAG_CONTROL_DOWN) {
        if ((key >= SDLK_A && key <= SDLK_Z) ||
            (key >= SDLK_0 && key <= SDLK_9)) {
            return true;
        }
    }

    // 导航键不发送CHAR事件
    switch (key) {
        case SDLK_ESCAPE:
        case SDLK_TAB:
        case SDLK_CAPSLOCK:
        case SDLK_PRINTSCREEN:
        case SDLK_SCROLLLOCK:
        case SDLK_PAUSE:
        case SDLK_INSERT:
        case SDLK_HOME:
        case SDLK_PAGEUP:
        case SDLK_DELETE:
        case SDLK_END:
        case SDLK_PAGEDOWN:
        case SDLK_RIGHT:
        case SDLK_LEFT:
        case SDLK_DOWN:
        case SDLK_UP:
        case SDLK_NUMLOCKCLEAR:
        case SDLK_KP_CLEAR:
        case SDLK_BACKSPACE:
            return false;
    }

    // 如果Alt键按下，通常不发送CHAR事件（用于菜单快捷键）
    if (modifiers & EVENTFLAG_ALT_DOWN) {
        if (key >= SDLK_KP_0 && key <= SDLK_KP_9) {
            return true;
        }
        return false;
    }

    return true;
}