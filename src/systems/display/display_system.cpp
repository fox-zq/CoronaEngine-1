#include <ranges>
#include <corona/events/display_system_events.h>
#include <corona/events/engine_events.h>
#include <corona/kernel/core/i_logger.h>
#include <corona/kernel/event/i_event_bus.h>
#include <corona/kernel/event/i_event_stream.h>
#include <corona/systems/display/display_system.h>

namespace Corona::Systems {

bool DisplaySystem::initialize(Kernel::ISystemContext* ctx) {
    CFW_LOG_NOTICE("DisplaySystem: Initializing...");

    auto* event_bus = ctx->event_bus();
    if (event_bus == nullptr) {
        CFW_LOG_WARNING("DisplaySystem: No event bus available");
        return true;
    }

    surface_changed_sub_id_ = event_bus->subscribe<Events::DisplaySurfaceChangedEvent>(
        [this](const Events::DisplaySurfaceChangedEvent& event) {
            if (event.surface == nullptr) {
                return;
            }

            const auto surface_id = reinterpret_cast<uint64_t>(event.surface);
            if (!displayers_.contains(surface_id)) {
                CFW_LOG_INFO("DisplaySystem: Creating new displayer for surface {}", surface_id);
                displayers_.emplace(surface_id, HardwareDisplayer(event.surface));
            }
        });

    frame_ready_sub_id_ = event_bus->subscribe<Events::DisplayFrameReadyEvent>(
        [this](const Events::DisplayFrameReadyEvent& event) {
            if (event.surface == nullptr || event.image == nullptr || event.executor == nullptr) {
                return;
            }

            const auto surface_id = reinterpret_cast<uint64_t>(event.surface);
            auto& slot = latest_frames_[surface_id];
            if (event.frame_index >= slot.frame_index) {
                slot.image = event.image;
                slot.executor = event.executor;
                slot.frame_index = event.frame_index;
                slot.source = event.source;
            }
        });

    CFW_LOG_DEBUG("DisplaySystem: EventBus subscriptions ready");
    return true;
}

void DisplaySystem::update() {
    for (auto& [surface_id, displayer] : displayers_) {
        auto frame_it = latest_frames_.find(surface_id);
        if (frame_it == latest_frames_.end()) {
            continue;
        }

        auto* image = static_cast<HardwareImage*>(frame_it->second.image);
        auto* executor = static_cast<HardwareExecutor*>(frame_it->second.executor);
        if (image == nullptr || executor == nullptr) {
            continue;
        }

        displayer.wait(*executor) << *image;
    }
}

void DisplaySystem::shutdown() {
    CFW_LOG_NOTICE("DisplaySystem: Shutting down...");

    if (auto* event_bus = context()->event_bus()) {
        if (surface_changed_sub_id_ != 0) {
            event_bus->unsubscribe(surface_changed_sub_id_);
        }
        if (frame_ready_sub_id_ != 0) {
            event_bus->unsubscribe(frame_ready_sub_id_);
        }
    }

    latest_frames_.clear();
    displayers_.clear();
    CFW_LOG_DEBUG("DisplaySystem: Unsubscribed from events");
}

}  // namespace Corona::Systems
