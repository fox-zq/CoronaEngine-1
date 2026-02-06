#pragma once

#include <vulkan/vulkan.h>

#include <map>
#include <memory>
#include <unordered_map>

#include "browser_types.h"

namespace Corona::Systems {
class VulkanBackend;
}

namespace Corona::Systems::UI {

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
