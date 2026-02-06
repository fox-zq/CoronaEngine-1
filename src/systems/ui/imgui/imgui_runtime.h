#pragma once

#include <SDL3/SDL.h>
#include <imgui.h>

#include <memory>

namespace Corona::Systems {
class VulkanBackend;
}

namespace Corona::Systems::UI {

bool initialize_sdl_imgui(SDL_Window*& window, ImGuiIO*& io, std::unique_ptr<VulkanBackend>& vulkan_backend);
void shutdown_sdl_imgui(SDL_Window*& window, ImGuiIO*& io, std::unique_ptr<VulkanBackend>& vulkan_backend);

}  // namespace Corona::Systems::UI
