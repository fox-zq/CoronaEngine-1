#pragma once

#include <cstdint>

namespace Corona::Events
{
    /**
     * @brief 显示系统内部事件（单线程使用 EventBus）
     */
    struct DisplaySystemDemoEvent
    {
        int demo_value;
    };

    /**
     * @brief 引擎到显示系统的跨线程事件（使用 EventStream）
     */
    struct EngineToDisplayDemoEvent
    {
        float delta_time;
    };

    /**
     * @brief 显示系统到引擎的跨线程事件（使用 EventStream）
     */
    struct DisplayToEngineDemoEvent
    {
        float delta_time;
    };


    /**
     * @brief 显示表面变化事件（使用 EventBus）
     */
    struct DisplaySurfaceChangedEvent
    {
        void* surface;
    };

    enum class DisplayFrameSource : uint8_t
    {
        ui = 0,
        optics = 1
    };

    /**
     * @brief 待显示帧事件（由 UI/Optics 发布，DisplaySystem 消费）
     */
    struct DisplayFrameReadyEvent
    {
        void* surface = nullptr;
        void* image = nullptr;
        void* executor = nullptr;
        uint64_t frame_index = 0;
        DisplayFrameSource source = DisplayFrameSource::ui;
    };

} // namespace Corona::Events
