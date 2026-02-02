#include <corona/events/engine_events.h>
#include <corona/events/mechanics_system_events.h>
#include <corona/kernel/core/i_logger.h>
#include <corona/kernel/event/i_event_bus.h>
#include <corona/kernel/event/i_event_stream.h>
#include <corona/systems/mechanics/mechanics_system.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

#include "corona/shared_data_hub.h"
#include "ktm/ktm.h"

namespace {

// 八叉树潜在碰撞对；窄相位仍用 AABB 重叠 + 分离
struct OctreeEntry {
    std::uintptr_t handle;
    ktm::fvec3 min_bounds;
    ktm::fvec3 max_bounds;
};

struct OctreeNode {
    ktm::fvec3 min_bounds;
    ktm::fvec3 max_bounds;
    std::vector<OctreeEntry> entries;
    std::unique_ptr<std::array<OctreeNode, 8>> children;
};

constexpr int kOctreeMaxDepth = 6;           // 八叉树最大深度为6
constexpr int kOctreeMaxObjectsPerLeaf = 4;  // 八叉树每个叶子节点最大物体数为4

inline bool aabb_overlap(const ktm::fvec3& a_min, const ktm::fvec3& a_max,  // 两个AABB的包围盒
                         const ktm::fvec3& b_min, const ktm::fvec3& b_max) {
    return (a_min.x <= b_max.x && a_max.x >= b_min.x) &&
           (a_min.y <= b_max.y && a_max.y >= b_min.y) &&
           (a_min.z <= b_max.z && a_max.z >= b_min.z);
}

void octree_init_children(OctreeNode& node) {  // 初始化八叉树的子节点
    node.children = std::make_unique<std::array<OctreeNode, 8>>();
    const float cx = (node.min_bounds.x + node.max_bounds.x) * 0.5f;  // 计算中心点
    const float cy = (node.min_bounds.y + node.max_bounds.y) * 0.5f;
    const float cz = (node.min_bounds.z + node.max_bounds.z) * 0.5f;
    const ktm::fvec3 mid{cx, cy, cz};
    const ktm::fvec3& mn = node.min_bounds;  // 计算最小点
    const ktm::fvec3& mx = node.max_bounds;  // 计算最大点

    (*node.children)[0].min_bounds = mn;   // 设置第一个子节点的最小点
    (*node.children)[0].max_bounds = mid;  // 设置第一个子节点的最大点

    (*node.children)[1].min_bounds = {cx, mn.y, mn.z};  // 设置第二个子节点的最小点
    (*node.children)[1].max_bounds = {mx.x, cy, cz};    // 设置第二个子节点的最大点

    (*node.children)[2].min_bounds = {mn.x, cy, mn.z};  // 设置第三个子节点的最小点
    (*node.children)[2].max_bounds = {cx, mx.y, cz};    // 设置第三个子节点的最大点

    (*node.children)[3].min_bounds = {cx, cy, mn.z};    // 设置第四个子节点的最小点
    (*node.children)[3].max_bounds = {mx.x, mx.y, cz};  // 设置第四个子节点的最大点

    (*node.children)[4].min_bounds = {mn.x, mn.y, cz};  // 设置第五个子节点的最小点
    (*node.children)[4].max_bounds = {cx, cy, mx.z};    // 设置第五个子节点的最大点

    (*node.children)[5].min_bounds = {cx, mn.y, cz};    // 设置第六个子节点的最小点
    (*node.children)[5].max_bounds = {mx.x, cy, mx.z};  // 设置第六个子节点的最大点

    (*node.children)[6].min_bounds = {mn.x, cy, cz};    // 设置第七个子节点的最小点
    (*node.children)[6].max_bounds = {cx, mx.y, mx.z};  // 设置第七个子节点的最大点

    (*node.children)[7].min_bounds = mid;  // 设置第八个子节点的最小点
    (*node.children)[7].max_bounds = mx;   // 设置第八个子节点的最大点
}

void octree_insert(OctreeNode& node, std::uintptr_t handle,  // 插入物体到八叉树
                   const ktm::fvec3& obj_min, const ktm::fvec3& obj_max, int depth) {
    if (!aabb_overlap(obj_min, obj_max, node.min_bounds, node.max_bounds)) {  // 如果物体与八叉树的包围盒不相交，则返回
        return;
    }

    const bool is_leaf = (node.children == nullptr);  // 如果节点没有子节点，则表示是叶子节点（为空）

    if (is_leaf) {
        const bool should_split =                                               // 如果深度小于最大深度，并且叶子节点中的物体数大于等于最大物体数，则需要分割
            depth < kOctreeMaxDepth &&                                          // 深度小于最大深度
            static_cast<int>(node.entries.size()) >= kOctreeMaxObjectsPerLeaf;  // 叶子节点中的物体数大于等于最大物体数

        if (!should_split) {                                     // 不分割
            node.entries.push_back({handle, obj_min, obj_max});  // 直接插入
            return;
        }

        octree_init_children(node);  // 初始化子节点

        for (const OctreeEntry& e : node.entries) {  // 遍历叶子节点中的物体
            for (int i = 0; i < 8; ++i) {
                octree_insert((*node.children)[i], e.handle, e.min_bounds, e.max_bounds, depth + 1);
            }  // 插入子节点
        }
        node.entries.clear();

        for (int i = 0; i < 8; ++i) {
            octree_insert((*node.children)[i], handle, obj_min, obj_max, depth + 1);
        }
        return;
    }

    const float cx = (node.min_bounds.x + node.max_bounds.x) * 0.5f;
    const float cy = (node.min_bounds.y + node.max_bounds.y) * 0.5f;
    const float cz = (node.min_bounds.z + node.max_bounds.z) * 0.5f;
    const ktm::fvec3 mid{cx, cy, cz};
    const ktm::fvec3& mn = node.min_bounds;
    const ktm::fvec3& mx = node.max_bounds;

    const bool overlap[8] = {
        aabb_overlap(obj_min, obj_max, mn, mid),
        aabb_overlap(obj_min, obj_max, ktm::fvec3{cx, mn.y, mn.z}, ktm::fvec3{mx.x, cy, cz}),
        aabb_overlap(obj_min, obj_max, ktm::fvec3{mn.x, cy, mn.z}, ktm::fvec3{cx, mx.y, cz}),
        aabb_overlap(obj_min, obj_max, ktm::fvec3{cx, cy, mn.z}, ktm::fvec3{mx.x, mx.y, cz}),
        aabb_overlap(obj_min, obj_max, ktm::fvec3{mn.x, mn.y, cz}, ktm::fvec3{cx, cy, mx.z}),
        aabb_overlap(obj_min, obj_max, ktm::fvec3{cx, mn.y, cz}, ktm::fvec3{mx.x, cy, mx.z}),
        aabb_overlap(obj_min, obj_max, ktm::fvec3{mn.x, cy, cz}, ktm::fvec3{cx, mx.y, mx.z}),
        aabb_overlap(obj_min, obj_max, mid, mx),
    };

    for (int i = 0; i < 8; ++i) {
        if (overlap[i]) {
            octree_insert((*node.children)[i], handle, obj_min, obj_max, depth + 1);
        }
    }
}

void octree_collect_pairs(const OctreeNode& node,
                          std::vector<std::pair<std::uintptr_t, std::uintptr_t>>& out) {
    if (node.children) {
        for (int i = 0; i < 8; ++i) {
            octree_collect_pairs((*node.children)[i], out);
        }
        return;
    }

    for (std::size_t i = 0; i < node.entries.size(); ++i) {
        for (std::size_t j = i + 1; j < node.entries.size(); ++j) {
            std::uintptr_t a = node.entries[i].handle;
            std::uintptr_t b = node.entries[j].handle;
            if (a > b) std::swap(a, b);
            out.emplace_back(a, b);
        }
    }
}

void octree_dedupe_pairs(std::vector<std::pair<std::uintptr_t, std::uintptr_t>>& pairs) {
    if (pairs.empty()) return;
    std::sort(pairs.begin(), pairs.end());
    pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
}

struct MechanicsWorldAABB {
    std::uintptr_t handle;
    std::uintptr_t transform_handle;
    ktm::fvec3 min_world;
    ktm::fvec3 max_world;
    ktm::fvec3 center_world;
};

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

    // 1. 通过 Scene -> Actor -> Profile 收集 Mechanics 句柄并去重
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
    std::sort(mechanics_handles.begin(), mechanics_handles.end());
    mechanics_handles.erase(std::unique(mechanics_handles.begin(), mechanics_handles.end()), mechanics_handles.end());
    if (mechanics_handles.size() < 2) {
        return;
    }

    // 2. 单遍计算所有物体的世界 AABB，并缓存 transform_handle 供分离写回使用
    std::vector<MechanicsWorldAABB> mechanics_data;
    mechanics_data.reserve(mechanics_handles.size());
    std::unordered_map<std::uintptr_t, std::size_t> handle_to_index;

    for (std::uintptr_t h : mechanics_handles) {
        auto m_acc = mechanics_storage.acquire_read(h);
        if (!m_acc) continue;
        const auto& m = *m_acc;
        auto geom_acc = geometry_storage.acquire_read(m.geometry_handle);
        if (!geom_acc) continue;
        auto tx_acc = transform_storage.acquire_read(geom_acc->transform_handle);
        if (!tx_acc) continue;

        const auto& t = *tx_acc;
        ktm::fvec3 c_local;
        c_local.x = (m.min_xyz.x + m.max_xyz.x) * 0.5f;
        c_local.y = (m.min_xyz.y + m.max_xyz.y) * 0.5f;
        c_local.z = (m.min_xyz.z + m.max_xyz.z) * 0.5f;
        ktm::fvec3 e_local;
        e_local.x = (m.max_xyz.x - m.min_xyz.x) * 0.5f;
        e_local.y = (m.max_xyz.y - m.min_xyz.y) * 0.5f;
        e_local.z = (m.max_xyz.z - m.min_xyz.z) * 0.5f;

        MechanicsWorldAABB entry;
        entry.handle = h;
        entry.transform_handle = geom_acc->transform_handle;
        entry.center_world.x = c_local.x + t.position.x;
        entry.center_world.y = c_local.y + t.position.y;
        entry.center_world.z = c_local.z + t.position.z;
        ktm::fvec3 e_world;
        e_world.x = std::abs(e_local.x * t.scale.x);
        e_world.y = std::abs(e_local.y * t.scale.y);
        e_world.z = std::abs(e_local.z * t.scale.z);
        entry.min_world.x = entry.center_world.x - e_world.x;
        entry.min_world.y = entry.center_world.y - e_world.y;
        entry.min_world.z = entry.center_world.z - e_world.z;
        entry.max_world.x = entry.center_world.x + e_world.x;
        entry.max_world.y = entry.center_world.y + e_world.y;
        entry.max_world.z = entry.center_world.z + e_world.z;

        handle_to_index[h] = mechanics_data.size();
        mechanics_data.push_back(entry);
    }
    if (mechanics_data.size() < 2) {
        return;
    }

    // 3. 构建八叉树：根节点 AABB = 全体物体的包围盒（加 padding）
    ktm::fvec3 root_min = mechanics_data[0].min_world;
    ktm::fvec3 root_max = mechanics_data[0].max_world;
    for (const auto& e : mechanics_data) {
        root_min.x = std::min(root_min.x, e.min_world.x);
        root_min.y = std::min(root_min.y, e.min_world.y);
        root_min.z = std::min(root_min.z, e.min_world.z);
        root_max.x = std::max(root_max.x, e.max_world.x);
        root_max.y = std::max(root_max.y, e.max_world.y);
        root_max.z = std::max(root_max.z, e.max_world.z);
    }
    constexpr float pad = 0.01f;
    root_min.x -= pad;
    root_min.y -= pad;
    root_min.z -= pad;
    root_max.x += pad;
    root_max.y += pad;
    root_max.z += pad;

    OctreeNode octree_root;
    octree_root.min_bounds = root_min;
    octree_root.max_bounds = root_max;
    for (const auto& e : mechanics_data) {
        octree_insert(octree_root, e.handle, e.min_world, e.max_world, 0);
    }

    // 4. 八叉树 broad-phase：收集潜在碰撞对并去重
    std::vector<std::pair<std::uintptr_t, std::uintptr_t>> pairs;
    pairs.reserve(mechanics_data.size() * 4);
    octree_collect_pairs(octree_root, pairs);
    octree_dedupe_pairs(pairs);

    // 5. 窄相位：仅对候选对做 AABB 重叠检测 + 分离
    constexpr float eps = 1e-6f;
    constexpr float separation = 0.02f;
    constexpr float bounce_strength = 0.1f;

    for (const auto& pr : pairs) {
        const std::uintptr_t ha = pr.first;
        const std::uintptr_t hb = pr.second;
        auto it_a = handle_to_index.find(ha);
        auto it_b = handle_to_index.find(hb);
        if (it_a == handle_to_index.end() || it_b == handle_to_index.end()) continue;

        const MechanicsWorldAABB& a = mechanics_data[it_a->second];
        const MechanicsWorldAABB& b = mechanics_data[it_b->second];

        if (!aabb_overlap(a.min_world, a.max_world, b.min_world, b.max_world)) {
            continue;
        }

        ktm::fvec3 diff;
        diff.x = b.center_world.x - a.center_world.x;
        diff.y = b.center_world.y - a.center_world.y;
        diff.z = b.center_world.z - a.center_world.z;
        if (ktm::length(diff) < eps) {
            diff.x = 1.0f;
            diff.y = 0.0f;
            diff.z = 0.0f;
        }
        ktm::fvec3 normal = ktm::normalize(diff);

        ktm::fvec3 total_offset;
        total_offset.x = normal.x * (separation + bounce_strength);
        total_offset.y = normal.y * (separation + bounce_strength);
        total_offset.z = normal.z * (separation + bounce_strength);

        if (auto txa = transform_storage.acquire_write(a.transform_handle)) {
            txa->position.x -= total_offset.x;
            txa->position.y -= total_offset.y;
            txa->position.z -= total_offset.z;
        }
        if (auto txb = transform_storage.acquire_write(b.transform_handle)) {
            txb->position.x += total_offset.x;
            txb->position.y += total_offset.y;
            txb->position.z += total_offset.z;
        }
    }
}

void MechanicsSystem::shutdown() {
    CFW_LOG_NOTICE("MechanicsSystem: Shutting down...");
}
}  // namespace Corona::Systems
