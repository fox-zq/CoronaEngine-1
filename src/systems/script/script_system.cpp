#include <corona/events/script_system_events.h>
#include <corona/events/engine_events.h>
#include <corona/kernel/core/i_logger.h>
#include <corona/kernel/event/i_event_bus.h>
#include <corona/kernel/event/i_event_stream.h>
#include <corona/systems/script/script_system.h>

namespace Corona::Systems {

bool ScriptSystem::initialize(Kernel::ISystemContext* ctx) {
    CFW_LOG_NOTICE("ScriptSystem: Initializing...");

    // 【订阅系统内部事件】使用 EventBus
    auto* event_bus = ctx->event_bus();
    if (event_bus) {
        CFW_LOG_DEBUG("ScriptSystem: EventBus subscriptions ready");
    } else {
        CFW_LOG_WARNING("ScriptSystem: No event bus available");
    }

    return true;
}

void ScriptSystem::update() {

#ifdef CORONA_ENABLE_PYTHON_API
    // python_api_.runPythonScript();
#endif

}

void ScriptSystem::shutdown() {
    CFW_LOG_NOTICE("ScriptSystem: Shutting down...");
}

}  // namespace Corona::Systems
