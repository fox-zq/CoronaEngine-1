// BrowserWindow.cpp
#include "browser_window.h"

#include <corona/systems/ui/vulkan_backend.h>
#include <imgui_impl_vulkan.h>

#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>

#include "browser_types.h"
#include "cef_client.h"

// 全局变量定义
std::unordered_map<int, BrowserTab*> tabs;
int tab_counter = 0;
Corona::Systems::VulkanBackend* g_vulkan_backend = nullptr;
class OffscreenCefClient;
// Track owned Vulkan resources for each ImGui descriptor set
struct OwnedImage {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView view;
    VkSampler sampler;
    uint32_t width;
    uint32_t height;
};
static std::map<VkDescriptorSet, OwnedImage> owned_images;

using namespace Corona::Systems;

namespace fs = std::filesystem;

// 创建 Vulkan 纹理（占位符）
VkDescriptorSet create_browser_texture(int width, int height) {
    using namespace Corona::Systems;
    if (!g_vulkan_backend) return VK_NULL_HANDLE;

    VkDevice device = g_vulkan_backend->get_device();
    VkPhysicalDevice phys = g_vulkan_backend->get_physical_device();

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

    // Find memory type
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

    // Create ImageView
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

    // Create sampler
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

    // Register texture with ImGui to get descriptor set
    VkDescriptorSet descriptor = ImGui_ImplVulkan_AddTexture(sampler, image_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Store owned resources for later cleanup
    owned_images[descriptor] = {image, image_memory, image_view, sampler, (uint32_t)width, (uint32_t)height};

    return descriptor;
}

// 转换本地路径为URL
std::string convert_local_path_to_url(const std::string& local_path) {
    if (local_path.find("http://") == 0 || local_path.find("https://") == 0) {
        return local_path;
    }

    // 检查是否是绝对路径
    if (fs::path(local_path).is_absolute()) {
        // 转换为file:/// URL
        std::string url = "file://";
        for (char c : local_path) {
            if (c == '\\') {
                url += '/';
            } else if (c == ' ') {
                url += "%20";
            } else {
                url += c;
            }
        }
        return url;
    }

    return local_path;
}

std::string resolve_html_path_for_cef(const std::string& maybe_relative_path) {
    fs::path base;
#ifdef HTML_SOURCE_DIR
    base = fs::path(HTML_SOURCE_DIR);
#else
    base = fs::current_path();
#endif

    fs::path p = fs::path(maybe_relative_path);

    // Treat leading '/' or '\\' as "project-relative" in our app, not absolute.
    // (On Windows, "/foo" is relative to current drive; keeping it explicit avoids surprises.)
    if (!maybe_relative_path.empty() && (maybe_relative_path[0] == '/' || maybe_relative_path[0] == '\\')) {
        p = fs::path(maybe_relative_path.substr(1));
    }

    // If CEF is given a real file URL, keep it as-is.
    const auto lower = [](std::string s) {
        for (auto& ch : s) ch = (char)tolower((unsigned char)ch);
        return s;
    };
    std::string lp = lower(maybe_relative_path);
    if (lp.rfind("file://", 0) == 0 || lp.rfind("http://", 0) == 0 || lp.rfind("https://", 0) == 0) {
        return maybe_relative_path;
    }

    fs::path abs_path;
    if (p.is_absolute()) {
        abs_path = p;
    } else {
        abs_path = base / p;
    }

    // weakly_canonical handles paths that might not exist yet, or normalization
    // Note: weakly_canonical requires C++17
    abs_path = fs::weakly_canonical(abs_path);

    // Convert to a file:// URL that CEF understands.
    return std::string("file:///") + abs_path.generic_string();
}

// 创建新的浏览器标签页
int create_browser_tab(const std::string& url, const std::string& path) {
    BrowserTab* tab = new BrowserTab();

    int tab_id = ++tab_counter;

    tab->name = "Browser " + std::to_string(tab_id);

    // 转换本地路径为URL
    std::string full_url = convert_local_path_to_url(url);

    // 如果提供了 fragment，添加到 URL
    if (!path.empty()) {
        // 移除 fragment 开头的 #（如果有）
        std::string clean_fragment = path;
        if (clean_fragment.starts_with("#")) {
            clean_fragment = clean_fragment.substr(1);
        }

        // 确保 URL 不包含其他 fragment
        size_t hash_pos = full_url.find('#');
        if (hash_pos != std::string::npos) {
            full_url = full_url.substr(0, hash_pos);
        }

        // 添加 fragment
        full_url += "#" + clean_fragment;
    }
    std::cout << "Loading URL: " << full_url << std::endl;

    tab->url = full_url;
    strncpy(tab->url_buffer, full_url.c_str(), sizeof(tab->url_buffer) - 1);

    tab->client = new OffscreenCefClient();
    tab->client->SetTab(tab);

    // 创建 OpenGL 纹理
    tab->texture_id = create_browser_texture(tab->width, tab->height);

    // 创建离屏浏览器
    CefWindowInfo window_info;
    window_info.SetAsWindowless(GetDesktopWindow());

    CefBrowserSettings browser_settings;
    browser_settings.windowless_frame_rate = 60;
    // 启用JavaScript
    browser_settings.javascript = STATE_ENABLED;
    // 启用本地存储
    browser_settings.local_storage = STATE_ENABLED;
    // 启用WebGL
    browser_settings.webgl = STATE_ENABLED;

    CefBrowserHost::CreateBrowser(window_info, tab->client, full_url, browser_settings, nullptr, nullptr);
    tabs[tab_id] = tab;
    return tab_id;
}

// 更新浏览器纹理
void update_browser_texture(int tab_id) {
    using namespace Corona::Systems;
    BrowserTab* tab = tabs[tab_id];
    if (!g_vulkan_backend) return;
    if (!(tab->buffer_dirty && !tab->pixel_buffer.empty() && tab->texture_id != VK_NULL_HANDLE)) return;

    VkDevice device = g_vulkan_backend->get_device();
    VkPhysicalDevice phys = g_vulkan_backend->get_physical_device();
    VkQueue queue = g_vulkan_backend->get_queue();

    // Find the OwnedImage by descriptor
    // Find owned image by matching descriptor
    OwnedImage* found = nullptr;
    VkDescriptorSet desc = tab->texture_id;
    auto it = owned_images.find(desc);
    if (it != owned_images.end()) {
        found = &it->second;
    }
    if (!found) {
        tab->buffer_dirty = false;
        return;
    }

    // Create staging buffer
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

    // Copy pixel data
    void* mapped;
    vkMapMemory(device, stagingMemory, 0, imageSize, 0, &mapped);
    memcpy(mapped, tab->pixel_buffer.data(), imageSize);
    vkUnmapMemory(device, stagingMemory);

    // Create command buffer to copy buffer->image
    VkCommandPool cmdPool;
    VkCommandBuffer cmdBuf;
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = g_vulkan_backend->get_queue_family();
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
    region.imageExtent = {(uint32_t)tab->width, (uint32_t)tab->height, 1};

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

// 关闭浏览器标签页
void close_browser_tab(int tab_id) {
    if (tabs.find(tab_id) == tabs.end()) {
        return;
    }

    BrowserTab* tab = tabs[tab_id];
    if (tab->client && tab->client->GetBrowser()) {
        tab->client->GetBrowser()->GetHost()->CloseBrowser(true);
    }
    if (tab->texture_id != VK_NULL_HANDLE) {
        // Remove ImGui binding and destroy Vulkan resources
        ImGui_ImplVulkan_RemoveTexture(tab->texture_id);
        auto it = owned_images.find(tab->texture_id);
        if (it != owned_images.end()) {
            VkDevice device = g_vulkan_backend->get_device();
            vkDestroySampler(device, it->second.sampler, nullptr);
            vkDestroyImageView(device, it->second.view, nullptr);
            vkFreeMemory(device, it->second.memory, nullptr);
            vkDestroyImage(device, it->second.image, nullptr);
            owned_images.erase(it);
        }
        tab->texture_id = VK_NULL_HANDLE;
    }
    delete tab;
}
