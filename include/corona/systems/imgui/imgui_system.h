#pragma once

#include <corona/events/imgui_system_events.h>
#include <corona/kernel/event/i_event_bus.h>
#include <corona/kernel/event/i_event_stream.h>
#include <corona/kernel/system/system_base.h>
#include <corona/systems/imgui/vulkan_backend.h>

#include <memory>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

// CEF headers
#include <cef_app.h>
#include <cef_client.h>
#include <cef_browser.h>
#include <cef_render_handler.h>
#include <cef_scheme.h>
#include <wrapper/cef_helpers.h>
#include <cef_v8.h>
#include <windows.h>

#include <wrapper/cef_message_router.h>

namespace Corona::Systems {

/**
 * @brief UI系统
 *
 * 负责启动和管理 ImGui 界面。
 * 运行在独立线程。
 */
class ImguiSystem : public Kernel::SystemBase {
   public:
    ImguiSystem() {
        set_target_fps(60);  // 几何系统运行在 60 FPS
    }

    ~ImguiSystem() override = default;

    // ========================================
    // ISystem 接口实现
    // ========================================

    std::string_view get_name() const override {
        return "Imgui";
    }

    int get_priority() const override {
        return 40;  // 最高优先级，最先初始化
    }

    /**
     * @brief 初始化UI系统
     * @param ctx 系统上下文
     * @return 初始化成功返回 true
     */
    bool initialize(Kernel::ISystemContext* ctx) override;

    /**
     * @brief 每帧更新UI
     *
     * 在独立线程中调用，处理窗口事件和输入
     */
    void update() override;

    // Run the system thread loop; override to perform graphics initialization
    // on the system thread so SDL event handling runs on the same thread
    void thread_loop() override;

    /**
     * @brief 关闭显示系统
     *
     * 销毁窗口并清理UI资源
     */
    void shutdown() override;

   private:
        SDL_Event event;
        bool showDemoWindow;
        bool running;
        SDL_Window* window;
        ImGuiIO *io = nullptr;

        std::unique_ptr<VulkanBackend> m_VulkanBackend;
};

}  // namespace Corona::Systems

