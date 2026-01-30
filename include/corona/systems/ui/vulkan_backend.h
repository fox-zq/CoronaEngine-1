#pragma once

#include <vector>
#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <imgui.h>

namespace Corona::Systems {

    class VulkanBackend {
    public:
        VulkanBackend(SDL_Window* window);
        ~VulkanBackend();

        void Initialize(std::vector<const char*> instance_extensions);
        void Shutdown();

        // Check if swapchain needs rebuild
        bool IsSwapChainRebuild() const { return m_SwapChainRebuild; }
        void SetSwapChainRebuild(bool rebuild) { m_SwapChainRebuild = rebuild; }

        void RebuildSwapChain(int width, int height);

        void NewFrame();
        void RenderFrame(ImDrawData* draw_data);
        void PresentFrame();

        // Getters for ImGui Init
        VkInstance GetInstance() const { return g_Instance; }
        VkPhysicalDevice GetPhysicalDevice() const { return g_PhysicalDevice; }
        VkDevice GetDevice() const { return g_Device; }
        uint32_t GetQueueFamily() const { return g_QueueFamily; }
        VkQueue GetQueue() const { return g_Queue; }
        VkDescriptorPool GetDescriptorPool() const { return g_DescriptorPool; }
        VkRenderPass GetRenderPass() const { return g_RenderPass; }
        uint32_t GetMinImageCount() const { return g_MinImageCount; }
        uint32_t GetImageCount() const { return g_ImageCount; }
        VkSampleCountFlagBits GetMSAASamples() const { return VK_SAMPLE_COUNT_1_BIT; }

    private:
        void SetupVulkan(std::vector<const char*> instance_extensions);
        void SetupVulkanWindow(VkSurfaceKHR surface, int width, int height);
        void CreateVulkanWindowSurface(VkSurfaceKHR surface, int width, int height);
        void CleanupVulkan();
        void CleanupVulkanWindow();

        SDL_Window* m_Window = nullptr;

        // Vulkan Data
        VkInstance               g_Instance = VK_NULL_HANDLE;
        VkPhysicalDevice         g_PhysicalDevice = VK_NULL_HANDLE;
        VkDevice                 g_Device = VK_NULL_HANDLE;
        uint32_t                 g_QueueFamily = (uint32_t)-1;
        VkQueue                  g_Queue = VK_NULL_HANDLE;
        VkDebugReportCallbackEXT g_DebugReport = VK_NULL_HANDLE;
        VkPipelineCache          g_PipelineCache = VK_NULL_HANDLE;
        VkDescriptorPool         g_DescriptorPool = VK_NULL_HANDLE;

        VkSurfaceKHR             g_Surface = VK_NULL_HANDLE;
        VkSwapchainKHR           g_Swapchain = VK_NULL_HANDLE;
        VkRenderPass             g_RenderPass = VK_NULL_HANDLE;
        VkCommandPool            g_CommandPool = VK_NULL_HANDLE;

        // Static Helper Variables (Instance level for simplicity)
        VkFormat                 g_SurfaceFormat = VK_FORMAT_B8G8R8A8_UNORM;
        VkPresentModeKHR         g_PresentMode = VK_PRESENT_MODE_FIFO_KHR;

        // Swapchain Resources
        struct FrameData {
            VkCommandPool       CommandPool;
            VkCommandBuffer     CommandBuffer;
            VkFence             Fence;
            VkImage             Backbuffer;
            VkImageView         BackbufferView;
            VkFramebuffer       Framebuffer;
        };
        struct FrameSemaphores {
            VkSemaphore         ImageAcquiredSemaphore;
            VkSemaphore         RenderCompleteSemaphore;
        };

        std::vector<FrameData>       g_Frames;
        std::vector<FrameSemaphores> g_FrameSemaphores;

        uint32_t                 g_MinImageCount = 2;
        bool                     m_SwapChainRebuild = false;
        uint32_t                 g_ImageCount = 0;
        uint32_t                 g_CurrentFrameIndex = 0;
    };

} // namespace Corona::Systems
