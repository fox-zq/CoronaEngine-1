#pragma once

#include <SDL3/SDL.h>
#include <include/internal/cef_types.h>
#include <vulkan/vulkan.h>

#include <string>
#include <vector>

class OffscreenCefClient;

struct BrowserTab {
    std::string name;
    std::string url;

    OffscreenCefClient* client = nullptr;
    VkDescriptorSet texture_id = VK_NULL_HANDLE;

    int width = 800;
    int height = 600;
    // 添加 docking 相关属性
    std::string docking_pos = "";   // docking位置，如"left", "right", "top", "bottom", "center"
    int dock_width = 0;             // 如果指定宽度，0表示自动
    int dock_height = 0;            // 如果指定高度，0表示自动
    bool dock_fixed = false;        // 是否固定位置，不能移动
    bool dock_initialized = false;  // 是否已初始化docking

    bool open = true;
    bool needs_resize = false;
    bool buffer_dirty = false;
    bool has_focus = false;

    char url_buffer[1024] = "";
    std::vector<uint8_t> pixel_buffer;
};