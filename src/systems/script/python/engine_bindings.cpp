#include <corona/kernel/core/callback_sink.h>
#include <corona/kernel/core/i_logger.h>
#include <corona/systems/script/corona_engine_api.h>
#include <corona/systems/script/engine_scripts.h>
#include <nanobind/nanobind.h>
#include <nanobind/stl/array.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <array>
#include <cstdint>

namespace nb = nanobind;
using namespace Corona::API;

namespace EngineScripts {

void BindAll(nanobind::module_& m) {
    // ============================================================================
    // Geometry: 作为所有组件的锚点，存储位置/旋转/缩放和模型数据
    // ============================================================================
    nb::class_<Geometry>(m, "Geometry")
        .def(nb::init<const std::string&>(), nb::arg("model_path"),
             "Create a Geometry from a model file path")
        .def("set_position", &Geometry::set_position, nb::arg("position"),
             "Set local position [x, y, z]")
        .def("set_rotation", &Geometry::set_rotation, nb::arg("euler"),
             "Set local rotation (Euler angles ZYX order) [pitch, yaw, roll]")
        .def("set_scale", &Geometry::set_scale, nb::arg("scale"),
             "Set local scale [x, y, z]")
        .def("get_position", &Geometry::get_position,
             "Get local position [x, y, z]")
        .def("get_rotation", &Geometry::get_rotation,
             "Get local rotation (Euler angles) [pitch, yaw, roll]")
        .def("get_scale", &Geometry::get_scale,
             "Get local scale [x, y, z]");

    // ============================================================================
    // Mechanics: 物理/力学组件
    // ============================================================================
    nb::class_<Mechanics>(m, "Mechanics")
        .def(nb::init<Geometry&>(), nb::arg("geometry"),
             "Create a Mechanics component attached to a Geometry")
        .def("set_mass", &Mechanics::set_mass, nb::arg("mass"),
             "Set object mass")
        .def("get_mass", &Mechanics::get_mass,
             "Get object mass")
        .def("set_restitution", &Mechanics::set_restitution, nb::arg("restitution"),
             "Set object restitution (bounciness)")
        .def("get_restitution", &Mechanics::get_restitution,
             "Get object restitution")
        .def("set_damping", &Mechanics::set_damping, nb::arg("damping"),
             "Set velocity damping factor")
        .def("get_damping", &Mechanics::get_damping,
             "Get velocity damping factor");

    // ============================================================================
    // Optics: 光学/渲染组件
    // ============================================================================
    nb::class_<Optics>(m, "Optics")
        .def(nb::init<Geometry&>(), nb::arg("geometry"),
             "Create an Optics component attached to a Geometry");

    // ============================================================================
    // Acoustics: 声学组件
    // ============================================================================
    nb::class_<Acoustics>(m, "Acoustics")
        .def(nb::init<Geometry&>(), nb::arg("geometry"),
             "Create an Acoustics component attached to a Geometry")
        .def("set_volume", &Acoustics::set_volume, nb::arg("volume"),
             "Set audio volume")
        .def("get_volume", &Acoustics::get_volume,
             "Get audio volume");

    // ============================================================================
    // Kinematics: 运动学/动画组件
    // ============================================================================
    nb::class_<Kinematics>(m, "Kinematics")
        .def(nb::init<Geometry&>(), nb::arg("geometry"),
             "Create a Kinematics component attached to a Geometry")
        .def("set_animation", &Kinematics::set_animation, nb::arg("animation_index"),
             "Set active animation by index")
        .def("play_animation", &Kinematics::play_animation, nb::arg("speed") = 1.0f,
             "Play the current animation at specified speed")
        .def("stop_animation", &Kinematics::stop_animation,
             "Stop the current animation")
        .def("get_animation_index", &Kinematics::get_animation_index,
             "Get current animation index")
        .def("get_current_time", &Kinematics::get_current_time,
             "Get current animation time");

    // ============================================================================
    // Actor: OOP 风格的实体类，支持多套组件配置（Profile）
    // ============================================================================
    nb::class_<Actor::Profile>(m, "ActorProfile")
        .def(nb::init<>())
        .def_rw("optics", &Actor::Profile::optics, "Optics component")
        .def_rw("acoustics", &Actor::Profile::acoustics, "Acoustics component")
        .def_rw("mechanics", &Actor::Profile::mechanics, "Mechanics component")
        .def_rw("kinematics", &Actor::Profile::kinematics, "Kinematics component")
        .def_rw("geometry", &Actor::Profile::geometry, "Geometry anchor");

    nb::class_<Actor>(m, "Actor")
        .def(nb::init<>(), "Create an empty Actor")
        .def("add_profile", &Actor::add_profile, nb::arg("profile"),
             "Add a component profile to this actor. Returns pointer to the stored profile.",
             nb::rv_policy::reference_internal)
        .def("remove_profile", &Actor::remove_profile, nb::arg("profile"),
             "Remove a component profile from this actor")
        .def("set_active_profile", &Actor::set_active_profile, nb::arg("profile"),
             "Set the active profile for this actor")
        .def("get_active_profile", &Actor::get_active_profile,
             "Get the currently active profile",
             nb::rv_policy::reference_internal)
        .def("profile_count", &Actor::profile_count,
             "Get number of profiles in this actor");

    // ============================================================================
    // Camera: 相机类
    // ============================================================================
    nb::class_<Camera>(m, "Camera")
        .def(nb::init<>(), "Create a default Camera")
        .def(nb::init<const std::array<float, 3>&, const std::array<float, 3>&,
                      const std::array<float, 3>&, float>(),
             nb::arg("position"), nb::arg("forward"), nb::arg("world_up"), nb::arg("fov"),
             "Create a Camera with specified parameters")
        .def("set", &Camera::set,
             nb::arg("position"), nb::arg("forward"), nb::arg("world_up"), nb::arg("fov"),
             "Set all camera parameters at once")
        .def("save_screenshot", &Camera::save_screenshot, nb::arg("path"),
             "Save a screenshot from this camera's perspective to file")
        .def("set_surface", [](Camera& self, std::uintptr_t surface) { self.set_surface(reinterpret_cast<void*>(surface)); }, nb::arg("surface"), "Set render surface (pass window ID as integer)")
        .def("get_position", &Camera::get_position, "Get camera position [x, y, z]")
        .def("get_forward", &Camera::get_forward, "Get camera forward direction [x, y, z]")
        .def("get_world_up", &Camera::get_world_up, "Get camera world up vector [x, y, z]")
        .def("get_fov", &Camera::get_fov, "Get field of view in degrees")
        .def("set_view_front", &Camera::set_view_front, nb::arg("distance") = 5.0f, "正视图: position from -Z looking toward +Z (distance: camera distance from origin)")
        .def("set_view_back", &Camera::set_view_back, nb::arg("distance") = 5.0f, "后视图: position from +Z looking toward -Z")
        .def("set_view_left", &Camera::set_view_left, nb::arg("distance") = 5.0f, "左视图: position from -X looking toward +X")
        .def("set_view_right", &Camera::set_view_right, nb::arg("distance") = 5.0f, "右视图: position from +X looking toward -X")
        .def("set_view_top", &Camera::set_view_top, nb::arg("distance") = 5.0f, "俯视图: position from +Y looking toward -Y")
        .def("set_view_bottom", &Camera::set_view_bottom, nb::arg("distance") = 5.0f, "仰视图: position from -Y looking toward +Y");

    // ============================================================================
    // ImageEffects: 图像效果类
    // ============================================================================
    nb::class_<ImageEffects>(m, "ImageEffects")
        .def(nb::init<>(), "Create an ImageEffects instance");

    // ============================================================================
    // Viewport: 视口类
    // ============================================================================
    nb::class_<Viewport>(m, "Viewport")
        .def(nb::init<>(), "Create a default Viewport")
        .def(nb::init<int, int, bool>(),
             nb::arg("width"), nb::arg("height"), nb::arg("light_field") = false,
             "Create a Viewport with specified dimensions")
        .def("set_camera", &Viewport::set_camera, nb::arg("camera"),
             "Set the camera for this viewport")
        .def("get_camera", &Viewport::get_camera,
             "Get the camera attached to this viewport",
             nb::rv_policy::reference)
        .def("has_camera", &Viewport::has_camera,
             "Check if viewport has a camera")
        .def("remove_camera", &Viewport::remove_camera,
             "Remove camera from this viewport")
        .def("set_image_effects", &Viewport::set_image_effects, nb::arg("effects"),
             "Set image effects for this viewport")
        .def("get_image_effects", &Viewport::get_image_effects,
             "Get image effects attached to this viewport",
             nb::rv_policy::reference)
        .def("has_image_effects", &Viewport::has_image_effects,
             "Check if viewport has image effects")
        .def("remove_image_effects", &Viewport::remove_image_effects,
             "Remove image effects from this viewport")
        .def("set_size", &Viewport::set_size, nb::arg("width"), nb::arg("height"),
             "Set viewport dimensions")
        .def("set_viewport_rect", &Viewport::set_viewport_rect,
             nb::arg("x"), nb::arg("y"), nb::arg("width"), nb::arg("height"),
             "Set viewport rectangle")
        .def("pick_actor_at_pixel", &Viewport::pick_actor_at_pixel,
             nb::arg("x"), nb::arg("y"),
             "Pick actor at pixel coordinates")
        .def("save_screenshot", &Viewport::save_screenshot, nb::arg("path"),
             "Save a screenshot from this viewport's camera to file");

    // ============================================================================
    // Environment: 环境类
    // ============================================================================
    nb::class_<Environment>(m, "Environment")
        .def(nb::init<>(), "Create an Environment")
        .def("set_sun_direction", &Environment::set_sun_direction, nb::arg("direction"),
             "Set sun light direction [x, y, z]")
        .def("set_floor_grid", &Environment::set_floor_grid, nb::arg("enabled"),
             "Enable or disable floor grid rendering")
        .def("set_gravity", &Environment::set_gravity, nb::arg("gravity"),
             "Set gravity vector [x, y, z]")
        .def("get_gravity", &Environment::get_gravity,
             "Get gravity vector [x, y, z]")
        .def("set_floor_y", &Environment::set_floor_y, nb::arg("y"),
             "Set floor plane Y height")
        .def("get_floor_y", &Environment::get_floor_y,
             "Get floor plane Y height")
        .def("set_floor_restitution", &Environment::set_floor_restitution, nb::arg("restitution"),
             "Set floor restitution (bounciness)")
        .def("get_floor_restitution", &Environment::get_floor_restitution,
             "Get floor restitution")
        .def("set_fixed_dt", &Environment::set_fixed_dt, nb::arg("dt"),
             "Set physics fixed time step")
        .def("get_fixed_dt", &Environment::get_fixed_dt,
             "Get physics fixed time step");

    // ============================================================================
    // Scene: 场景类
    // ============================================================================
    nb::class_<Scene>(m, "Scene")
        .def(nb::init<>(), "Create an empty Scene")
        // Environment management
        .def("set_environment", &Scene::set_environment, nb::arg("environment"),
             "Set the scene environment")
        .def("get_environment", &Scene::get_environment,
             "Get the scene environment",
             nb::rv_policy::reference)
        .def("has_environment", &Scene::has_environment,
             "Check if scene has an environment")
        .def("remove_environment", &Scene::remove_environment,
             "Remove environment from scene")
        // Actor management
        .def("add_actor", &Scene::add_actor, nb::arg("actor"),
             "Add an actor to the scene")
        .def("remove_actor", &Scene::remove_actor, nb::arg("actor"),
             "Remove an actor from the scene")
        .def("clear_actors", &Scene::clear_actors,
             "Remove all actors from the scene")
        .def("actor_count", &Scene::actor_count,
             "Get number of actors in the scene")
        .def("has_actor", &Scene::has_actor, nb::arg("actor"),
             "Check if actor is in the scene")
        // Viewport management
        .def("add_viewport", &Scene::add_viewport, nb::arg("viewport"),
             "Add a viewport to the scene")
        .def("remove_viewport", &Scene::remove_viewport, nb::arg("viewport"),
             "Remove a viewport from the scene")
        .def("clear_viewports", &Scene::clear_viewports,
             "Remove all viewports from the scene")
        .def("viewport_count", &Scene::viewport_count,
             "Get number of viewports in the scene")
        .def("has_viewport", &Scene::has_viewport, nb::arg("viewport"),
             "Check if viewport is in the scene");

    // ============================================================================
    // Scene I/O utilities
    // ============================================================================
    // m.def("read_scene", &read_scene, nb::arg("scene_path"),
    //       "Load a scene from file",
    //       nb::rv_policy::take_ownership);
    // m.def("write_scene", &write_scene, nb::arg("scene"), nb::arg("scene_path"),
    //       "Save a scene to file");

    // ============================================================================
    // Logger: 日志前端转发接口
    // ============================================================================
    nb::class_<Corona::Kernel::LogEntry>(m, "LogEntry")
        .def_ro("level", &Corona::Kernel::LogEntry::level,
                "Log level string: TRACE/DEBUG/INFO/WARNING/ERROR/CRITICAL")
        .def_ro("message", &Corona::Kernel::LogEntry::message,
                "Formatted log message")
        .def_ro("timestamp", &Corona::Kernel::LogEntry::timestamp,
                "Timestamp in nanoseconds since epoch");

    m.def("drain_logs", []() -> std::vector<Corona::Kernel::LogEntry> { return Corona::Kernel::CoronaLogger::drain_logs(); }, "Drain all pending log entries from the engine log queue");

    m.def("send_log", [](const std::string& level, const std::string& message) {
              if (level == "TRACE") {
                  PY_LOG_TRACE("{}", message.c_str());
              } else if (level == "DEBUG") {
                  PY_LOG_DEBUG("{}", message.c_str());
              } else if (level == "INFO") {
                  PY_LOG_INFO("{}", message.c_str());
              } else if (level == "WARNING") {
                  PY_LOG_WARNING("{}", message.c_str());
              } else if (level == "ERROR") {
                  PY_LOG_ERROR("{}", message.c_str());
              } else if (level == "CRITICAL") {
                  PY_LOG_CRITICAL("{}", message.c_str());
              } else {
                  PY_LOG_INFO("{}", message.c_str());  // Default to INFO
              } }, nb::arg("level"), nb::arg("message"), "Send a log message to the engine logger with specified level");
}

}  // namespace EngineScripts
