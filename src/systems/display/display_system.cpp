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
            if (event.surface == nullptr) {
                return;
            }

            const auto surface_id = reinterpret_cast<uint64_t>(event.surface);
            if (!displayers_.contains(surface_id)) {
                CFW_LOG_INFO("DisplaySystem: Creating new displayer for surface {}", surface_id);
                displayers_.emplace(surface_id, HardwareDisplayer(event.surface));
            }
        });

    optics_frame_sub_id_ = event_bus->subscribe<Events::OpticsFrameReadyEvent>(
        [this](const Events::OpticsFrameReadyEvent& event) {
            if (event.surface == nullptr || event.image == nullptr || event.executor == nullptr) {
                return;
            }

            const auto surface_id = reinterpret_cast<uint64_t>(event.surface);
            auto& layer = surface_states_[surface_id].optics;
            if (event.frame_index >= layer.frame_index) {
                layer.image = event.image;
                layer.executor = event.executor;
                layer.frame_index = event.frame_index;
                layer.width = event.width;
                layer.height = event.height;
            }
        });

    ui_frame_sub_id_ = event_bus->subscribe<Events::UIFrameReadyEvent>(
        [this](const Events::UIFrameReadyEvent& event) {
            if (event.surface == nullptr || event.image == nullptr || event.executor == nullptr) {
                return;
            }

            const auto surface_id = reinterpret_cast<uint64_t>(event.surface);
            auto& layer = surface_states_[surface_id].ui;
            if (event.frame_index >= layer.frame_index) {
                layer.image = event.image;
                layer.executor = event.executor;
                layer.frame_index = event.frame_index;
                layer.width = event.width;
                layer.height = event.height;
            }
        });

    CFW_LOG_DEBUG("DisplaySystem: EventBus subscriptions ready (optics + ui)");
    return true;
}

void DisplaySystem::update() {
    for (auto& [surface_id, displayer] : displayers_) {
        auto it = surface_states_.find(surface_id);
        if (it == surface_states_.end()) {
            continue;
        }

        auto& state = it->second;
        const bool has_optics = state.optics.image != nullptr && state.optics.executor != nullptr;
        const bool has_ui = state.ui.image != nullptr && state.ui.executor != nullptr;

/*        if (has_optics && has_ui) {
            compose_and_present(displayer, state);
        } else if (has_optics) {
            displayer.wait(*state.optics.executor) << *state.optics.image;
        } else */if (has_ui) {
            displayer.wait(*state.ui.executor) << *state.ui.image;
        }
    }
}

void DisplaySystem::compose_and_present(HardwareDisplayer& displayer, SurfaceState& state) {
    const uint32_t out_w = state.ui.width;
    const uint32_t out_h = state.ui.height;

    if (out_w == 0 || out_h == 0) {
        return;
    }

    if (!ensure_composite_resources(out_w, out_h)) {
        // Fallback: present optics layer only when compositing is unavailable
        displayer.wait(*state.optics.executor) << *state.optics.image;
        return;
    }

    composite_pipeline_["pushConsts.bgImage"] = state.optics.image->storeDescriptor();
    composite_pipeline_["pushConsts.fgImage"] = state.ui.image->storeDescriptor();
    composite_pipeline_["pushConsts.outputImage"] = composite_output_.storeDescriptor();
    composite_pipeline_["pushConsts.outputWidth"] = out_w;
    composite_pipeline_["pushConsts.outputHeight"] = out_h;

    compositor_executor_ << composite_pipeline_(out_w / 8, out_h / 8, 1)
                         << compositor_executor_.commit();

    displayer.wait(compositor_executor_) << composite_output_;
}

bool DisplaySystem::ensure_composite_resources(uint32_t width, uint32_t height) {
    if (composite_output_ && composite_width_ == width && composite_height_ == height
        && composite_pipeline_ready_) {
        return true;
    }

    try {
        if (!composite_pipeline_ready_) {
            composite_pipeline_ = ComputePipeline(std::string(k_composite_shader));
            composite_pipeline_ready_ = true;
            CFW_LOG_INFO("DisplaySystem: Composite compute pipeline created");
        }

        composite_output_ = HardwareImage(width, height, ImageFormat::RGBA16_FLOAT, ImageUsage::StorageImage);
        composite_width_ = width;
        composite_height_ = height;
        CFW_LOG_INFO("DisplaySystem: Composite output resized to {}x{}", width, height);
    } catch (const std::exception& e) {
        CFW_LOG_ERROR("DisplaySystem: Failed to create composite resources: {}", e.what());
        composite_pipeline_ready_ = false;
        return false;
    }

    return true;
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
