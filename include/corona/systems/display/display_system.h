#pragma once

#include <corona/events/display_system_events.h>
#include <corona/kernel/event/i_event_bus.h>
#include <corona/kernel/event/i_event_stream.h>
#include <corona/kernel/system/system_base.h>
#include <CabbageHardware.h>

#include <memory>

namespace Corona::Systems
{
    /**
     * @brief 显示系统
     *
     * 负责管理窗口、输入事件和显示设备。
     * 运行在独立线程，以 120 FPS 响应输入事件。
     */
    class DisplaySystem : public Kernel::SystemBase
    {
    public:
        DisplaySystem()
        {
            set_target_fps(120); // 显示系统高刷新率以提升响应速度
        }

        ~DisplaySystem() override = default;

        // ========================================
        // ISystem 接口实现
        // ========================================

        std::string_view get_name() const override
        {
            return "Display";
        }

        int get_priority() const override
        {
            return 100; // 最高优先级，最先初始化
        }

        /**
         * @brief 初始化显示系统
         * @param ctx 系统上下文
         * @return 初始化成功返回 true
         */
        bool initialize(Kernel::ISystemContext* ctx) override;

        /**
         * @brief 每帧更新显示
         *
         * 在独立线程中调用，处理窗口事件和输入
         */
        void update() override;

        /**
         * @brief 关闭显示系统
         *
         * 销毁窗口并清理显示资源
         */
        void shutdown() override;

    private:
        Kernel::EventId surface_changed_sub_id_ = 0;

        std::unordered_map<uint64_t, HardwareDisplayer> displayers_;
    };
} // namespace Corona::Systems
