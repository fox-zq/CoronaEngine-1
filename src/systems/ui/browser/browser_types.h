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

    bool open = true;
    bool needs_resize = false;
    bool buffer_dirty = false;
    bool has_focus = false;

    char url_buffer[1024] = "";
    std::vector<uint8_t> pixel_buffer;
};