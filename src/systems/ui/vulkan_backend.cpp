#include <SDL3/SDL_vulkan.h>
#include <corona/systems/ui/vulkan_backend.h>
#include <imgui_impl_vulkan.h>
#include <volk.h>

#include <iostream>
#include <vector>

namespace Corona::Systems {

static void check_vk_result(VkResult err) {
    if (err == 0) return;
    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
    if (err < 0) abort();
}

VulkanBackend::VulkanBackend(SDL_Window* window) : window_(window) {}

VulkanBackend::~VulkanBackend() {
    if (device_ != VK_NULL_HANDLE) {
        shutdown();
    }
}

void VulkanBackend::initialize() {
    // 初始化 Volk
    if (volkInitialize() != VK_SUCCESS) {
        std::cerr << "Failed to initialize Volk\n";
        return;
    }

    // 获取 Vulkan 实例扩展
    uint32_t extensions_count = 0;
    char const* const* extensions_names = SDL_Vulkan_GetInstanceExtensions(&extensions_count);
    std::vector<const char*> extensions;
    if (extensions_names) {
        for (uint32_t i = 0; i < extensions_count; i++) {
            extensions.push_back(extensions_names[i]);
        }
    }

    setup_vulkan(extensions);

    // Create Surface
    VkSurfaceKHR surface;
    if (SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface) == 0) {
        std::cerr << "Failed to create Vulkan Surface\n";
        abort();
    }

    int w, h;
    SDL_GetWindowSize(window_, &w, &h);
    setup_vulkan_window(surface, w, h);
}

void VulkanBackend::shutdown() {
    VkResult err = vkDeviceWaitIdle(device_);
    check_vk_result(err);

    cleanup_vulkan_window();
    cleanup_vulkan();

    device_ = VK_NULL_HANDLE;  // Mark as destroyed
}

void VulkanBackend::rebuild_swap_chain(int width, int height) {
    if (width > 0 && height > 0) {
        ImGui_ImplVulkan_SetMinImageCount(min_image_count_);
        create_vulkan_window_surface(surface_, width, height);
        // Reset frame index to sync with new framebuffers/semaphores
        current_frame_index_ = 0;
        swap_chain_rebuild_ = false;
    }
}

void VulkanBackend::new_frame() {
    ImGui_ImplVulkan_NewFrame();
}

void VulkanBackend::setup_vulkan(std::vector<const char*> instance_extensions) {
    VkResult err;

    // Create Vulkan Instance
    {
        VkInstanceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;

        create_info.enabledExtensionCount = (uint32_t)instance_extensions.size();
        create_info.ppEnabledExtensionNames = instance_extensions.data();

#ifdef IMGUI_VULKAN_DEBUG_REPORT
        // Optional validation layers
        const char* layers[] = {"VK_LAYER_KHRONOS_validation"};
        create_info.enabledLayerCount = 1;
        create_info.ppEnabledLayerNames = layers;
        const char** extensions_ext = (const char**)malloc(sizeof(const char*) * (create_info.enabledExtensionCount + 1));
        memcpy(extensions_ext, create_info.ppEnabledExtensionNames, create_info.enabledExtensionCount * sizeof(const char*));
        extensions_ext[create_info.enabledExtensionCount] = "VK_EXT_debug_report";
        create_info.enabledExtensionCount++;
        create_info.ppEnabledExtensionNames = extensions_ext;
#endif

        err = vkCreateInstance(&create_info, nullptr, &instance_);
        check_vk_result(err);
        volkLoadInstance(instance_);

#ifdef IMGUI_VULKAN_DEBUG_REPORT
        free((void*)extensions_ext);
#endif
    }

    // Select Physical Device
    {
        uint32_t gpu_count;
        err = vkEnumeratePhysicalDevices(instance_, &gpu_count, nullptr);
        check_vk_result(err);

        if (gpu_count == 0) {
            std::cerr << "No Vulkan Physical Devices found.\n";
            abort();
        }

        std::vector<VkPhysicalDevice> gpus(gpu_count);
        err = vkEnumeratePhysicalDevices(instance_, &gpu_count, gpus.data());
        check_vk_result(err);

        physical_device_ = gpus[0];
    }

    // Select Graphics Queue Family
    {
        uint32_t count;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &count, nullptr);
        std::vector<VkQueueFamilyProperties> queues(count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device_, &count, queues.data());
        for (uint32_t i = 0; i < count; i++)
            if (queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                queue_family_ = i;
                break;
            }
        if (queue_family_ == (uint32_t)-1) {
            std::cerr << "No Graphics Queue Family found.\n";
            abort();
        }
    }

    // Create Logical Device
    {
        int device_extension_count = 1;
        const char* device_extensions[] = {"VK_KHR_swapchain"};
        const float queue_priority[] = {1.0f};
        VkDeviceQueueCreateInfo queue_info[1] = {};
        queue_info[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue_info[0].queueFamilyIndex = queue_family_;
        queue_info[0].queueCount = 1;
        queue_info[0].pQueuePriorities = queue_priority;
        VkDeviceCreateInfo create_info = {};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.queueCreateInfoCount = sizeof(queue_info) / sizeof(queue_info[0]);
        create_info.pQueueCreateInfos = queue_info;
        create_info.enabledExtensionCount = device_extension_count;
        create_info.ppEnabledExtensionNames = device_extensions;
        err = vkCreateDevice(physical_device_, &create_info, nullptr, &device_);
        check_vk_result(err);
        volkLoadDevice(device_);
        vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
    }

    // Create Descriptor Pool
    {
        VkDescriptorPoolSize pool_sizes[] =
            {
                {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
                {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
                {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
                {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
                {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
                {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
                {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
                {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};
        VkDescriptorPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
        pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
        pool_info.pPoolSizes = pool_sizes;
        err = vkCreateDescriptorPool(device_, &pool_info, nullptr, &descriptor_pool_);
        check_vk_result(err);
    }
}

void VulkanBackend::setup_vulkan_window(VkSurfaceKHR surface, int width, int height) {
    surface_ = surface;

    // 在 setup_vulkan_window() 函数中修改表面格式选择
    // 选择支持透明度的格式
    VkBool32 res;
    vkGetPhysicalDeviceSurfaceSupportKHR(physical_device_, queue_family_, surface_, &res);
    if (res != VK_TRUE) {
        fprintf(stderr, "Error no WSI support on physical device 0\n");
        exit(-1);
    }

    // 选择支持 Alpha 通道的表面格式
    uint32_t format_count;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, nullptr);
    std::vector<VkSurfaceFormatKHR> surface_formats(format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_, &format_count, surface_formats.data());

    // 优先选择支持 Alpha 的格式
    surface_format_ = VK_FORMAT_B8G8R8A8_UNORM;  // 确保使用带 Alpha 的格式
    for (const auto& fmt : surface_formats) {
        if (fmt.format == VK_FORMAT_B8G8R8A8_UNORM && fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surface_format_ = VK_FORMAT_B8G8R8A8_UNORM;
            break;
        }
    }

    // Select Present Mode
    // Default FIFO
    present_mode_ = VK_PRESENT_MODE_FIFO_KHR;
    // Check for Mailbox
    uint32_t present_mode_count;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &present_mode_count, nullptr);
    std::vector<VkPresentModeKHR> present_modes(present_mode_count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device_, surface_, &present_mode_count, present_modes.data());
    for (const auto& pm : present_modes) {
        if (pm == VK_PRESENT_MODE_MAILBOX_KHR) {
            present_mode_ = VK_PRESENT_MODE_MAILBOX_KHR;
            break;
        }
    }

    create_vulkan_window_surface(surface_, width, height);
}

void VulkanBackend::create_vulkan_window_surface(VkSurfaceKHR surface, int width, int height) {
    VkResult err;
    VkSwapchainKHR old_swapchain = swapchain_;
    VkSwapchainCreateInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = surface;
    info.minImageCount = min_image_count_;
    info.imageFormat = surface_format_;
    info.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    info.imageExtent.width = width;
    info.imageExtent.height = height;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;  // Should assume valid transform
    // info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = present_mode_;
    info.clipped = VK_TRUE;
    info.oldSwapchain = old_swapchain;

    // 查询表面能力以选择正确的复合Alpha模式
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface, &capabilities);

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

    err = vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_);
    check_vk_result(err);

    if (old_swapchain)
        vkDestroySwapchainKHR(device_, old_swapchain, nullptr);

    vkGetSwapchainImagesKHR(device_, swapchain_, &image_count_, nullptr);
    std::vector<VkImage> backbuffers(image_count_);
    vkGetSwapchainImagesKHR(device_, swapchain_, &image_count_, backbuffers.data());

    frames_.resize(image_count_);
    frame_semaphores_.resize(image_count_);

    for (uint32_t i = 0; i < image_count_; i++) {
        // Create Command Buffer / Pool if not exists
        if (frames_[i].CommandPool == VK_NULL_HANDLE) {
            VkCommandPoolCreateInfo cmd_pool_info = {};
            cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
            cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            cmd_pool_info.queueFamilyIndex = queue_family_;
            err = vkCreateCommandPool(device_, &cmd_pool_info, nullptr, &frames_[i].CommandPool);
            check_vk_result(err);

            VkCommandBufferAllocateInfo alloc_info = {};
            alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            alloc_info.commandPool = frames_[i].CommandPool;
            alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            alloc_info.commandBufferCount = 1;
            err = vkAllocateCommandBuffers(device_, &alloc_info, &frames_[i].CommandBuffer);
            check_vk_result(err);

            VkFenceCreateInfo fence_info = {};
            fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            err = vkCreateFence(device_, &fence_info, nullptr, &frames_[i].Fence);
            check_vk_result(err);

            VkSemaphoreCreateInfo sem_info = {};
            sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            err = vkCreateSemaphore(device_, &sem_info, nullptr, &frame_semaphores_[i].ImageAcquiredSemaphore);
            check_vk_result(err);
            err = vkCreateSemaphore(device_, &sem_info, nullptr, &frame_semaphores_[i].RenderCompleteSemaphore);
            check_vk_result(err);
        }

        // Create ImageView
        frames_[i].Backbuffer = backbuffers[i];
        VkImageViewCreateInfo view_info = {};
        view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = surface_format_;
        view_info.components.r = VK_COMPONENT_SWIZZLE_R;
        view_info.components.g = VK_COMPONENT_SWIZZLE_G;
        view_info.components.b = VK_COMPONENT_SWIZZLE_B;
        view_info.components.a = VK_COMPONENT_SWIZZLE_A;
        VkImageSubresourceRange m_ImageRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        view_info.subresourceRange = m_ImageRange;
        view_info.image = frames_[i].Backbuffer;
        err = vkCreateImageView(device_, &view_info, nullptr, &frames_[i].BackbufferView);
        check_vk_result(err);
    }

    // Create RenderPass
    if (render_pass_ == VK_NULL_HANDLE) {
        // 修改为支持透明度混合的渲染通道
        VkAttachmentDescription attachment = {};
        attachment.format = surface_format_;
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

        VkRenderPassCreateInfo render_pass_info = {};
        render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_info.attachmentCount = 1;
        render_pass_info.pAttachments = &attachment;
        render_pass_info.subpassCount = 1;
        render_pass_info.pSubpasses = &subpass;
        render_pass_info.dependencyCount = 1;
        render_pass_info.pDependencies = &dependency;

        err = vkCreateRenderPass(device_, &render_pass_info, nullptr, &render_pass_);
        check_vk_result(err);
    }

    // Create Framebuffer
    for (uint32_t i = 0; i < image_count_; i++) {
        VkImageView attachment[1];
        attachment[0] = frames_[i].BackbufferView;
        VkFramebufferCreateInfo framebuffer_info = {};
        framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer_info.renderPass = render_pass_;
        framebuffer_info.attachmentCount = 1;
        framebuffer_info.pAttachments = attachment;
        framebuffer_info.width = width;
        framebuffer_info.height = height;
        framebuffer_info.layers = 1;
        err = vkCreateFramebuffer(device_, &framebuffer_info, nullptr, &frames_[i].Framebuffer);
        check_vk_result(err);
    }
}

void VulkanBackend::cleanup_vulkan() {
    vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
    vkDestroyDevice(device_, nullptr);
    vkDestroyInstance(instance_, nullptr);
}

void VulkanBackend::cleanup_vulkan_window() {
    vkDeviceWaitIdle(device_);
    for (uint32_t i = 0; i < image_count_; i++) {
        vkDestroyImageView(device_, frames_[i].BackbufferView, nullptr);
        vkDestroyFramebuffer(device_, frames_[i].Framebuffer, nullptr);
    }
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
}

void VulkanBackend::render_frame(ImDrawData* draw_data) {
    VkResult err;
    VkSemaphore image_acquired_semaphore = frame_semaphores_[current_frame_index_].ImageAcquiredSemaphore;
    VkSemaphore render_complete_semaphore = frame_semaphores_[current_frame_index_].RenderCompleteSemaphore;

    uint32_t image_index;
    err = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, image_acquired_semaphore, VK_NULL_HANDLE, &image_index);

    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
        swap_chain_rebuild_ = true;
        return;
    }
    check_vk_result(err);

    current_frame_index_ = image_index;

    FrameData* frame = &frames_[current_frame_index_];
    err = vkWaitForFences(device_, 1, &frame->Fence, VK_TRUE, UINT64_MAX);
    check_vk_result(err);
    err = vkResetFences(device_, 1, &frame->Fence);
    check_vk_result(err);

    err = vkResetCommandPool(device_, frame->CommandPool, 0);
    check_vk_result(err);
    VkCommandBufferBeginInfo info = {};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    err = vkBeginCommandBuffer(frame->CommandBuffer, &info);
    check_vk_result(err);

    {
        VkRenderPassBeginInfo render_pass_begin_info = {};
        render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_begin_info.renderPass = render_pass_;
        render_pass_begin_info.framebuffer = frame->Framebuffer;

        // Get size from Image? Or we should store RenderArea?
        // Re-using ImGui DisplaySize is convenient but might be slightly off?
        // In SetupVulkanWindow we passed width/height.
        // Let's use ImDrawData display size.
        render_pass_begin_info.renderArea.extent.width = (uint32_t)draw_data->DisplaySize.x;
        render_pass_begin_info.renderArea.extent.height = (uint32_t)draw_data->DisplaySize.y;
        render_pass_begin_info.renderArea.offset = {0, 0};
        VkClearValue clearColor = {0.0f, 0.0f, 0.0f, 0.0f};
        render_pass_begin_info.clearValueCount = 1;
        render_pass_begin_info.pClearValues = &clearColor;
        vkCmdBeginRenderPass(frame->CommandBuffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
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

    err = vkQueueSubmit(queue_, 1, &submit_info, frame->Fence);
    check_vk_result(err);
}

void VulkanBackend::present_frame() {
    if (swap_chain_rebuild_) return;
    VkSemaphore render_complete_semaphore = frame_semaphores_[current_frame_index_].RenderCompleteSemaphore;
    VkPresentInfoKHR info = {};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &render_complete_semaphore;
    info.swapchainCount = 1;
    info.pSwapchains = &swapchain_;
    info.pImageIndices = &current_frame_index_;
    VkResult err = vkQueuePresentKHR(queue_, &info);
    if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR) {
        swap_chain_rebuild_ = true;
        return;
    }
    check_vk_result(err);
}

}  // namespace Corona::Systems
