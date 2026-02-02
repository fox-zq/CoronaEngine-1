#include <corona/systems/ui/vulkan_backend.h>
#include <iostream>
#include <vector>
#include <cstring>
#include <volk.h>
#include <SDL3/SDL_vulkan.h>
#include <imgui_impl_vulkan.h>

namespace Corona::Systems {

    static void check_vk_result(VkResult err)
    {
        if (err == 0) return;
        fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
        if (err < 0) abort();
    }

    VulkanBackend::VulkanBackend(SDL_Window* window) : m_Window(window) {}

    VulkanBackend::~VulkanBackend() {
        if (g_Device != VK_NULL_HANDLE) {
            Shutdown();
        }
    }

    void VulkanBackend::Initialize(std::vector<const char*> instance_extensions) {
        SetupVulkan(instance_extensions);

        // Create Surface
        VkSurfaceKHR surface;
        if (SDL_Vulkan_CreateSurface(m_Window, g_Instance, nullptr, &surface) == 0)
        {
             std::cerr << "Failed to create Vulkan Surface\n";
             abort();
        }

        int w, h;
        SDL_GetWindowSize(m_Window, &w, &h);
        SetupVulkanWindow(surface, w, h);
    }

    void VulkanBackend::Shutdown() {
        VkResult err = vkDeviceWaitIdle(g_Device);
        check_vk_result(err);

        CleanupVulkanWindow();
        CleanupVulkan();

        g_Device = VK_NULL_HANDLE; // Mark as destroyed
    }

    void VulkanBackend::RebuildSwapChain(int width, int height) {
        if (width > 0 && height > 0)
        {
            ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
            CreateVulkanWindowSurface(g_Surface, width, height);
            // Reset frame index to sync with new framebuffers/semaphores
            g_CurrentFrameIndex = 0;
            m_SwapChainRebuild = false;
        }
    }

    void VulkanBackend::NewFrame() {
        ImGui_ImplVulkan_NewFrame();
    }

    void VulkanBackend::SetupVulkan(std::vector<const char*> instance_extensions)
    {
        VkResult err;

        // Create Vulkan Instance
        {
            VkInstanceCreateInfo create_info = {};
            create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

            create_info.enabledExtensionCount = (uint32_t)instance_extensions.size();
            create_info.ppEnabledExtensionNames = instance_extensions.data();

#ifdef IMGUI_VULKAN_DEBUG_REPORT
            // Optional validation layers
            const char* layers[] = { "VK_LAYER_KHRONOS_validation" };
            create_info.enabledLayerCount = 1;
            create_info.ppEnabledLayerNames = layers;
            const char** extensions_ext = (const char**)malloc(sizeof(const char*) * (create_info.enabledExtensionCount + 1));
            memcpy(extensions_ext, create_info.ppEnabledExtensionNames, create_info.enabledExtensionCount * sizeof(const char*));
            extensions_ext[create_info.enabledExtensionCount] = "VK_EXT_debug_report";
            create_info.enabledExtensionCount++;
            create_info.ppEnabledExtensionNames = extensions_ext;
#endif

            err = vkCreateInstance(&create_info, nullptr, &g_Instance);
            check_vk_result(err);
            volkLoadInstance(g_Instance);

#ifdef IMGUI_VULKAN_DEBUG_REPORT
            free((void*)extensions_ext);
#endif
        }

        // Select Physical Device
        {
            uint32_t gpu_count;
            err = vkEnumeratePhysicalDevices(g_Instance, &gpu_count, nullptr);
            check_vk_result(err);

            if (gpu_count == 0) {
                std::cerr << "No Vulkan Physical Devices found.\n";
                abort();
            }

            std::vector<VkPhysicalDevice> gpus(gpu_count);
            err = vkEnumeratePhysicalDevices(g_Instance, &gpu_count, gpus.data());
            check_vk_result(err);

            g_PhysicalDevice = gpus[0];
        }

        // Select Graphics Queue Family
        {
            uint32_t count;
            vkGetPhysicalDeviceQueueFamilyProperties(g_PhysicalDevice, &count, nullptr);
            std::vector<VkQueueFamilyProperties> queues(count);
            vkGetPhysicalDeviceQueueFamilyProperties(g_PhysicalDevice, &count, queues.data());
            for (uint32_t i = 0; i < count; i++)
                if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                {
                    g_QueueFamily = i;
                    break;
                }
            if (g_QueueFamily == (uint32_t)-1) {
                std::cerr << "No Graphics Queue Family found.\n";
                abort();
            }
        }

        // Create Logical Device
        {
            int device_extension_count = 1;
            const char* device_extensions[] = { "VK_KHR_swapchain" };
            const float queue_priority[] = { 1.0f };
            VkDeviceQueueCreateInfo queue_info[1] = {};
            queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queue_info[0].queueFamilyIndex = g_QueueFamily;
            queue_info[0].queueCount = 1;
            queue_info[0].pQueuePriorities = queue_priority;
            VkDeviceCreateInfo create_info = {};
            create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            create_info.queueCreateInfoCount = sizeof(queue_info) / sizeof(queue_info[0]);
            create_info.pQueueCreateInfos = queue_info;
            create_info.enabledExtensionCount = device_extension_count;
            create_info.ppEnabledExtensionNames = device_extensions;
            err = vkCreateDevice(g_PhysicalDevice, &create_info, nullptr, &g_Device);
            check_vk_result(err);
            volkLoadDevice(g_Device);
            vkGetDeviceQueue(g_Device, g_QueueFamily, 0, &g_Queue);
        }

        // Create Descriptor Pool
        {
            VkDescriptorPoolSize pool_sizes[] =
            {
                { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
                { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
                { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
                { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
                { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
                { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
                { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
                { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
                { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
                { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
            };
            VkDescriptorPoolCreateInfo pool_info = {};
            pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
            pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
            pool_info.pPoolSizes = pool_sizes;
            err = vkCreateDescriptorPool(g_Device, &pool_info, nullptr, &g_DescriptorPool);
            check_vk_result(err);
        }
    }

    void VulkanBackend::SetupVulkanWindow(VkSurfaceKHR surface, int width, int height)
    {
        g_Surface = surface;

        // 在 SetupVulkanWindow() 函数中修改表面格式选择
        // 选择支持透明度的格式
        VkBool32 res;
        vkGetPhysicalDeviceSurfaceSupportKHR(g_PhysicalDevice, g_QueueFamily, g_Surface, &res);
        if (res != VK_TRUE) {
            fprintf(stderr, "Error no WSI support on physical device 0\n");
            exit(-1);
        }

        // 选择支持 Alpha 通道的表面格式
        uint32_t format_count;
        vkGetPhysicalDeviceSurfaceFormatsKHR(g_PhysicalDevice, g_Surface, &format_count, nullptr);
        std::vector<VkSurfaceFormatKHR> surface_formats(format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(g_PhysicalDevice, g_Surface, &format_count, surface_formats.data());

        // 优先选择支持 Alpha 的格式
        g_SurfaceFormat = VK_FORMAT_B8G8R8A8_UNORM;  // 确保使用带 Alpha 的格式
        for (const auto& fmt : surface_formats) {
            if (fmt.format == VK_FORMAT_B8G8R8A8_UNORM && fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                g_SurfaceFormat = VK_FORMAT_B8G8R8A8_UNORM;
                break;
            }
        }

        // Select Present Mode
        // Default FIFO
        g_PresentMode = VK_PRESENT_MODE_FIFO_KHR;
        // Check for Mailbox
        uint32_t present_mode_count;
        vkGetPhysicalDeviceSurfacePresentModesKHR(g_PhysicalDevice, g_Surface, &present_mode_count, nullptr);
        std::vector<VkPresentModeKHR> present_modes(present_mode_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(g_PhysicalDevice, g_Surface, &present_mode_count, present_modes.data());
        for (const auto& pm : present_modes) {
            if (pm == VK_PRESENT_MODE_MAILBOX_KHR) {
                g_PresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
                break;
            }
        }

        CreateVulkanWindowSurface(g_Surface, width, height);
    }

    void VulkanBackend::CreateVulkanWindowSurface(VkSurfaceKHR surface, int width, int height)
    {
        VkResult err;
        VkSwapchainKHR old_swapchain = g_Swapchain;
        VkSwapchainCreateInfoKHR info = {};
        info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        info.surface = surface;
        info.minImageCount = g_MinImageCount;
        info.imageFormat = g_SurfaceFormat;
        info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        info.imageExtent.width = width;
        info.imageExtent.height = height;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR; // Should assume valid transform
        //info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode = g_PresentMode;
        info.clipped = VK_TRUE;
        info.oldSwapchain = old_swapchain;

        // 查询表面能力以选择正确的复合Alpha模式
        VkSurfaceCapabilitiesKHR capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(g_PhysicalDevice, surface, &capabilities);

        // 优先选择支持预乘Alpha的模式
        VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        if (capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR) {
            compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
        } else if (capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR) {
            compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
        } else if (capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR) {
            compositeAlpha = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        }

        info.compositeAlpha = compositeAlpha;  // 使用支持Alpha的复合模式

        err = vkCreateSwapchainKHR(g_Device, &info, nullptr, &g_Swapchain);
        check_vk_result(err);

        if (old_swapchain)
              vkDestroySwapchainKHR(g_Device, old_swapchain, nullptr);

        vkGetSwapchainImagesKHR(g_Device, g_Swapchain, &g_ImageCount, nullptr);
        std::vector<VkImage> backbuffers(g_ImageCount);
        vkGetSwapchainImagesKHR(g_Device, g_Swapchain, &g_ImageCount, backbuffers.data());

        g_Frames.resize(g_ImageCount);
        g_FrameSemaphores.resize(g_ImageCount);

        for (uint32_t i = 0; i < g_ImageCount; i++)
        {
             // Create Command Buffer / Pool if not exists
             if (g_Frames[i].CommandPool == VK_NULL_HANDLE)
             {
                  VkCommandPoolCreateInfo cmd_pool_info = {};
                  cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
                  cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
                  cmd_pool_info.queueFamilyIndex = g_QueueFamily;
                  err = vkCreateCommandPool(g_Device, &cmd_pool_info, nullptr, &g_Frames[i].CommandPool);
                  check_vk_result(err);

                  VkCommandBufferAllocateInfo alloc_info = {};
                  alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                  alloc_info.commandPool = g_Frames[i].CommandPool;
                  alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                  alloc_info.commandBufferCount = 1;
                  err = vkAllocateCommandBuffers(g_Device, &alloc_info, &g_Frames[i].CommandBuffer);
                  check_vk_result(err);

                  VkFenceCreateInfo fence_info = {};
                  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
                  fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
                  err = vkCreateFence(g_Device, &fence_info, nullptr, &g_Frames[i].Fence);
                  check_vk_result(err);

                  VkSemaphoreCreateInfo sem_info = {};
                  sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
                  err = vkCreateSemaphore(g_Device, &sem_info, nullptr, &g_FrameSemaphores[i].ImageAcquiredSemaphore);
                  check_vk_result(err);
                  err = vkCreateSemaphore(g_Device, &sem_info, nullptr, &g_FrameSemaphores[i].RenderCompleteSemaphore);
                  check_vk_result(err);
             }

             // Create ImageView
             g_Frames[i].Backbuffer = backbuffers[i];
             VkImageViewCreateInfo view_info = {};
             view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
             view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
             view_info.format = g_SurfaceFormat;
             view_info.components.r = VK_COMPONENT_SWIZZLE_R;
             view_info.components.g = VK_COMPONENT_SWIZZLE_G;
             view_info.components.b = VK_COMPONENT_SWIZZLE_B;
             view_info.components.a = VK_COMPONENT_SWIZZLE_A;
             VkImageSubresourceRange m_ImageRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
             view_info.subresourceRange = m_ImageRange;
             view_info.image = g_Frames[i].Backbuffer;
             err = vkCreateImageView(g_Device, &view_info, nullptr, &g_Frames[i].BackbufferView);
             check_vk_result(err);
        }

        // Create RenderPass
        if (g_RenderPass == VK_NULL_HANDLE)
        {
            // 修改为支持透明度混合的渲染通道
            VkAttachmentDescription attachment = {};
            attachment.format = g_SurfaceFormat;
            attachment.samples = VK_SAMPLE_COUNT_1_BIT;
            attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            VkAttachmentReference color_attachment = {};
            color_attachment.attachment = 0;
            color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            VkSubpassDescription subpass = {};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 1;
            subpass.pColorAttachments = &color_attachment;

            // 简单的Alpha混合（ImGui标准设置）
            VkSubpassDependency dependency = {};
            dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
            dependency.dstSubpass = 0;
            dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.srcAccessMask = 0;
            dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

            VkRenderPassCreateInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            info.attachmentCount = 1;
            info.pAttachments = &attachment;
            info.subpassCount = 1;
            info.pSubpasses = &subpass;
            info.dependencyCount = 1;
            info.pDependencies = &dependency;

            err = vkCreateRenderPass(g_Device, &info, nullptr, &g_RenderPass);
            check_vk_result(err);
        }

        // Create Framebuffer
        for (uint32_t i = 0; i < g_ImageCount; i++)
        {
             VkImageView attachment[1];
             attachment[0] = g_Frames[i].BackbufferView;
             VkFramebufferCreateInfo info = {};
             info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
             info.renderPass = g_RenderPass;
             info.attachmentCount = 1;
             info.pAttachments = attachment;
             info.width = width;
             info.height = height;
             info.layers = 1;
             err = vkCreateFramebuffer(g_Device, &info, nullptr, &g_Frames[i].Framebuffer);
             check_vk_result(err);
        }
    }

    void VulkanBackend::CleanupVulkan()
    {
        vkDestroyDescriptorPool(g_Device, g_DescriptorPool, nullptr);
        vkDestroyDevice(g_Device, nullptr);
        vkDestroyInstance(g_Instance, nullptr);
    }

    void VulkanBackend::CleanupVulkanWindow()
    {
         vkDeviceWaitIdle(g_Device);
         for (uint32_t i = 0; i < g_ImageCount; i++)
         {
              vkDestroyImageView(g_Device, g_Frames[i].BackbufferView, nullptr);
              vkDestroyFramebuffer(g_Device, g_Frames[i].Framebuffer, nullptr);
         }
         vkDestroySwapchainKHR(g_Device, g_Swapchain, nullptr);
    }

    void VulkanBackend::RenderFrame(ImDrawData* draw_data)
    {
        VkResult err;
        VkSemaphore image_acquired_semaphore  = g_FrameSemaphores[g_CurrentFrameIndex].ImageAcquiredSemaphore;
        VkSemaphore render_complete_semaphore = g_FrameSemaphores[g_CurrentFrameIndex].RenderCompleteSemaphore;

        uint32_t image_index;
        err = vkAcquireNextImageKHR(g_Device, g_Swapchain, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &image_index);

        if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
             m_SwapChainRebuild = true;
             return;
        }
        check_vk_result(err);

        g_CurrentFrameIndex = image_index;

        FrameData* frame = &g_Frames[g_CurrentFrameIndex];
        err = vkWaitForFences(g_Device, 1, &frame->Fence, VK_TRUE, UINT64_MAX);
        check_vk_result(err);
        err = vkResetFences(g_Device, 1, &frame->Fence);
        check_vk_result(err);

        err = vkResetCommandPool(g_Device, frame->CommandPool, 0);
        check_vk_result(err);
        VkCommandBufferBeginInfo info = {};
        info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        err = vkBeginCommandBuffer(frame->CommandBuffer, &info);
        check_vk_result(err);

        {
            VkRenderPassBeginInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            info.renderPass = g_RenderPass;
            info.framebuffer = frame->Framebuffer;
            VkExtent2D swapChainExtent = { (uint32_t)g_Frames[g_CurrentFrameIndex].BackbufferView, 0 }; // wait, BackbufferView is view.

            // Get size from Image? Or we should store RenderArea?
            // Re-using ImGui DisplaySize is convenient but might be slightly off?
            // In SetupVulkanWindow we passed width/height.
            // Let's use ImDrawData display size.
            info.renderArea.extent.width = (uint32_t)draw_data->DisplaySize.x;
            info.renderArea.extent.height = (uint32_t)draw_data->DisplaySize.y;
            info.renderArea.offset = { 0, 0 };
            VkClearValue clearColor = {0.0f, 0.0f, 0.0f, 0.0f};
            info.clearValueCount = 1;
            info.pClearValues = &clearColor;
            vkCmdBeginRenderPass(frame->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
        }

        // Record ImGui Draw Data
        ImGui_ImplVulkan_RenderDrawData(draw_data, frame->CommandBuffer);

        // Submit
        vkCmdEndRenderPass(frame->CommandBuffer);

        err = vkEndCommandBuffer(frame->CommandBuffer);
        check_vk_result(err);

        VkSubmitInfo submit_info = {};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.waitSemaphoreCount = 1;
        submit_info.pWaitSemaphores = &image_acquired_semaphore;
        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        submit_info.pWaitDstStageMask = &wait_stage;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &frame->CommandBuffer;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &render_complete_semaphore;

        err = vkQueueSubmit(g_Queue, 1, &submit_info, frame->Fence);
        check_vk_result(err);
    }

    void VulkanBackend::PresentFrame()
    {
         if (m_SwapChainRebuild) return;
         VkSemaphore render_complete_semaphore = g_FrameSemaphores[g_CurrentFrameIndex].RenderCompleteSemaphore;
         VkPresentInfoKHR info = {};
         info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
         info.waitSemaphoreCount = 1;
         info.pWaitSemaphores = &render_complete_semaphore;
         info.swapchainCount = 1;
         info.pSwapchains = &g_Swapchain;
         info.pImageIndices = &g_CurrentFrameIndex;
         VkResult err = vkQueuePresentKHR(g_Queue, &info);
         if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
              m_SwapChainRebuild = true;
              return;
         }
         check_vk_result(err);
    }

} // namespace Corona::Systems
