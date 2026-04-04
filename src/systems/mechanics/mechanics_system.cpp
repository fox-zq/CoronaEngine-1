



#include <corona/events/engine_events.h>
#include <corona/events/mechanics_system_events.h>
#include <corona/kernel/core/i_logger.h>
#include <corona/kernel/event/i_event_bus.h>
#include <corona/kernel/event/i_event_stream.h>
#include <corona/systems/mechanics/mechanics_system.h>
#include <set>
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
// Note: do not depend on nanobind in the mechanics system. Callbacks provided
// from the scripting layer are expected to manage GIL acquisition themselves.

namespace {

struct PairHash {
    std::size_t operator()(const std::pair<std::uintptr_t, std::uintptr_t>& p) const noexcept {
        // combine hashes
        std::size_t h1 = std::hash<std::uintptr_t>{}(p.first);
        std::size_t h2 = std::hash<std::uintptr_t>{}(p.second);
        return h1 ^ (h2 << 1);
    }
};

// Persistent set of active collision pairs from previous frame
static std::unordered_set<std::pair<std::uintptr_t, std::uintptr_t>, PairHash> g_prev_active_collisions;



constexpr ktm::fvec3 make_fvec3(float x, float y, float z) {
    ktm::fvec3 result;
    result.x = x;
    result.y = y;
    result.z = z;
    return result;
}

/*
 八叉树节点中的物体数据结构
 存储物体句柄和AABB包围盒
 */
struct OctreeEntry {
    std::uintptr_t handle;        // 物体唯一标识句柄
    ktm::fvec3 min_bounds;        // AABB包围盒最小边界
    ktm::fvec3 max_bounds;        // AABB包围盒最大边界
};

/*
 八叉树节点结构
 八叉树是节点包含8个子节点
 */
struct OctreeNode {
    ktm::fvec3 min_bounds;                                    // 当前节点的AABB最小边界
    ktm::fvec3 max_bounds;                                    // 当前节点的AABB最大边界
    std::vector<OctreeEntry> entries;                         // 叶子节点存储的物体列表
    std::unique_ptr<std::array<OctreeNode, 8>> children;      // 子节点（8个，非叶子节点才有）
};

//八叉树常量
constexpr int kOctreeMaxDepth = 6;            // 八叉树最大深度（防止过深）
constexpr int kOctreeMaxObjectsPerLeaf = 4;   // 叶子节点最大物体数（超过分裂）

/*
 检测两个AABB包围盒是否重叠
 a_min A物体AABB最小边界
 a_max A物体AABB最大边界
 b_min B物体AABB最小边界
 b_max B物体AABB最大边界
*/
inline bool aabb_overlap(const ktm::fvec3& a_min, const ktm::fvec3& a_max,
                         const ktm::fvec3& b_min, const ktm::fvec3& b_max) {
    // 三个轴都有重叠才视为重叠
    return (a_min.x <= b_max.x && a_max.x >= b_min.x) &&
           (a_min.y <= b_max.y && a_max.y >= b_min.y) &&
           (a_min.z <= b_max.z && a_max.z >= b_min.z);
}

/*
  初始化八叉树节点的8个子节点
 */
void octree_init_children(OctreeNode& node) {
    //创建8个子节点的空间
    node.children = std::make_unique<std::array<OctreeNode, 8>>();
    auto& children = *node.children;

    //计算父节点的坐标（作为子节点的分割点）
    const ktm::fvec3 center = make_fvec3(
        (node.min_bounds.x + node.max_bounds.x) * 0.5f,
        (node.min_bounds.y + node.max_bounds.y) * 0.5f,
        (node.min_bounds.z + node.max_bounds.z) * 0.5f
    );
    const auto& min = node.min_bounds;
    const auto& max = node.max_bounds;

    //初始化8个子节点的AABB边界
    children[0].min_bounds = min;
    children[0].max_bounds = center;

    children[1].min_bounds = make_fvec3(center.x, min.y, min.z);
    children[1].max_bounds = make_fvec3(max.x, center.y, center.z);

    children[2].min_bounds = make_fvec3(min.x, center.y, min.z);
    children[2].max_bounds = make_fvec3(center.x, max.y, center.z);

    children[3].min_bounds = make_fvec3(center.x, center.y, min.z);
    children[3].max_bounds = make_fvec3(max.x, max.y, center.z);

    children[4].min_bounds = make_fvec3(min.x, min.y, center.z);
    children[4].max_bounds = make_fvec3(center.x, center.y, max.z);

    children[5].min_bounds = make_fvec3(center.x, min.y, center.z);
    children[5].max_bounds = make_fvec3(max.x, center.y, max.z);

    children[6].min_bounds = make_fvec3(min.x, center.y, center.z);
    children[6].max_bounds = make_fvec3(center.x, max.y, max.z);

    children[7].min_bounds = center;
    children[7].max_bounds = max;
}


void octree_insert(OctreeNode& node, std::uintptr_t handle,
                   const ktm::fvec3& obj_min, const ktm::fvec3& obj_max, int depth) {
    //物体不在当前节点范围内，return
    if (!aabb_overlap(obj_min, obj_max, node.min_bounds, node.max_bounds)) {
        return;
    }

    //判断节点是否为叶子节点（
    const bool is_leaf = (node.children == nullptr);

    if (is_leaf) {
        //检查分裂：深度未到最大值和物体数超过阈值
        const bool should_split =
            depth < kOctreeMaxDepth &&
            static_cast<int>(node.entries.size()) >= kOctreeMaxObjectsPerLeaf;

        //不分裂直接将物体加入当前叶子节点
        if (!should_split) {
            node.entries.push_back({handle, obj_min, obj_max});
            return;
        }
        octree_init_children(node);

        //将当前节点的物体重新分配到子节点中
        for (const OctreeEntry& e : node.entries) {
            for (int i = 0; i < 8; ++i) {
                octree_insert((*node.children)[i], e.handle, e.min_bounds, e.max_bounds, depth + 1);
            }
        }
        node.entries.clear(); //清空转移到子节点

        //将新物体插入到子节点中
        for (int i = 0; i < 8; ++i) {
            octree_insert((*node.children)[i], handle, obj_min, obj_max, depth + 1);
        }
        return;
    }

    //非叶子节点计算中心并判断物体属于哪些子节点
    const ktm::fvec3 center = make_fvec3(
        (node.min_bounds.x + node.max_bounds.x) * 0.5f,
        (node.min_bounds.y + node.max_bounds.y) * 0.5f,
        (node.min_bounds.z + node.max_bounds.z) * 0.5f
    );

    const ktm::fvec3& min_bounds = node.min_bounds;
    const ktm::fvec3& max_bounds = node.max_bounds;

    //检测物体与8个子节点的重叠情况
    const bool overlap[8] = {
        aabb_overlap(obj_min, obj_max, min_bounds, center),
        aabb_overlap(obj_min, obj_max,
                     make_fvec3(center.x, min_bounds.y, min_bounds.z),
                     make_fvec3(max_bounds.x, center.y, center.z)),
        aabb_overlap(obj_min, obj_max,
                     make_fvec3(min_bounds.x, center.y, min_bounds.z),
                     make_fvec3(center.x, max_bounds.y, center.z)),
        aabb_overlap(obj_min, obj_max,
                     make_fvec3(center.x, center.y, min_bounds.z),
                     make_fvec3(max_bounds.x, max_bounds.y, center.z)),
        aabb_overlap(obj_min, obj_max,
                     make_fvec3(min_bounds.x, min_bounds.y, center.z),
                     make_fvec3(center.x, center.y, max_bounds.z)),
        aabb_overlap(obj_min, obj_max,
                     make_fvec3(center.x, min_bounds.y, center.z),
                     make_fvec3(max_bounds.x, center.y, max_bounds.z)),
        aabb_overlap(obj_min, obj_max,
                     make_fvec3(min_bounds.x, center.y, center.z),
                     make_fvec3(center.x, max_bounds.y, max_bounds.z)),
        aabb_overlap(obj_min, obj_max, center, max_bounds)
    };

    //将物体插入到所有重叠的子节点中
    for (int i = 0; i < 8; ++i) {
        if (overlap[i]) {
            octree_insert((*node.children)[i], handle, obj_min, obj_max, depth + 1);
        }
    }
}


void octree_collect_pairs(const OctreeNode& node,
                          std::vector<std::pair<std::uintptr_t, std::uintptr_t>>& out) {
    //非叶子节点：递归遍历子节点
    if (node.children) {
        for (int i = 0; i < 8; ++i) {
            octree_collect_pairs((*node.children)[i], out);
        }
        return;
    }

    //叶子节点：生成所有物体对（i<j，避免重复）
    for (std::size_t i = 0; i < node.entries.size(); ++i) {
        for (std::size_t j = i + 1; j < node.entries.size(); ++j) {
            std::uintptr_t a = node.entries[i].handle;
            std::uintptr_t b = node.entries[j].handle;
            if (a > b) std::swap(a, b); // 保证a<=b，统一碰撞对顺序
            out.emplace_back(a, b);
        }
    }
}


void octree_dedupe_pairs(std::vector<std::pair<std::uintptr_t, std::uintptr_t>>& pairs) {
    if (pairs.empty()) return;
    //排序后去重
    std::sort(pairs.begin(), pairs.end());
    pairs.erase(std::unique(pairs.begin(), pairs.end()), pairs.end());
}


struct MechanicsWorldAABB {
    std::uintptr_t handle;            //物体句柄
    std::uintptr_t transform_handle;  //物体变换句柄（用于更新位置）
    ktm::fvec3 min_world;             //世界AABB最小边界
    ktm::fvec3 max_world;             //世界AABB最大边界
    ktm::fvec3 center_world;          //世界AABB中心
};

}  // anonymous namespace

namespace Corona::Systems {

bool MechanicsSystem::initialize(Kernel::ISystemContext* ctx) {
    CFW_LOG_NOTICE("MechanicsSystem: Initializing...");
    return true;
}

void MechanicsSystem::update() {
    update_physics(); // 每帧调用物理更新
}

void MechanicsSystem::shutdown() {
    CFW_LOG_NOTICE("MechanicsSystem: Shutting down...");
}


void MechanicsSystem::update_physics() {
    const float floor_eps = 0.01f;  //地板碰撞容差（防止抖动）

    //存储物体的速度质量阻尼
    std::unordered_map<std::uintptr_t, ktm::fvec3> handle_to_velocity; // 物体句柄→速度
    std::unordered_map<std::uintptr_t, float> handle_to_mass;         // 物体句柄→质量
    std::unordered_map<std::uintptr_t, float> handle_to_damping;      // 物体句柄→阻尼
    std::unordered_map<std::uintptr_t, float> handle_to_restitution;  // 物体句柄→反弹系数
    std::unordered_map<std::uintptr_t, std::uintptr_t> mech_to_actor;  // mechanics_handle -> actor_handle


    auto& mechanics_storage = SharedDataHub::instance().mechanics_storage();
    auto& geometry_storage = SharedDataHub::instance().geometry_storage();
    auto& transform_storage = SharedDataHub::instance().model_transform_storage();
    auto& scene_storage = SharedDataHub::instance().scene_storage();
    auto& actor_storage = SharedDataHub::instance().actor_storage();
    auto& profile_storage = SharedDataHub::instance().profile_storage();
    auto& environment_storage = SharedDataHub::instance().environment_storage();

    // 场景级物理参数（从第一个有效 EnvironmentDevice 读取，缺省使用默认值）
    float fixed_dt          = 1.0f / 60.0f;
    ktm::fvec3 gravity      = make_fvec3(0.0f, -9.8f, 0.0f);
    float floor_restitution = 0.6f;
    float floor_y           = 0.0f;

    std::vector<std::uintptr_t> mechanics_handles;
    mechanics_handles.reserve(64);

    std::vector<std::uintptr_t> scene_handles;
    scene_handles.reserve(4);

    //遍历所有场景所有物体
    for (const auto& scene : scene_storage) {
        scene_handles.push_back(reinterpret_cast<std::uintptr_t>(&scene));
        // 从场景的 EnvironmentDevice 读取物理参数
        if (scene.environment != 0) {
            if (auto env = environment_storage.acquire_read(scene.environment)) {
                gravity           = env->gravity;
                floor_y           = env->floor_y;
                floor_restitution = env->floor_restitution;
                fixed_dt          = env->fixed_dt;
            }
        }

        for (auto actor_handle : scene.actor_handles) {
            if (auto actor = actor_storage.acquire_read(actor_handle)) {
                for (auto profile_handle : actor->profile_handles) {
                    if (auto profile = profile_storage.acquire_read(profile_handle)) {
                        //过滤有效物理物体
                        if (profile->mechanics_handle != 0) {
                            mechanics_handles.push_back(profile->mechanics_handle);
                            // record mapping from mechanics handle to its owning actor handle
                            mech_to_actor[profile->mechanics_handle] = actor_handle;
                            //初始化速度
                            handle_to_velocity[profile->mechanics_handle] = make_fvec3(0.0f, 0.0f, 0.0f);
                            // 从 MechanicsDevice 读取物体级参数
                            if (auto m_acc = mechanics_storage.acquire_read(profile->mechanics_handle)) {
                                handle_to_mass[profile->mechanics_handle]        = m_acc->mass;
                                handle_to_damping[profile->mechanics_handle]     = m_acc->damping;
                                handle_to_restitution[profile->mechanics_handle] = m_acc->restitution;
                            } else {
                                handle_to_mass[profile->mechanics_handle]        = 1.0f;
                                handle_to_damping[profile->mechanics_handle]     = 0.99f;
                                handle_to_restitution[profile->mechanics_handle] = 0.8f;
                            }
                        }
                    }
                }
            }
        }
    }

    //去重
    std::sort(mechanics_handles.begin(), mechanics_handles.end());
    mechanics_handles.erase(std::unique(mechanics_handles.begin(), mechanics_handles.end()), mechanics_handles.end());

    //无物理物体时直接返回
    if (mechanics_handles.size() < 1) {
        CFW_LOG_TRACE("MechanicsSystem: No physics objects found.");
        return;
    }

    CFW_LOG_TRACE("MechanicsSystem: {} physics objects found.", mechanics_handles.size());

    // 速度更新 重力+阻尼+位置更新+地板碰撞检测
    for (std::uintptr_t h : mechanics_handles) {
        //速度累加重力
        handle_to_velocity[h].x += gravity.x * fixed_dt;
        handle_to_velocity[h].y += gravity.y * fixed_dt;
        handle_to_velocity[h].z += gravity.z * fixed_dt;

        //速度阻尼（模拟空气阻力）
        float d = handle_to_damping[h];
        handle_to_velocity[h].x *= d;
        handle_to_velocity[h].y *= d;
        handle_to_velocity[h].z *= d;

        //获取物体的几何和变换数据
        auto m_acc = mechanics_storage.acquire_read(h);
        if (!m_acc) continue; // 数据无效则跳过
        auto geom_acc = geometry_storage.acquire_read(m_acc->geometry_handle);
        if (!geom_acc) continue;
        auto tx_acc = transform_storage.acquire_write(geom_acc->transform_handle); // 可写权限（更新位置）
        if (!tx_acc) continue;

        //更新物体位置
        tx_acc->position.x += handle_to_velocity[h].x * fixed_dt;
        tx_acc->position.y += handle_to_velocity[h].y * fixed_dt;
        tx_acc->position.z += handle_to_velocity[h].z * fixed_dt;

        //地板碰撞检测与响应
        if (tx_acc->position.y < floor_y - floor_eps) {
            // 修正位置：防止穿透地板
            tx_acc->position.y = floor_y;
            // 速度反弹（Y轴反向*地板反弹系数）
            handle_to_velocity[h].y = -handle_to_velocity[h].y * floor_restitution;
            // 速度过小0（防止抖动）
            if (std::abs(handle_to_velocity[h].y) < floor_eps * 10) {
                handle_to_velocity[h].y = 0.0f;
            }
        }
    }

    //计算所有物体的世界空间AABB（用于碰撞检测）
    std::vector<MechanicsWorldAABB> mechanics_data;
    mechanics_data.reserve(mechanics_handles.size());
    std::unordered_map<std::uintptr_t, std::size_t> handle_to_index;

    for (std::uintptr_t h : mechanics_handles) {
        //获取物体基础数据
        auto m_acc = mechanics_storage.acquire_read(h);
        if (!m_acc) continue;
        const auto& m = *m_acc;
        auto geom_acc = geometry_storage.acquire_read(m.geometry_handle);
        if (!geom_acc) continue;
        auto tx_acc = transform_storage.acquire_read(geom_acc->transform_handle);
        if (!tx_acc) continue;

        const auto& t = *tx_acc;
        // 计算局部AABB中心和半长轴
        ktm::fvec3 c_local;
        c_local.x = (m.min_xyz.x + m.max_xyz.x) * 0.5f;
        c_local.y = (m.min_xyz.y + m.max_xyz.y) * 0.5f;
        c_local.z = (m.min_xyz.z + m.max_xyz.z) * 0.5f;

        ktm::fvec3 e_local;
        e_local.x = (m.max_xyz.x - m.min_xyz.x) * 0.5f;
        e_local.y = (m.max_xyz.y - m.min_xyz.y) * 0.5f;
        e_local.z = (m.max_xyz.z - m.min_xyz.z) * 0.5f;

        //计算世界空间AABB
        MechanicsWorldAABB entry;
        entry.handle = h;
        entry.transform_handle = geom_acc->transform_handle;
        //局部中心*缩放+世界位置
        entry.center_world.x = c_local.x + t.position.x;
        entry.center_world.y = c_local.y + t.position.y;
        entry.center_world.z = c_local.z + t.position.z;
        //局部半长轴
        ktm::fvec3 e_world;
        e_world.x = std::abs(e_local.x * t.scale.x);
        e_world.y = std::abs(e_local.y * t.scale.y);
        e_world.z = std::abs(e_local.z * t.scale.z);
        //世界AABB边界
        entry.min_world.x = entry.center_world.x - e_world.x;
        entry.min_world.y = entry.center_world.y - e_world.y;
        entry.min_world.z = entry.center_world.z - e_world.z;
        entry.max_world.x = entry.center_world.x + e_world.x;
        entry.max_world.y = entry.center_world.y + e_world.y;
        entry.max_world.z = entry.center_world.z + e_world.z;


        handle_to_index[h] = mechanics_data.size();
        mechanics_data.push_back(entry);
    }

    // 计算场景级世界 AABB 并写入 scene_storage
    if (!mechanics_data.empty()) {
        ktm::fvec3 scene_min = mechanics_data[0].min_world;
        ktm::fvec3 scene_max = mechanics_data[0].max_world;
        for (const auto& e : mechanics_data) {
            scene_min.x = std::min(scene_min.x, e.min_world.x);
            scene_min.y = std::min(scene_min.y, e.min_world.y);
            scene_min.z = std::min(scene_min.z, e.min_world.z);
            scene_max.x = std::max(scene_max.x, e.max_world.x);
            scene_max.y = std::max(scene_max.y, e.max_world.y);
            scene_max.z = std::max(scene_max.z, e.max_world.z);
        }
        ktm::fvec3 scene_center;
        scene_center.x = (scene_min.x + scene_max.x) * 0.5f;
        scene_center.y = (scene_min.y + scene_max.y) * 0.5f;
        scene_center.z = (scene_min.z + scene_max.z) * 0.5f;

        for (auto sh : scene_handles) {
            if (auto s_w = scene_storage.acquire_write(sh)) {
                s_w->min_world    = scene_min;
                s_w->max_world    = scene_max;
                s_w->center_world = scene_center;
            }
        }
    }

    //少于2个物体无需碰撞检测
    if (mechanics_data.size() < 2) {
        return;
    }

    //构建八叉树根节点的AABB（包含所有物体）
    ktm::fvec3 root_min = mechanics_data[0].min_world;
    ktm::fvec3 root_max = mechanics_data[0].max_world;

    //遍历所有物体，根节点AABB到包含所有物体
    for (const auto& e : mechanics_data) {
        root_min.x = std::min(root_min.x, e.min_world.x);
        root_min.y = std::min(root_min.y, e.min_world.y);
        root_min.y = std::max(root_min.y, floor_y); // 根节点Y轴下限不低于地板
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

    //八叉树碰撞检测：插入物体→收集碰撞对→去重1
    OctreeNode octree_root;
    octree_root.min_bounds = root_min;
    octree_root.max_bounds = root_max;

    //插入所有物体到八叉树
    for (const auto& e : mechanics_data) {
        octree_insert(octree_root, e.handle, e.min_world, e.max_world, 0);
    }

    //收集碰撞对
    std::vector<std::pair<std::uintptr_t, std::uintptr_t>> pairs;
    pairs.reserve(mechanics_data.size() * 4);
    octree_collect_pairs(octree_root, pairs);

    //去重碰撞对
    octree_dedupe_pairs(pairs);

    // Build current active collision set (actor pairs)
    std::unordered_set<std::pair<std::uintptr_t, std::uintptr_t>, PairHash> curr_active_collisions;

    //窄相位碰撞检测与响应
    constexpr float eps = 1e-6f;               //容差
    constexpr float min_separation = 0.001f;   //避免微小重叠

    for (const auto& pr : pairs) {
        const std::uintptr_t ha = pr.first;
        const std::uintptr_t hb = pr.second;

        //查找两个物体的AABB数据
        auto it_a = handle_to_index.find(ha);
        auto it_b = handle_to_index.find(hb);
        if (it_a == handle_to_index.end() || it_b == handle_to_index.end()) continue;

        MechanicsWorldAABB& a = mechanics_data[it_a->second];
        MechanicsWorldAABB& b = mechanics_data[it_b->second];

        if (!aabb_overlap(a.min_world, a.max_world, b.min_world, b.max_world)) {
            continue;
        }

        //计算碰撞法线（从A指向B的单位向量）
        ktm::fvec3 diff;
        diff.x = b.center_world.x - a.center_world.x;
        diff.y = b.center_world.y - a.center_world.y;
        diff.z = b.center_world.z - a.center_world.z;

        //防止两物体中心重合
        if (ktm::length(diff) < eps) {
            diff = make_fvec3(0.0f, 0.0f, 0.0f);
        }
        ktm::fvec3 normal = ktm::normalize(diff); // 单位化法线

        //计算三个轴的重叠量，取最小值作为穿透深度
        float overlap_x = (a.max_world.x - a.min_world.x)/2 + (b.max_world.x - b.min_world.x)/2 - std::abs(diff.x);
        float overlap_y = (a.max_world.y - a.min_world.y)/2 + (b.max_world.y - b.min_world.y)/2 - std::abs(diff.y);
        float overlap_z = (a.max_world.z - a.min_world.z)/2 + (b.max_world.z - b.min_world.z)/2 - std::abs(diff.z);
        float min_overlap = std::min({overlap_x, overlap_y, overlap_z});

        //重叠量过小，忽略
        if (min_overlap < min_separation) continue;

        ktm::fvec3 point;
        point.x = (a.center_world.x + b.center_world.x) * 0.5f;
        point.y = (a.center_world.y + b.center_world.y) * 0.5f;
        point.z = (a.center_world.z + b.center_world.z) * 0.5f;

        // Acquire accessors and copy callbacks out while holding the accessor to
        // avoid races with writers. Call the copied callbacks outside the
        // accessor scope so we don't hold engine locks while invoking Python.
        std::function<void(std::uintptr_t, bool, const std::array<float, 3>&, const std::array<float, 3>&)> cb_a;
        std::function<void(std::uintptr_t, bool, const std::array<float, 3>&, const std::array<float, 3>&)> cb_b;

        {
            auto mech_a_acc = mechanics_storage.acquire_read(ha);
            if (mech_a_acc && mech_a_acc->collision_callback) {
                cb_a = mech_a_acc->collision_callback; // copy under read lock
            }
        }

        {
            auto mech_b_acc = mechanics_storage.acquire_read(hb);
            if (mech_b_acc && mech_b_acc->collision_callback) {
                cb_b = mech_b_acc->collision_callback; // copy under read lock
            }
        }

        // Prepare arrays for callback arguments
        std::array<float, 3> normal_arr = {normal.x, normal.y, normal.z};
        std::array<float, 3> point_arr = {point.x, point.y, point.z};

        // Translate mechanics handles to actor handles (fallback to mechanics handle if mapping missing)
        std::uintptr_t actor_a_handle = ha;
        std::uintptr_t actor_b_handle = hb;
        auto it_map_a = mech_to_actor.find(ha);
        if (it_map_a != mech_to_actor.end()) actor_a_handle = it_map_a->second;
        auto it_map_b = mech_to_actor.find(hb);
        if (it_map_b != mech_to_actor.end()) actor_b_handle = it_map_b->second;

        // Insert actor pair into current active collisions set (order normalized)
        std::pair<std::uintptr_t, std::uintptr_t> actor_pair = actor_a_handle <= actor_b_handle ? std::make_pair(actor_a_handle, actor_b_handle) : std::make_pair(actor_b_handle, actor_a_handle);
        curr_active_collisions.insert(actor_pair);

        // Determine if this pair was newly started this frame
        bool was_active = (g_prev_active_collisions.find(actor_pair) != g_prev_active_collisions.end());

        // Only notify on collision start (was not active previously). Sustained collisions are ignored.
        if (!was_active) {
            if (cb_a) {
                try {
                    // cb_a is callback for object A; pass other actor handle (B) and began=true
                    cb_a(actor_b_handle, true, normal_arr, point_arr);
                } catch (...) {
                    CFW_LOG_ERROR("MechanicsSystem: Exception occurred in collision callback for actor {}.", actor_a_handle);
                }
            }

            if (cb_b) {
                std::array<float, 3> reverse_normal_arr = {-normal.x, -normal.y, -normal.z};
                try {
                    cb_b(actor_a_handle, true, reverse_normal_arr, point_arr);
                } catch (...) {
                    CFW_LOG_ERROR("MechanicsSystem: Exception occurred in collision callback for actor {}.", actor_b_handle);
                }
            }
        }

        //穿透修复：根据质量分配位移（质量大的物体位移少）
        float mass_a = handle_to_mass[ha];
        float mass_b = handle_to_mass[hb];

        float push_a = min_overlap * (mass_b / (mass_a + mass_b));
        float push_b = min_overlap * (mass_a / (mass_a + mass_b));

        if (auto txa = transform_storage.acquire_write(a.transform_handle)) {
            ktm::fvec3 push_vec_a;
            push_vec_a.x = normal.x * push_a;
            push_vec_a.y = normal.y * push_a;
            push_vec_a.z = normal.z * push_a;

            //更新位置
            txa->position.x -= push_vec_a.x;
            txa->position.y -= push_vec_a.y;
            txa->position.z -= push_vec_a.z;

            //更新AABB数据
            a.center_world.x -= push_vec_a.x;
            a.center_world.y -= push_vec_a.y;
            a.center_world.z -= push_vec_a.z;
            a.min_world.x -= push_vec_a.x;
            a.min_world.y -= push_vec_a.y;
            a.min_world.z -= push_vec_a.z;
            a.max_world.x -= push_vec_a.x;
            a.max_world.y -= push_vec_a.y;
            a.max_world.z -= push_vec_a.z;

            //修复后检测是否穿透地板
            if (txa->position.y < floor_y - floor_eps) {
                txa->position.y = floor_y;
                handle_to_velocity[ha].y = std::max(0.0f, handle_to_velocity[ha].y);
            }
        }

        //修复B物体的位置（沿法线正向位移）
        if (auto txb = transform_storage.acquire_write(b.transform_handle)) {
            ktm::fvec3 push_vec_b;
            push_vec_b.x = normal.x * push_b;
            push_vec_b.y = normal.y * push_b;
            push_vec_b.z = normal.z * push_b;

            //更新位置
            txb->position.x += push_vec_b.x;
            txb->position.y += push_vec_b.y;
            txb->position.z += push_vec_b.z;

            //更新
            b.center_world.x += push_vec_b.x;
            b.center_world.y += push_vec_b.y;
            b.center_world.z += push_vec_b.z;
            b.min_world.x += push_vec_b.x;
            b.min_world.y += push_vec_b.y;
            b.min_world.z += push_vec_b.z;
            b.max_world.x += push_vec_b.x;
            b.max_world.y += push_vec_b.y;
            b.max_world.z += push_vec_b.z;

            //修复后检测是否穿透地板
            if (txb->position.y < floor_y - floor_eps) {
                txb->position.y = floor_y;
                handle_to_velocity[hb].y = std::max(0.0f, handle_to_velocity[hb].y);
            }
        }

        //碰撞速度反弹
        ktm::fvec3 vel_a = handle_to_velocity[ha];
        ktm::fvec3 vel_b = handle_to_velocity[hb];

        //计算速度在法线上的投影
        float vel_a_normal = ktm::dot(vel_a, normal);
        float vel_b_normal = ktm::dot(vel_b, normal);

        //取两物体反弹系数的平均值
        float restitution = (handle_to_restitution[ha] + handle_to_restitution[hb]) * 0.5f;

        //冲量计算
        float j = (-(1 + restitution) * (vel_a_normal - vel_b_normal)) / (1/mass_a + 1/mass_b);
        ktm::fvec3 impulse; // 冲量向量
        impulse.x = normal.x * j;
        impulse.y = normal.y * j;
        impulse.z = normal.z * j;

        //应用冲量到速度
        handle_to_velocity[ha].x += impulse.x / mass_a;
        handle_to_velocity[ha].y += impulse.y / mass_a;
        handle_to_velocity[ha].z += impulse.z / mass_a;

        handle_to_velocity[hb].x -= impulse.x / mass_b;
        handle_to_velocity[hb].y -= impulse.y / mass_b;
        handle_to_velocity[hb].z -= impulse.z / mass_b;
    }
}

} // namespace Corona::Systems