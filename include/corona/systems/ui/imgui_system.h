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
          window_(nullptr),
          mouse_drag_start_(0, 0),
          is_mouse_dragging_(false),
          is_left_mouse_down_(false),
          mouse_down_start_time_(0),
          has_browser_focus_(false) {
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
    SDL_Event event_;
    bool show_demo_window_;
    bool running_;
    SDL_Window* window_;
    ImGuiIO* io_ = nullptr;

    bool window_size_changed_ = false;

    std::unique_ptr<VulkanBackend> vulkan_backend_;

    // 键盘输入处理相关成员
    struct PendingKeyEvent {
        enum EventType {
            kMKeyEvent,
            kTextEvent,
            kImeComposition
        };

        EventType type;
        int key_code = 0;
        int scan_code = 0;
        int modifiers = 0;
        bool pressed = false;
        std::string text;
        int ime_start = 0;
        int ime_length = 0;
        bool is_modifier_combo = false;

        explicit PendingKeyEvent(EventType t) : type(t) {}
    };

    std::vector<PendingKeyEvent> pending_key_events_;  // 待处理的键盘事件队列
    int active_tab_id_ = -1;  // 当前活动的标签页ID

    ImVec2 mouse_drag_start_;      // 鼠标拖动起始位置
    bool is_mouse_dragging_;       // 是否正在拖动
    bool is_left_mouse_down_;       // 左键是否按下
    Uint32 mouse_down_start_time_;  // 鼠标按下的开始时间
    bool has_browser_focus_;       // 浏览器是否已有焦点

    Uint32 last_click_time_ = 0;      // 上次点击的时间戳
    ImVec2 last_click_pos_ = {0, 0};  // 上次点击的坐标
    int manual_click_count_ = 0;      // 手动维护的连击计数

    // 连击判定的常量
    const Uint32 kDoubleClickTime = 500;  // 500毫秒内视为连击
    const float kDoubleClickDist = 5.0f;  // 点击距离偏移在5像素内视为连击
};

}  // namespace Corona::Systems