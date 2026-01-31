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
    ImGuiIO* io = nullptr;

    std::unique_ptr<VulkanBackend> m_VulkanBackend;

    // 键盘输入处理相关成员
    struct PendingKeyEvent {
        enum EventType {
            MKEY_EVENT,
            TEXT_EVENT,
            IME_COMPOSITION
        };

        EventType type;
        int key_code = 0;
        int scan_code = 0;
        int modifiers = 0;
        bool pressed = false;
        std::string text;
        int ime_start = 0;
        int ime_length = 0;

        PendingKeyEvent(EventType t) : type(t) {}
    };

    std::vector<PendingKeyEvent> m_PendingKeyEvents;
    int m_ActiveTabId = -1;  // 当前活动的标签页ID

    // 调试输出函数
    void DebugLog(const char* format, ...);
    void DebugLogKeyEvent(const SDL_Event& event, const char* action);
    void DebugLogTextEvent(const SDL_Event& event);
    void DebugLogIMEEvent(const SDL_Event& event);

    // 输入处理函数
    void ProcessSDLKeyEvent(const SDL_Event& event);
    void ProcessSDLTextEvent(const SDL_Event& event);
    void ProcessSDLIMEEvent(const SDL_Event& event);
    void SendKeyEventsToBrowser(int tabId);
};

}  // namespace Corona::Systems