#include "RuntimeLoop.h"

#include <corona/api/CoronaEngineAPI.h>
#include <corona/core/Engine.h>
#include <corona/core/SystemRegistry.h>
#include <corona/systems/AnimationSystem.h>
#include <corona/systems/AudioSystem.h>
#include <corona/systems/DisplaySystem.h>
#include <corona/systems/RenderingSystem.h>

#include <chrono>
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace {
using clock_type = std::chrono::steady_clock;
constexpr double kTargetFps = 120.0;
constexpr std::chrono::duration<double> kFrameDuration{1.0 / kTargetFps};
}  // namespace

RuntimeLoop::RuntimeLoop(Corona::Engine& engine) : engine_(engine) {
    base_entities_.reserve(100);
}

namespace {
std::vector<std::string> parse_requested_systems() {
    std::vector<std::string> requested;
    if (const char* env = std::getenv("CORONA_RUNTIME_SYSTEMS")) {
        std::string value{env};
        std::stringstream ss{value};
        std::string token;
        while (std::getline(ss, token, ',')) {
            if (!token.empty()) {
                requested.push_back(token);
            }
        }
    }
    if (requested.empty()) {
        requested = {"AnimationSystem", "RenderingSystem", "AudioSystem", "DisplaySystem"};
    }
    return requested;
}
}  // namespace

void RuntimeLoop::initialize() {
    build_base_entities();

    auto& registry = engine_.system_registry();
    if (!registry.contains("AnimationSystem")) {
        registry.register_plugin({.name = "AnimationSystem",
                                  .dependencies = {},
                                  .factory = [](const Corona::Interfaces::SystemContext&) {
                                      return std::make_shared<Corona::AnimationSystem>();
                                  },
                                  .description = "Animates skeletal models"});
    }
    if (!registry.contains("RenderingSystem")) {
        registry.register_plugin({.name = "RenderingSystem",
                                  .dependencies = {"AnimationSystem"},
                                  .factory = [](const Corona::Interfaces::SystemContext&) {
                                      return std::make_shared<Corona::RenderingSystem>();
                                  },
                                  .description = "Renders scenes using raster and compute pipelines"});
    }
    if (!registry.contains("AudioSystem")) {
        registry.register_plugin({.name = "AudioSystem",
                                  .dependencies = {},
                                  .factory = [](const Corona::Interfaces::SystemContext&) {
                                      return std::make_shared<Corona::AudioSystem>();
                                  },
                                  .description = "Handles audio playback"});
    }
    if (!registry.contains("DisplaySystem")) {
        registry.register_plugin({.name = "DisplaySystem",
                                  .dependencies = {"RenderingSystem"},
                                  .factory = [](const Corona::Interfaces::SystemContext&) {
                                      return std::make_shared<Corona::DisplaySystem>();
                                  },
                                  .description = "Presents final frames"});
    }

    auto requested = parse_requested_systems();
    auto resolution = registry.resolve(requested);
    if (!resolution.missing.empty()) {
        for (const auto& name : resolution.missing) {
            CE_LOG_ERROR("Requested system '{}' is not registered", name);
        }
    }
    if (!resolution.cycles.empty()) {
        for (const auto& cycle : resolution.cycles) {
            std::string joined;
            for (size_t i = 0; i < cycle.size(); ++i) {
                if (i > 0) {
                    joined += " -> ";
                }
                joined += cycle[i];
            }
            CE_LOG_ERROR("System dependency cycle detected: {}", joined);
        }
    }

    if (!resolution.success()) {
        throw std::runtime_error("System resolution failed; aborting runtime initialization");
    }

    auto base_context = engine_.kernel().make_context(nullptr, nullptr);
    auto instances = registry.instantiate(resolution, base_context);
    for (auto& instance : instances) {
        engine_.adopt_system(instance);
    }

    animation_system_ = engine_.has_system<Corona::AnimationSystem>() ? &engine_.get_system<Corona::AnimationSystem>() : nullptr;
    rendering_system_ = engine_.has_system<Corona::RenderingSystem>() ? &engine_.get_system<Corona::RenderingSystem>() : nullptr;
    audio_system_ = engine_.has_system<Corona::AudioSystem>() ? &engine_.get_system<Corona::AudioSystem>() : nullptr;
    display_system_ = engine_.has_system<Corona::DisplaySystem>() ? &engine_.get_system<Corona::DisplaySystem>() : nullptr;

    on_initialize();

    engine_.start_systems();

    animation_running_ = true;
    rendering_running_ = true;
    audio_running_ = true;
    display_running_ = true;
}

void RuntimeLoop::run(std::atomic<bool>& running_flag) {
    std::uint64_t frame_counter_ = 0;
    CE_LOG_INFO("Entering main loop (press Ctrl+C to exit)...");
    while (running_flag.load(std::memory_order_relaxed)) {
        const auto frame_start = clock_type::now();

        on_tick();

        ++frame_counter_;

        // toggle_cycle<CoronaEngineAPI::RenderTag>(600, 300, "RenderTag");
        // toggle_cycle<CoronaEngineAPI::AnimationTag>(720, 360, "AnimationTag");
        // toggle_cycle<CoronaEngineAPI::AudioTag>(840, 420, "AudioTag");
        // toggle_cycle<CoronaEngineAPI::DisplayTag>(960, 480, "DisplayTag");

        {
            const bool has_render_entities = !registry_.storage<CoronaEngineAPI::RenderTag>().empty();
            const bool has_animation_entities = !registry_.storage<CoronaEngineAPI::AnimationTag>().empty();
            const bool has_audio_entities = !registry_.storage<CoronaEngineAPI::AudioTag>().empty();
            const bool has_display_entities = !registry_.storage<CoronaEngineAPI::DisplayTag>().empty();

            if (rendering_system_) {
                update_system(has_render_entities, rendering_running_, *rendering_system_);
            }
            if (animation_system_) {
                update_system(has_animation_entities, animation_running_, *animation_system_);
            }
            if (audio_system_) {
                update_system(has_audio_entities, audio_running_, *audio_system_);
            }
            if (display_system_) {
                update_system(has_display_entities, display_running_, *display_system_);
            }

            if (frame_counter_ % 600 == 0) {
                CE_LOG_INFO("Frame {}: Systems status - Animation: {}, Rendering: {}, Audio: {}, Display: {}",
                            frame_counter_,
                            animation_running_ ? "Running" : "Paused",
                            rendering_running_ ? "Running" : "Paused",
                            audio_running_ ? "Running" : "Paused",
                            display_running_ ? "Running" : "Paused");
            }
        }

        const auto frame_elapsed = clock_type::now() - frame_start;
        if (frame_elapsed < kFrameDuration) {
            std::this_thread::sleep_for(kFrameDuration - frame_elapsed);
        }
    }
    CE_LOG_INFO("Main loop exited");
}

void RuntimeLoop::shutdown() {
    on_shutdown();
    engine_.stop_systems();
    CE_LOG_INFO("All systems stopped");
}

void RuntimeLoop::build_base_entities() {
    base_entities_.clear();
    base_entities_.reserve(100);

    for (size_t i = 0; i < 100; ++i) {
        auto entity = registry_.create();
        base_entities_.push_back(entity);
        registry_.emplace<CoronaEngineAPI::RenderTag>(entity);
        registry_.emplace<CoronaEngineAPI::AnimationTag>(entity);
        registry_.emplace<CoronaEngineAPI::AudioTag>(entity);
        registry_.emplace<CoronaEngineAPI::DisplayTag>(entity);
    }
}

void RuntimeLoop::on_initialize() {}
void RuntimeLoop::on_shutdown() {}
void RuntimeLoop::on_tick() {}