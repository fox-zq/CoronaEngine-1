#pragma once
// browser_types.h

#pragma once

#include <vulkan/vulkan.h>

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