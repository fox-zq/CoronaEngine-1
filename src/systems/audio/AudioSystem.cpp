#include <ResourceTypes.h>
#include <corona/systems/AudioSystem.h>

#include <filesystem>

using namespace Corona;

AudioSystem::AudioSystem()
    : ThreadedSystem("AudioSystem") {
}

void AudioSystem::configure(const Interfaces::SystemContext& context) {
    ThreadedSystem::configure(context);
    resource_service_ = services().try_get<Interfaces::IResourceService>();
    scheduler_ = services().try_get<Interfaces::ICommandScheduler>();
    if (scheduler_) {
        system_queue_handle_ = scheduler_->get_queue(name());
        if (!system_queue_handle_) {
            system_queue_handle_ = scheduler_->create_queue(name());
        }
    }
}

void AudioSystem::onStart() {
}

void AudioSystem::onTick() {
    if (auto* queue = command_queue()) {
        int spun = 0;
        while (spun < 100 && !queue->empty()) {
            if (!queue->try_execute()) {
                continue;
            }
            ++spun;
        }
    }
}
void AudioSystem::onStop() {
}
void AudioSystem::process_audio(uint64_t /*id*/) {
}
