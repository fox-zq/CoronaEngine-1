#include <corona/systems/ui/vulkan_backend.h>

#include <algorithm>
#include <mutex>

#include "cef/browser_manager.h"
#include "cef/cef_client.h"

namespace Corona::Systems::UI {
void BrowserManager::destroy_tab_texture(BrowserTab* tab) {
    if (!tab || tab->texture_id == -1) {
        return;
    }

    auto it = owned_images_.find(tab->texture_id);
    if (it != owned_images_.end()) {
        owned_images_.erase(it);
    }

    tab->texture_id = -1;
}

ImTextureID BrowserManager::create_browser_texture(int width, int height) {
    const uint32_t safe_width = static_cast<uint32_t>(std::max(width, 1));
    const uint32_t safe_height = static_cast<uint32_t>(std::max(height, 1));

    OwnedImage owned{};
    owned.image = HardwareImage(safe_width, safe_height, ImageFormat::RGBA8_SRGB, ImageUsage::SampledImage);
    if (!owned.image) {
        return -1;
    }

    owned.width = safe_width;
    owned.height = safe_height;

    const uint32_t descriptor = owned.image.storeDescriptor();
    const ImTextureID texture_id = static_cast<ImTextureID>(descriptor);

    owned_images_[texture_id] = std::move(owned);
    return texture_id;
}

void BrowserManager::update_texture(int tab_id) {
    auto it = tabs_.find(tab_id);
    if (it == tabs_.end()) {
        return;
    }

    BrowserTab* tab = it->second.get();

    std::vector<uint8_t> pixels;
    ImTextureID texture_id = -1;

    {
        std::unique_lock<std::mutex> lock(tab->mutex);
        if (!(tab->buffer_dirty && !tab->pixel_buffer.empty() && tab->texture_id != -1)) {
            return;
        }

        texture_id = tab->texture_id;
        pixels = tab->pixel_buffer;
    }

    auto image_it = owned_images_.find(texture_id);
    if (image_it == owned_images_.end()) {
        std::unique_lock<std::mutex> lock(tab->mutex);
        tab->buffer_dirty = false;
        return;
    }

    constexpr size_t rgba_bytes_per_pixel = 4;
    const size_t expected_size =
        static_cast<size_t>(image_it->second.width) *
        static_cast<size_t>(image_it->second.height) *
        rgba_bytes_per_pixel;

    if (pixels.size() >= expected_size) {
        texture_executor_ << image_it->second.image.copyFrom(pixels.data())
                          << texture_executor_.commit();
    }

    std::unique_lock<std::mutex> lock(tab->mutex);
    tab->buffer_dirty = false;
}

void BrowserManager::resize_tab(int tab_id, int width, int height) {
    auto it = tabs_.find(tab_id);
    if (it == tabs_.end()) {
        return;
    }

    BrowserTab* tab = it->second.get();
    if (width <= 0 || height <= 0) {
        return;
    }
    if (width == tab->width && height == tab->height) {
        return;
    }

    tab->width = width;
    tab->height = height;

    destroy_tab_texture(tab);
    tab->texture_id = create_browser_texture(tab->width, tab->height);

    if (tab->client) {
        tab->client->Resize(tab->width, tab->height);
    }
}
}  // namespace Corona::Systems::UI