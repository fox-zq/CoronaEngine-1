#include "imgui_runtime.h"

#include <SDL3/SDL_vulkan.h>
#include <corona/kernel/core/i_logger.h>
#include <corona/systems/ui/vulkan_backend.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include "../cef/browser_manager.h"

namespace Corona::Systems::UI {

bool initialize_sdl_imgui(SDL_Window*& window, ImGuiIO*& io, std::unique_ptr<VulkanBackend>& vulkan_backend) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        CFW_LOG_ERROR("Failed to initialize SDL: {}", SDL_GetError());
        return false;
    }

    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
    window = SDL_CreateWindow("Corona Engine (Vulkan)", 1920, 1080,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (window == nullptr) {
        CFW_LOG_ERROR("Failed to create window: {}", SDL_GetError());
        SDL_Quit();
        return false;
    }

    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);
    SDL_StartTextInput(window);
    SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, "1");

    vulkan_backend = std::make_unique<VulkanBackend>(window);
    vulkan_backend->initialize();

    BrowserManager::instance().set_vulkan_backend(vulkan_backend.get());

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    io = &ImGui::GetIO();
    io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io->ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImGuiCol_DockingPreview] = ImVec4(0.2f, 0.2f, 0.8f, 0.3f);
    style.WindowRounding = 1.0f;
    style.WindowBorderSize = 1.0f;
    style.WindowPadding = ImVec2(1.0f, 1.0f);

    ImGui_ImplSDL3_InitForVulkan(window);

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = vulkan_backend->get_instance();
    init_info.PhysicalDevice = vulkan_backend->get_physical_device();
    init_info.Device = vulkan_backend->get_device();
    init_info.QueueFamily = vulkan_backend->get_queue_family();
    init_info.Queue = vulkan_backend->get_queue();
    init_info.DescriptorPool = vulkan_backend->get_descriptor_pool();
    init_info.PipelineInfoMain.RenderPass = vulkan_backend->get_render_pass();
    init_info.MinImageCount = vulkan_backend->get_min_image_count();
    init_info.ImageCount = vulkan_backend->get_image_count();
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init_info.UseDynamicRendering = false;

    ImGui_ImplVulkan_Init(&init_info);

    return true;
}

void shutdown_sdl_imgui(SDL_Window*& window, ImGuiIO*& io, std::unique_ptr<VulkanBackend>& vulkan_backend) {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (vulkan_backend) {
        vulkan_backend->shutdown();
        vulkan_backend.reset();
        BrowserManager::instance().set_vulkan_backend(nullptr);
    }

    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }

    SDL_StopTextInput(window);
    SDL_Quit();

    io = nullptr;
}

}  // namespace Corona::Systems::UI
