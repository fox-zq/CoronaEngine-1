#pragma once

#include <SDL3/SDL.h>
#include <corona/events/imgui_system_events.h>
#include <corona/kernel/event/i_event_bus.h>
#include <corona/kernel/event/i_event_stream.h>
#include <corona/kernel/system/system_base.h>
#include <corona/systems/ui/vulkan_backend.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <windows.h>

#include <memory>

namespace Corona::Systems {

class VulkanBackend;

/**
 * @brief UI系统
 *
 * 负责启动和管理 ImGui 界面。
 * 运行在独立线程。
 */
class ImguiSystem : public Kernel::SystemBase {
   public:
    ImguiSystem()
        : event_{},
          show_demo_window_(false),
          running_(false),
          window_(nullptr) {
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

    void on_thread_started() override;

    void on_thread_stopped() override;

    /**
     * @brief 关闭显示系统
     *
     * 销毁窗口并清理UI资源
     */
    void shutdown() override;

   private:
    SDL_Event event_;
    bool show_demo_window_;
    bool running_;
    SDL_Window* window_;
    ImGuiIO* io_ = nullptr;

    bool window_size_changed_ = false;

    std::unique_ptr<VulkanBackend> vulkan_backend_;

    int active_tab_id_ = -1;  // 当前活动的标签页ID
};

}  // namespace Corona::Systems