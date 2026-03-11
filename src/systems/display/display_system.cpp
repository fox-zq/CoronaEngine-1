#include <ranges>
#include <corona/events/display_system_events.h>
#include <corona/events/engine_events.h>
#include <corona/kernel/core/i_logger.h>
#include <corona/kernel/event/i_event_bus.h>
#include <corona/kernel/event/i_event_stream.h>
#include <corona/systems/display/display_system.h>

#include <algorithm>
#include <string>

namespace {

// Alpha compositing compute shader: blends UI (foreground) over Optics (background)
// using standard Porter-Duff Source Over operation.
static constexpr const char* k_composite_shader = R"GLSL(
#version 460
#extension GL_EXT_nonuniform_qualifier : enable

layout(push_constant) uniform PushConsts
{
    uint bgImage;
    uint fgImage;
    uint outputImage;
    uint outputWidth;
    uint outputHeight;
} pushConsts;

layout(set = 0, binding = 0) uniform sampler2D textures[];
layout(set = 0, binding = 1, rgba16f) uniform image2D images[];

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

void main()
{
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    if (uint(pos.x) >= pushConsts.outputWidth || uint(pos.y) >= pushConsts.outputHeight)
        return;

    vec2 uv = (vec2(pos) + 0.5) / vec2(pushConsts.outputWidth, pushConsts.outputHeight);
    vec4 bg = texture(textures[nonuniformEXT(pushConsts.bgImage)], uv);
    vec4 fg = texture(textures[nonuniformEXT(pushConsts.fgImage)], uv);

    // Porter-Duff Source Over
    vec3 color = fg.rgb + bg.rgb * (1.0 - fg.a);
    float alpha = fg.a + bg.a * (1.0 - fg.a);

    imageStore(images[nonuniformEXT(pushConsts.outputImage)], pos, vec4(color, alpha));
}
)GLSL";

}  // namespace

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
            if (event.surface_id == 0) {
                return;
            }

            const auto surface_id = event.surface_id;
            auto* surface = reinterpret_cast<void*>(surface_id);
            std::lock_guard<std::mutex> lock(frame_mutex_);
            if (!displayers_.contains(surface_id)) {
                CFW_LOG_INFO("DisplaySystem: Creating new displayer for surface {}", surface_id);
                displayers_.emplace(surface_id, HardwareDisplayer(surface));
            }
        });

    optics_frame_sub_id_ = event_bus->subscribe<Events::OpticsFrameReadyEvent>(
        [this](const Events::OpticsFrameReadyEvent& event) {
            if (event.surface_id == 0 || event.image_id == 0 || event.executor_id == 0) {
                return;
            }

            const auto surface_id = event.surface_id;

            std::lock_guard<std::mutex> lock(frame_mutex_);
            auto& layer = surface_states_[surface_id].optics;
            if (event.frame_index >= layer.frame_index) {
                layer.image_id = event.image_id;
                layer.executor_id = event.executor_id;
                layer.frame_index = event.frame_index;
                layer.width = event.width;
                layer.height = event.height;
            }
        });

    ui_frame_sub_id_ = event_bus->subscribe<Events::UIFrameReadyEvent>(
        [this](const Events::UIFrameReadyEvent& event) {
            if (event.surface_id == 0 || event.image_id == 0 || event.executor_id == 0) {
                return;
            }

            const auto surface_id = event.surface_id;

            std::lock_guard<std::mutex> lock(frame_mutex_);
            auto& layer = surface_states_[surface_id].ui;
            if (event.frame_index >= layer.frame_index) {
                layer.image_id = event.image_id;
                layer.executor_id = event.executor_id;
                layer.frame_index = event.frame_index;
                layer.width = event.width;
                layer.height = event.height;
            }
        });

    CFW_LOG_DEBUG("DisplaySystem: EventBus subscriptions ready (optics + ui)");
    return true;
}

void DisplaySystem::update() {
    // Snapshot shared state under lock, then release before GPU work
    std::unordered_map<uint64_t, SurfaceState> states_snapshot;
    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        states_snapshot = surface_states_;
    }

    for (auto& [surface_id, displayer] : displayers_) {
        auto it = states_snapshot.find(surface_id);
        if (it == states_snapshot.end()) {
            continue;
        }

        auto& state = it->second;
        const bool has_optics = state.optics.image_id != 0 && state.optics.executor_id != 0;
        const bool has_ui = state.ui.image_id != 0 && state.ui.executor_id != 0;

        if (has_optics && has_ui) {
            compose_and_present(displayer, state);
        } else if (has_optics) {
            auto* optics_image = reinterpret_cast<HardwareImage*>(state.optics.image_id);
            auto* optics_executor = reinterpret_cast<HardwareExecutor*>(state.optics.executor_id);
            if (optics_image != nullptr && optics_executor != nullptr) {
                displayer.wait(*optics_executor) << *optics_image;
            }
        } else if (has_ui) {
            auto* ui_image = reinterpret_cast<HardwareImage*>(state.ui.image_id);
            auto* ui_executor = reinterpret_cast<HardwareExecutor*>(state.ui.executor_id);
            if (ui_image != nullptr && ui_executor != nullptr) {
                displayer.wait(*ui_executor) << *ui_image;
            }
        }
    }
}

bool DisplaySystem::ensure_composite_resources(uint32_t width, uint32_t height) {
    if (!composite_pipeline_ready_) {
        try {
            composite_pipeline_ = ComputePipeline(std::string(k_composite_shader));
            composite_pipeline_ready_ = true;
            CFW_LOG_INFO("DisplaySystem: Composite compute pipeline created");
        } catch (const std::exception& e) {
            CFW_LOG_ERROR("DisplaySystem: Failed to create composite pipeline: {}", e.what());
            return false;
        }
    }

    if (composite_width_ != width || composite_height_ != height || !composite_output_) {
        composite_output_ = HardwareImage(width, height, ImageFormat::RGBA16_FLOAT, ImageUsage::StorageImage);
        if (!composite_output_) {
            CFW_LOG_ERROR("DisplaySystem: Failed to create composite output ({}x{})", width, height);
            return false;
        }
        composite_width_ = width;
        composite_height_ = height;
        CFW_LOG_INFO("DisplaySystem: Composite output image created ({}x{})", width, height);
    }

    return true;
}

void DisplaySystem::compose_and_present(HardwareDisplayer& displayer, SurfaceState& state) {
    auto* optics_image = reinterpret_cast<HardwareImage*>(state.optics.image_id);
    auto* optics_executor = reinterpret_cast<HardwareExecutor*>(state.optics.executor_id);
    auto* ui_image = reinterpret_cast<HardwareImage*>(state.ui.image_id);
    auto* ui_executor = reinterpret_cast<HardwareExecutor*>(state.ui.executor_id);

    if (optics_image == nullptr || optics_executor == nullptr || ui_image == nullptr || ui_executor == nullptr) {
        return;
    }

    const uint32_t out_w = state.ui.width;
    const uint32_t out_h = state.ui.height;

    if (out_w == 0 || out_h == 0) {
        return;
    }

    if (!ensure_composite_resources(out_w, out_h)) {
        return;
    }

    composite_pipeline_["pushConsts.bgImage"] = optics_image->storeDescriptor();
    composite_pipeline_["pushConsts.fgImage"] = ui_image->storeDescriptor();
    composite_pipeline_["pushConsts.outputImage"] = composite_output_.storeDescriptor();
    composite_pipeline_["pushConsts.outputWidth"] = out_w;
    composite_pipeline_["pushConsts.outputHeight"] = out_h;

    compositor_executor_.wait(*optics_executor);
    compositor_executor_.wait(*ui_executor);

    compositor_executor_ << composite_pipeline_(out_w / 8, out_h / 8, 1)
                         << compositor_executor_.commit();

    displayer.wait(compositor_executor_) << composite_output_;
}


void DisplaySystem::shutdown() {
    CFW_LOG_NOTICE("DisplaySystem: Shutting down...");

    if (auto* event_bus = context()->event_bus()) {
        if (surface_changed_sub_id_ != 0) {
            event_bus->unsubscribe(surface_changed_sub_id_);
        }
        if (optics_frame_sub_id_ != 0) {
            event_bus->unsubscribe(optics_frame_sub_id_);
        }
        if (ui_frame_sub_id_ != 0) {
            event_bus->unsubscribe(ui_frame_sub_id_);
        }
    }

    composite_pipeline_ready_ = false;
    composite_output_ = HardwareImage();
    composite_pipeline_ = ComputePipeline();

    surface_states_.clear();
    displayers_.clear();
    CFW_LOG_DEBUG("DisplaySystem: Shutdown complete");
}

}  // namespace Corona::Systems
