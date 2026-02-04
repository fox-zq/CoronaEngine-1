#include "browser_manager.h"
#include <filesystem>
#include <iostream>
#include <cstring>
#include "cef_client.h"
#include <corona/systems/ui/vulkan_backend.h>
#include <imgui_impl_vulkan.h>

namespace fs = std::filesystem;

namespace Corona::Systems::UI {

// Helper functions (formerly in browser_window.cpp)
static std::string convert_local_path_to_url(const std::string& local_path) {
    if (local_path.find("http://") == 0 || local_path.find("https://") == 0) {
        return local_path;
    }
    if (fs::path(local_path).is_absolute()) {
        std::string url = "file://";
        for (char c : local_path) {
            if (c == '\\') url += '/';
            else if (c == ' ') url += "%20";
            else url += c;
        }
        return url;
    }
    return local_path;
}

BrowserManager& BrowserManager::instance() {
    static BrowserManager instance;
    return instance;
}

int BrowserManager::create_tab(const std::string& url, const std::string& path) {
    auto tab = std::make_unique<BrowserTab>();

    int id = ++tab_counter_;

    // Initial Size
    tab->width = 1600;
    tab->height = 900;

    tab->name = "Browser " + std::to_string(id);

    // URL Processing
    std::string full_url = convert_local_path_to_url(url);
    if (!path.empty()) {
        std::string clean_fragment = path;
        if (clean_fragment.starts_with("#")) {
            clean_fragment = clean_fragment.substr(1);
        }
        size_t hash_pos = full_url.find('#');
        if (hash_pos != std::string::npos) {
            full_url = full_url.substr(0, hash_pos);
        }
        full_url += "#" + clean_fragment;
    }
    std::cout << "Loading URL: " << full_url << std::endl;

    tab->url = full_url;
    strncpy(tab->url_buffer, full_url.c_str(), sizeof(tab->url_buffer) - 1);

    tab->client = new OffscreenCefClient();
    tab->client->SetTab(tab.get());

    // Create OpenGL/Vulkan Texture
    tab->texture_id = create_browser_texture(tab->width, tab->height);

    // Create Offscreen Browser
    CefWindowInfo window_info;
    window_info.SetAsWindowless(GetDesktopWindow());

    CefBrowserSettings browser_settings;
    browser_settings.windowless_frame_rate = 60;
    browser_settings.javascript = STATE_ENABLED;
    browser_settings.local_storage = STATE_ENABLED;
    browser_settings.webgl = STATE_ENABLED;

    CefBrowserHost::CreateBrowser(window_info, tab->client, full_url, browser_settings, nullptr, nullptr);

    tabs_[id] = std::move(tab);
    return id;
}

BrowserTab* BrowserManager::get_tab(int tab_id) {
    auto it = tabs_.find(tab_id);
    return it != tabs_.end() ? it->second.get() : nullptr;
}

void BrowserManager::remove_tab(int tab_id) {
    if (!tabs_.contains(tab_id)) return;

    BrowserTab* tab = tabs_[tab_id].get();
    if (tab->client && tab->client->GetBrowser()) {
        tab->client->GetBrowser()->GetHost()->CloseBrowser(true);
    }
    if (tab->texture_id != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(tab->texture_id);

        auto it = owned_images_.find(tab->texture_id);
        if (it != owned_images_.end() && vulkan_backend_) {
            VkDevice device = vulkan_backend_->get_device();
            vkDestroySampler(device, it->second.sampler, nullptr);
            vkDestroyImageView(device, it->second.view, nullptr);
            vkFreeMemory(device, it->second.memory, nullptr);
            vkDestroyImage(device, it->second.image, nullptr);
            owned_images_.erase(it);
        }
        tab->texture_id = VK_NULL_HANDLE;
    }

    tabs_.erase(tab_id);
}

VkDescriptorSet BrowserManager::create_browser_texture(int width, int height) {
    if (!vulkan_backend_) return VK_NULL_HANDLE;

    VkDevice device = vulkan_backend_->get_device();
    VkPhysicalDevice phys = vulkan_backend_->get_physical_device();

    // Create VkImage
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = VK_FORMAT_B8G8R8A8_UNORM;
    image_info.extent.width = static_cast<uint32_t>(width);
    image_info.extent.height = static_cast<uint32_t>(height);
    image_info.extent.depth = 1;
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage image;
    if (vkCreateImage(device, &image_info, nullptr, &image) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(device, image, &mem_reqs);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;

    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(phys, &mem_props);
    uint32_t memTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((mem_reqs.memoryTypeBits & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) {
            memTypeIndex = i;
            break;
        }
    }
    if (memTypeIndex == UINT32_MAX) {
        vkDestroyImage(device, image, nullptr);
        return VK_NULL_HANDLE;
    }
    alloc_info.memoryTypeIndex = memTypeIndex;

    VkDeviceMemory image_memory;
    if (vkAllocateMemory(device, &alloc_info, nullptr, &image_memory) != VK_SUCCESS) {
        vkDestroyImage(device, image, nullptr);
        return VK_NULL_HANDLE;
    }

    vkBindImageMemory(device, image, image_memory, 0);

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = VK_FORMAT_B8G8R8A8_UNORM;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.baseMipLevel = 0;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.baseArrayLayer = 0;
    view_info.subresourceRange.layerCount = 1;

    VkImageView image_view;
    if (vkCreateImageView(device, &view_info, nullptr, &image_view) != VK_SUCCESS) {
        vkFreeMemory(device, image_memory, nullptr);
        vkDestroyImage(device, image, nullptr);
        return VK_NULL_HANDLE;
    }

    VkSamplerCreateInfo samp_info{};
    samp_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samp_info.magFilter = VK_FILTER_LINEAR;
    samp_info.minFilter = VK_FILTER_LINEAR;
    samp_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samp_info.anisotropyEnable = VK_FALSE;
    samp_info.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samp_info.unnormalizedCoordinates = VK_FALSE;

    VkSampler sampler;
    if (vkCreateSampler(device, &samp_info, nullptr, &sampler) != VK_SUCCESS) {
        vkDestroyImageView(device, image_view, nullptr);
        vkFreeMemory(device, image_memory, nullptr);
        vkDestroyImage(device, image, nullptr);
        return VK_NULL_HANDLE;
    }

    VkDescriptorSet descriptor = ImGui_ImplVulkan_AddTexture(sampler, image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    owned_images_[descriptor] = {image, image_memory, image_view, sampler, static_cast<uint32_t>(width), static_cast<uint32_t>(height)};

    return descriptor;
}

void BrowserManager::update_texture(int tab_id) {
    if (!vulkan_backend_) return;

    auto it = tabs_.find(tab_id);
    if (it == tabs_.end()) return;

    BrowserTab* tab = it->second.get();

    if (!(tab->buffer_dirty && !tab->pixel_buffer.empty() && tab->texture_id != VK_NULL_HANDLE)) return;

    VkDevice device = vulkan_backend_->get_device();
    VkPhysicalDevice phys = vulkan_backend_->get_physical_device();
    VkQueue queue = vulkan_backend_->get_queue();

    OwnedImage* found = nullptr;
    auto image_it = owned_images_.find(tab->texture_id);
    if (image_it != owned_images_.end()) {
        found = &image_it->second;
    }
    if (!found) {
        tab->buffer_dirty = false;
        return;
    }

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    VkDeviceSize imageSize = tab->pixel_buffer.size();

    VkBufferCreateInfo buf_info{};
    buf_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_info.size = imageSize;
    buf_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buf_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    vkCreateBuffer(device, &buf_info, nullptr, &stagingBuffer);
    VkMemoryRequirements mem_reqs;
    vkGetBufferMemoryRequirements(device, stagingBuffer, &mem_reqs);

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.allocationSize = mem_reqs.size;

    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(phys, &mem_props);
    uint32_t memTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((mem_reqs.memoryTypeBits & (1u << i)) &&
            (mem_props.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) == (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            memTypeIndex = i;
            break;
        }
    }
    if (memTypeIndex == UINT32_MAX) {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        tab->buffer_dirty = false;
        return;
    }
    alloc_info.memoryTypeIndex = memTypeIndex;
    vkAllocateMemory(device, &alloc_info, nullptr, &stagingMemory);
    vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);

    void* mapped;
    vkMapMemory(device, stagingMemory, 0, imageSize, 0, &mapped);
    memcpy(mapped, tab->pixel_buffer.data(), imageSize);
    vkUnmapMemory(device, stagingMemory);

    VkCommandPool cmdPool;
    VkCommandBuffer cmdBuf;
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = vulkan_backend_->get_queue_family();
    vkCreateCommandPool(device, &pool_info, nullptr, &cmdPool);

    VkCommandBufferAllocateInfo alloc_cmd{};
    alloc_cmd.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_cmd.commandPool = cmdPool;
    alloc_cmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_cmd.commandBufferCount = 1;
    vkAllocateCommandBuffers(device, &alloc_cmd, &cmdBuf);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmdBuf, &beginInfo);

    VkImageMemoryBarrier barrier_to_dst{};
    barrier_to_dst.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier_to_dst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier_to_dst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier_to_dst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier_to_dst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier_to_dst.image = found->image;
    barrier_to_dst.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier_to_dst.subresourceRange.baseMipLevel = 0;
    barrier_to_dst.subresourceRange.levelCount = 1;
    barrier_to_dst.subresourceRange.baseArrayLayer = 0;
    barrier_to_dst.subresourceRange.layerCount = 1;
    barrier_to_dst.srcAccessMask = 0;
    barrier_to_dst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &barrier_to_dst);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {static_cast<uint32_t>(tab->width), static_cast<uint32_t>(tab->height), 1};

    vkCmdCopyBufferToImage(cmdBuf, stagingBuffer, found->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    VkImageMemoryBarrier barrier_to_shader{};
    barrier_to_shader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier_to_shader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier_to_shader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier_to_shader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier_to_shader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier_to_shader.image = found->image;
    barrier_to_shader.subresourceRange = barrier_to_dst.subresourceRange;
    barrier_to_shader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier_to_shader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &barrier_to_shader);

    vkEndCommandBuffer(cmdBuf);

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmdBuf;

    vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, cmdPool, 1, &cmdBuf);
    vkDestroyCommandPool(device, cmdPool, nullptr);

    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);

    tab->buffer_dirty = false;
}

void BrowserManager::resize_tab(int tab_id, int width, int height) {
    auto it = tabs_.find(tab_id);
    if (it == tabs_.end()) return;

    BrowserTab* tab = it->second.get();
    if (width <= 0 || height <= 0) return;
    if (width == tab->width && height == tab->height) return;

    tab->width = width;
    tab->height = height;

    // Recreate texture with new size
    // Note: Old texture cleanup is handled by creating new one?
    // Wait, create_browser_texture returns a new descriptor.
    // We should probably clean up the old one if we can, or just overwrite the ID in tab.
    // ImGui_ImplVulkan_RemoveTexture should be called on the old one.

    if (tab->texture_id != VK_NULL_HANDLE) {
        ImGui_ImplVulkan_RemoveTexture(tab->texture_id);

        auto img_it = owned_images_.find(tab->texture_id);
        if (img_it != owned_images_.end() && vulkan_backend_) {
            VkDevice device = vulkan_backend_->get_device();
            vkDestroySampler(device, img_it->second.sampler, nullptr);
            vkDestroyImageView(device, img_it->second.view, nullptr);
            vkFreeMemory(device, img_it->second.memory, nullptr);
            vkDestroyImage(device, img_it->second.image, nullptr);
            owned_images_.erase(img_it);
        }
    }

    tab->texture_id = create_browser_texture(tab->width, tab->height);

    if (tab->client) {
        tab->client->Resize(tab->width, tab->height);
    }
}

void BrowserManager::set_vulkan_backend(VulkanBackend* backend) {
    vulkan_backend_ = backend;
}

VulkanBackend* BrowserManager::get_vulkan_backend() const {
    return vulkan_backend_;
}

const std::unordered_map<int, std::unique_ptr<BrowserTab>>& BrowserManager::get_tabs() const {
    return tabs_;
}

std::unordered_map<int, std::unique_ptr<BrowserTab>>& BrowserManager::get_tabs() {
    return tabs_;
}

}

