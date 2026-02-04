#pragma once

#include <vector>
#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <imgui.h>

namespace Corona::Systems {

    class VulkanBackend {
    public:
        explicit VulkanBackend(SDL_Window* window);
        ~VulkanBackend();

        void initialize();
        void shutdown();

        // Check if swapchain needs rebuild
        bool is_swap_chain_rebuild() const { return swap_chain_rebuild_; }
        void set_swap_chain_rebuild(bool rebuild) { swap_chain_rebuild_ = rebuild; }

        void rebuild_swap_chain(int width, int height);

        void new_frame();
        void render_frame(ImDrawData* draw_data);
        void present_frame();

        // Getters for ImGui Init
        VkInstance get_instance() const { return instance_; }
        VkPhysicalDevice get_physical_device() const { return physical_device_; }
        VkDevice get_device() const { return device_; }
        uint32_t get_queue_family() const { return queue_family_; }
        VkQueue get_queue() const { return queue_; }
        VkDescriptorPool get_descriptor_pool() const { return descriptor_pool_; }
        VkRenderPass get_render_pass() const { return render_pass_; }
        uint32_t get_min_image_count() const { return min_image_count_; }
        uint32_t get_image_count() const { return image_count_; }
        VkSampleCountFlagBits get_msaa_samples() const { return VK_SAMPLE_COUNT_1_BIT; }

    private:
        void setup_vulkan(std::vector<const char*> instance_extensions);
        void setup_vulkan_window(VkSurfaceKHR surface, int width, int height);
        void create_vulkan_window_surface(VkSurfaceKHR surface, int width, int height);
        void cleanup_vulkan();
        void cleanup_vulkan_window();

        SDL_Window* window_ = nullptr;

        // Vulkan Data
        VkInstance               instance_ = VK_NULL_HANDLE;
        VkPhysicalDevice         physical_device_ = VK_NULL_HANDLE;
        VkDevice                 device_ = VK_NULL_HANDLE;
        uint32_t                 queue_family_ = (uint32_t)-1;
        VkQueue                  queue_ = VK_NULL_HANDLE;
        VkDebugReportCallbackEXT debug_report_ = VK_NULL_HANDLE;
        VkPipelineCache          pipeline_cache_ = VK_NULL_HANDLE;
        VkDescriptorPool         descriptor_pool_ = VK_NULL_HANDLE;

        VkSurfaceKHR             surface_ = VK_NULL_HANDLE;
        VkSwapchainKHR           swapchain_ = VK_NULL_HANDLE;
        VkRenderPass             render_pass_ = VK_NULL_HANDLE;
        VkCommandPool            command_pool_ = VK_NULL_HANDLE;

        // Static Helper Variables (Instance level for simplicity)
        VkFormat                 surface_format_ = VK_FORMAT_B8G8R8A8_UNORM;
        VkPresentModeKHR         present_mode_ = VK_PRESENT_MODE_FIFO_KHR;

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

        std::vector<FrameData>       frames_;
        std::vector<FrameSemaphores> frame_semaphores_;

        uint32_t                 min_image_count_ = 2;
        bool                     swap_chain_rebuild_ = false;
        uint32_t                 image_count_ = 0;
        uint32_t                 current_frame_index_ = 0;
    };

} // namespace Corona::Systems
