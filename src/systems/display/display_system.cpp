#include <corona/events/display_system_events.h>
#include <corona/events/engine_events.h>
#include <corona/kernel/core/i_logger.h>
#include <corona/kernel/event/i_event_bus.h>
#include <corona/kernel/event/i_event_stream.h>
#include <corona/systems/display/display_system.h>

namespace Corona::Systems {

bool DisplaySystem::initialize(Kernel::ISystemContext* ctx) {
    CFW_LOG_NOTICE("DisplaySystem: Initializing...");

    // 【订阅系统内部事件】使用 EventBus
    auto* event_bus = ctx->event_bus();
    if (event_bus) {
        CFW_LOG_DEBUG("DisplaySystem: EventBus subscriptions ready");
    } else {
        CFW_LOG_WARNING("DisplaySystem: No event bus available");
    }

    // 【订阅系统内部事件】使用 EventBus
    if (auto* event_bus = ctx->event_bus()) {
        surface_changed_sub_id_ = event_bus->subscribe<Events::DisplaySurfaceChangedEvent>(
            [this](const Events::DisplaySurfaceChangedEvent& event) {
                CFW_LOG_INFO("DisplaySystem: Received DisplaySurfaceChangedEvent, new surface: {}",
                             static_cast<uintptr_t>(reinterpret_cast<uintptr_t>(event.surface)));
                if (event.surface) {
                    auto surface_id = reinterpret_cast<uint64_t>(event.surface);
                    if (!displayers_.contains(surface_id)) {
                        CFW_LOG_INFO("DisplaySystem: Creating new displayer for surface {}", surface_id);
                        displayers_.emplace(surface_id, HardwareDisplayer(event.surface));
                    }
                }
            });
        CFW_LOG_DEBUG("DisplaySystem: Subscribed to DisplaySurfaceChangedEvent");
    } else {
        CFW_LOG_WARNING("DisplaySystem: No event bus available, cannot subscribe to events");
    }

    return true;
}

void DisplaySystem::update() {

}

void DisplaySystem::shutdown() {
    CFW_LOG_NOTICE("DisplaySystem: Shutting down...");
    // 取消 EventBus 订阅
    if (auto* event_bus = context()->event_bus())
    {
        if (surface_changed_sub_id_ != 0)
        {
            event_bus->unsubscribe(surface_changed_sub_id_);
            CFW_LOG_DEBUG("OpticsSystem: Unsubscribed from events");
        }
    }
}

}  // namespace Corona::Systems
