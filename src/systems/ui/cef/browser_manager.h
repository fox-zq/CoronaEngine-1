#pragma once

#include <SDL3/SDL.h>
#include <include/internal/cef_types.h>
#include <vulkan/vulkan.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace Corona::Systems {
class VulkanBackend;
}

namespace Corona::Systems::UI {

class OffscreenCefClient;

// ============================================================================
// 浏览器标签页数据结构
// ============================================================================

struct BrowserTab {
    std::string name;
    std::string url;

    OffscreenCefClient* client = nullptr;
    VkDescriptorSet texture_id = VK_NULL_HANDLE;

    int width = 800;
    int height = 600;

    // Docking 相关属性
    std::string docking_pos;        // 位置: "left", "right", "top", "bottom", "center"
    int dock_width = 0;             // 指定宽度，0 表示自动
    int dock_height = 0;            // 指定高度，0 表示自动
    bool dock_fixed = false;        // 是否固定位置
    bool dock_initialized = false;  // 是否已初始化 docking

    bool open = true;
    bool needs_resize = false;
    bool buffer_dirty = false;
    bool has_focus = false;

    char url_buffer[1024] = "";
    std::vector<uint8_t> pixel_buffer;
    std::mutex mutex;  // 保护 pixel_buffer 和 buffer_dirty
};

// ============================================================================
// 浏览器标签管理器
// ============================================================================

class BrowserManager {
   public:
    static BrowserManager& instance();

    int create_tab(const std::string& url, const std::string& path = "",
                   const std::string& docking_pos = "",
                   int dock_width = 0, int dock_height = 0,
                   bool dock_fixed = false);
    BrowserTab* get_tab(int tab_id);
    void remove_tab(int tab_id);
    void update_texture(int tab_id);
    void resize_tab(int tab_id, int width, int height);

    void set_vulkan_backend(VulkanBackend* backend);
    VulkanBackend* get_vulkan_backend() const;

    const std::unordered_map<int, std::unique_ptr<BrowserTab>>& get_tabs() const;
    std::unordered_map<int, std::unique_ptr<BrowserTab>>& get_tabs();

    void update();
    void close_all_tabs();

   private:
    BrowserManager() = default;

    VkDescriptorSet create_browser_texture(int width, int height);
    void destroy_tab_texture(BrowserTab* tab);

    struct OwnedImage {
        VkImage image;
        VkDeviceMemory memory;
        VkImageView view;
        VkSampler sampler;
        uint32_t width;
        uint32_t height;
    };

    std::unordered_map<int, std::unique_ptr<BrowserTab>> tabs_;
    std::vector<int> tabs_to_close_;
    std::map<VkDescriptorSet, OwnedImage> owned_images_;
    int tab_counter_ = 0;
    VulkanBackend* vulkan_backend_ = nullptr;
};

}  // namespace Corona::Systems::UI
