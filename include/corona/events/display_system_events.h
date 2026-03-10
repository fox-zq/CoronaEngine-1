#pragma once

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

} // namespace Corona::Events
