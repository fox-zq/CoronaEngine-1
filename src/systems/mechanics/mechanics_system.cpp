#include <algorithm>
#include <cmath>
#include <vector>

#include <corona/events/engine_events.h>
#include <corona/events/mechanics_system_events.h>
#include <corona/kernel/core/i_logger.h>
#include <corona/kernel/event/i_event_bus.h>
#include <corona/kernel/event/i_event_stream.h>
#include <corona/systems/mechanics/mechanics_system.h>

#include "corona/shared_data_hub.h"
#include "ktm/ktm.h"

namespace {
// 说明：旧的顶点级检测暂时保留（当前实现使用更稳定的世界空间 AABB overlap），后续可清理。
std::vector<ktm::fvec3> calculateVertices(const ktm::fvec3& startMin, const ktm::fvec3& startMax) {
    std::vector<ktm::fvec3> vertices;
    vertices.reserve(8);

    vertices.push_back(startMin);

    ktm::fvec3 v1;
    v1.x = startMax.x;
    v1.y = startMin.y;
    v1.z = startMin.z;
    vertices.push_back(v1);

    ktm::fvec3 v2;
    v2.x = startMin.x;
    v2.y = startMax.y;
    v2.z = startMin.z;
    vertices.push_back(v2);

    ktm::fvec3 v3;
    v3.x = startMax.x;
    v3.y = startMax.y;
    v3.z = startMin.z;
    vertices.push_back(v3);

    ktm::fvec3 v4;
    v4.x = startMin.x;
    v4.y = startMin.y;
    v4.z = startMax.z;
    vertices.push_back(v4);

    ktm::fvec3 v5;
    v5.x = startMax.x;
    v5.y = startMin.y;
    v5.z = startMax.z;
    vertices.push_back(v5);

    ktm::fvec3 v6;
    v6.x = startMin.x;
    v6.y = startMax.y;
    v6.z = startMax.z;
    vertices.push_back(v6);

    vertices.push_back(startMax);

    return vertices;
}

bool checkCollision(const std::vector<ktm::fvec3>& vertices1, const std::vector<ktm::fvec3>& vertices2) {
    // 计算vertices2的AABB
    ktm::fvec3 min2 = vertices2[0], max2 = vertices2[0];
    for (const auto& v : vertices2) {
        min2 = ktm::min(min2, v);
        max2 = ktm::max(max2, v);
    }

    // 检查vertices1的顶点是否在vertices2的AABB内
    for (const auto& point : vertices1) {
        if (point.x >= min2.x && point.x <= max2.x &&
            point.y >= min2.y && point.y <= max2.y &&
            point.z >= min2.z && point.z <= max2.z) {
            return true;
        }
    }

    // 计算vertices1的AABB
    ktm::fvec3 min1 = vertices1[0], max1 = vertices1[0];
    for (const auto& v : vertices1) {
        min1 = ktm::min(min1, v);
        max1 = ktm::max(max1, v);
    }

    // 检查vertices2的顶点是否在vertices1的AABB内
    for (const auto& point : vertices2) {
        if (point.x >= min1.x && point.x <= max1.x &&
            point.y >= min1.y && point.y <= max1.y &&
            point.z >= min1.z && point.z <= max1.z) {
            return true;
        }
    }

    return false;
}
}  // namespace

namespace Corona::Systems {
bool MechanicsSystem::initialize(Kernel::ISystemContext* ctx) {
    CFW_LOG_NOTICE("MechanicsSystem: Initializing...");
    return true;
}

void MechanicsSystem::update() {
    update_physics();
}

void MechanicsSystem::update_physics() {
    auto& mechanics_storage = SharedDataHub::instance().mechanics_storage();
    auto& geometry_storage = SharedDataHub::instance().geometry_storage();
    auto& transform_storage = SharedDataHub::instance().model_transform_storage();
    auto& scene_storage = SharedDataHub::instance().scene_storage();
    auto& actor_storage = SharedDataHub::instance().actor_storage();
    auto& profile_storage = SharedDataHub::instance().profile_storage();

    // 通过 Scene -> Actor -> Profile 链路获取所有 MechanicsDevice 句柄
    std::vector<std::uintptr_t> mechanics_handles;
    mechanics_handles.reserve(64);

    for (const auto& scene : scene_storage) {
        for (auto actor_handle : scene.actor_handles) {
            if (auto actor = actor_storage.acquire_read(actor_handle)) {
                for (auto profile_handle : actor->profile_handles) {
                    if (auto profile = profile_storage.acquire_read(profile_handle)) {
                        if (profile->mechanics_handle != 0) {
                            mechanics_handles.push_back(profile->mechanics_handle);
                        }
                    }
                }
            }
        }
    }

    if (mechanics_handles.size() < 2) {
        return;
    }

    // 去重，避免同一个 handle 被多次引用
    std::sort(mechanics_handles.begin(), mechanics_handles.end());
    mechanics_handles.erase(std::unique(mechanics_handles.begin(), mechanics_handles.end()), mechanics_handles.end());

    if (mechanics_handles.size() < 2) {
        return;
    }

    // 成对检测：i < j，避免 A-B / B-A 重复
    for (std::size_t i = 0; i < mechanics_handles.size(); ++i) {
        const std::uintptr_t handle1 = mechanics_handles[i];
        auto m1_accessor = mechanics_storage.acquire_read(handle1);
        if (!m1_accessor) {
            continue;
        }
        const auto& m1 = *m1_accessor;

        for (std::size_t j = i + 1; j < mechanics_handles.size(); ++j) {
            const std::uintptr_t handle2 = mechanics_handles[j];
            auto m2_accessor = mechanics_storage.acquire_read(handle2);
            if (!m2_accessor) {
                continue;
            }
            const auto& m2 = *m2_accessor;

            // 世界空间 AABB（最小可用版本：只考虑平移/缩放，不考虑旋转）
            bool found1 = false;
            bool found2 = false;

            ktm::fvec3 min1_world;
            ktm::fvec3 max1_world;
            ktm::fvec3 min2_world;
            ktm::fvec3 max2_world;

            ktm::fvec3 center1_world;
            center1_world.x = 0.0f;
            center1_world.y = 0.0f;
            center1_world.z = 0.0f;

            ktm::fvec3 center2_world;
            center2_world.x = 0.0f;
            center2_world.y = 0.0f;
            center2_world.z = 0.0f;

            // m1
            if (auto geom1_accessor = geometry_storage.acquire_read(m1.geometry_handle)) {
                const auto& geom1 = *geom1_accessor;
                if (auto transform1_accessor = transform_storage.acquire_read(geom1.transform_handle)) {
                    const auto& t1 = *transform1_accessor;

                    ktm::fvec3 c1_local;
                    c1_local.x = (m1.min_xyz.x + m1.max_xyz.x) * 0.5f;
                    c1_local.y = (m1.min_xyz.y + m1.max_xyz.y) * 0.5f;
                    c1_local.z = (m1.min_xyz.z + m1.max_xyz.z) * 0.5f;

                    ktm::fvec3 e1_local;
                    e1_local.x = (m1.max_xyz.x - m1.min_xyz.x) * 0.5f;
                    e1_local.y = (m1.max_xyz.y - m1.min_xyz.y) * 0.5f;
                    e1_local.z = (m1.max_xyz.z - m1.min_xyz.z) * 0.5f;

                    center1_world.x = c1_local.x + t1.position.x;
                    center1_world.y = c1_local.y + t1.position.y;
                    center1_world.z = c1_local.z + t1.position.z;

                    ktm::fvec3 e1_world;
                    e1_world.x = std::abs(e1_local.x * t1.scale.x);
                    e1_world.y = std::abs(e1_local.y * t1.scale.y);
                    e1_world.z = std::abs(e1_local.z * t1.scale.z);

                    min1_world.x = center1_world.x - e1_world.x;
                    min1_world.y = center1_world.y - e1_world.y;
                    min1_world.z = center1_world.z - e1_world.z;
                    max1_world.x = center1_world.x + e1_world.x;
                    max1_world.y = center1_world.y + e1_world.y;
                    max1_world.z = center1_world.z + e1_world.z;

                    found1 = true;
                }
            }

            if (!found1) {
                continue;
            }

            // m2
            if (auto geom2_accessor = geometry_storage.acquire_read(m2.geometry_handle)) {
                const auto& geom2 = *geom2_accessor;
                if (auto transform2_accessor = transform_storage.acquire_read(geom2.transform_handle)) {
                    const auto& t2 = *transform2_accessor;

                    ktm::fvec3 c2_local;
                    c2_local.x = (m2.min_xyz.x + m2.max_xyz.x) * 0.5f;
                    c2_local.y = (m2.min_xyz.y + m2.max_xyz.y) * 0.5f;
                    c2_local.z = (m2.min_xyz.z + m2.max_xyz.z) * 0.5f;

                    ktm::fvec3 e2_local;
                    e2_local.x = (m2.max_xyz.x - m2.min_xyz.x) * 0.5f;
                    e2_local.y = (m2.max_xyz.y - m2.min_xyz.y) * 0.5f;
                    e2_local.z = (m2.max_xyz.z - m2.min_xyz.z) * 0.5f;

                    center2_world.x = c2_local.x + t2.position.x;
                    center2_world.y = c2_local.y + t2.position.y;
                    center2_world.z = c2_local.z + t2.position.z;

                    ktm::fvec3 e2_world;
                    e2_world.x = std::abs(e2_local.x * t2.scale.x);
                    e2_world.y = std::abs(e2_local.y * t2.scale.y);
                    e2_world.z = std::abs(e2_local.z * t2.scale.z);

                    min2_world.x = center2_world.x - e2_world.x;
                    min2_world.y = center2_world.y - e2_world.y;
                    min2_world.z = center2_world.z - e2_world.z;
                    max2_world.x = center2_world.x + e2_world.x;
                    max2_world.y = center2_world.y + e2_world.y;
                    max2_world.z = center2_world.z + e2_world.z;

                    found2 = true;
                }
            }

            if (!found2) {
                continue;
            }

            const bool overlap = (min1_world.x <= max2_world.x && max1_world.x >= min2_world.x) &&
                                 (min1_world.y <= max2_world.y && max1_world.y >= min2_world.y) &&
                                 (min1_world.z <= max2_world.z && max1_world.z >= min2_world.z);

            if (!overlap) {
                continue;
            }

            // 计算分离方向（用世界中心，避免局部中心导致方向不对）
            ktm::fvec3 diff;
            diff.x = center2_world.x - center1_world.x;
            diff.y = center2_world.y - center1_world.y;
            diff.z = center2_world.z - center1_world.z;

            constexpr float eps = 1e-6f;
            if (ktm::length(diff) < eps) {
                diff.x = 1.0f;
                diff.y = 0.0f;
                diff.z = 0.0f;
            }

            ktm::fvec3 normal = ktm::normalize(diff);

            constexpr float separation = 0.02f;
            constexpr float bounce_strength = 0.1f;

            ktm::fvec3 total_offset;
            total_offset.x = normal.x * (separation + bounce_strength);
            total_offset.y = normal.y * (separation + bounce_strength);
            total_offset.z = normal.z * (separation + bounce_strength);

            // 写回 transform
            if (auto geom1_accessor = geometry_storage.acquire_read(m1.geometry_handle)) {
                const auto& geom1 = *geom1_accessor;
                if (auto transform1_accessor = transform_storage.acquire_write(geom1.transform_handle)) {
                    auto& transform1 = *transform1_accessor;
                    transform1.position.x -= total_offset.x;
                    transform1.position.y -= total_offset.y;
                    transform1.position.z -= total_offset.z;
                }
            }

            if (auto geom2_accessor = geometry_storage.acquire_read(m2.geometry_handle)) {
                const auto& geom2 = *geom2_accessor;
                if (auto transform2_accessor = transform_storage.acquire_write(geom2.transform_handle)) {
                    auto& transform2 = *transform2_accessor;
                    transform2.position.x += total_offset.x;
                    transform2.position.y += total_offset.y;
                    transform2.position.z += total_offset.z;
                }
            }
        }
    }
}

void MechanicsSystem::shutdown() {
    CFW_LOG_NOTICE("MechanicsSystem: Shutting down...");
}
}  // namespace Corona::Systems

