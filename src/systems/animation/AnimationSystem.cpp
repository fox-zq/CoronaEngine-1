#include <ResourceTypes.h>
#include <corona/core/detail/SystemHubs.h>
#include <corona/script/PythonBridge.h>
#include <corona/systems/AnimationSystem.h>

#include <cmath>
#include <filesystem>

#include "Animation.h"
#include "Bone.h"
#include "Model.h"

namespace {
using namespace Corona;

// 在本 cpp 内部隐藏的帮助函数，避免在头文件暴露重量类型
const Bone* findBone(const Animation& anim, const std::string& name) {
    for (const Bone& b : anim.m_Bones) {
        if (b.m_Name == name) {
            return &b;
        }
    }
    return nullptr;
}

int getPositionIndex(const Bone& bone, float time) {
    for (int i = 0; i < bone.m_NumPositions - 1; ++i) {
        if (time < bone.m_Positions[i + 1].timeStamp) {
            return i;
        }
    }
    return bone.m_NumPositions - 2;
}
int getRotationIndex(const Bone& bone, float time) {
    for (int i = 0; i < bone.m_NumRotations - 1; ++i) {
        if (time < bone.m_Rotations[i + 1].timeStamp) {
            return i;
        }
    }
    return bone.m_NumRotations - 2;
}
int getScaleIndex(const Bone& bone, float time) {
    for (int i = 0; i < bone.m_NumScales - 1; ++i) {
        if (time < bone.m_Scales[i + 1].timeStamp) {
            return i;
        }
    }
    return bone.m_NumScales - 2;
}

ktm::fmat4x4 interpolatePosition(const Bone& bone, float time) {
    if (bone.m_NumPositions <= 0) {
        return ktm::translate3d(ktm::fvec3(0, 0, 0));
    }
    if (bone.m_NumPositions == 1) {
        return ktm::translate3d(bone.m_Positions[0].position);
    }
    const int p0 = getPositionIndex(bone, time);
    const int p1 = p0 + 1;
    const float last = bone.m_Positions[p0].timeStamp;
    const float next = bone.m_Positions[p1].timeStamp;
    const float factor = (time - last) / (next - last);
    const ktm::fvec3 pos = ktm::lerp(bone.m_Positions[p0].position, bone.m_Positions[p1].position, factor);
    return ktm::translate3d(pos);
}
ktm::fmat4x4 interpolateRotation(const Bone& bone, float time) {
    if (bone.m_NumRotations <= 0) {
        return ktm::fquat(0, 0, 0, 1).matrix4x4();
    }
    if (bone.m_NumRotations == 1) {
        return ktm::normalize(bone.m_Rotations[0].orientation).matrix4x4();
    }
    const int r0 = getRotationIndex(bone, time);
    const int r1 = r0 + 1;
    const float last = bone.m_Rotations[r0].timeStamp;
    const float next = bone.m_Rotations[r1].timeStamp;
    const float factor = (time - last) / (next - last);
    ktm::fquat rot = ktm::slerp(bone.m_Rotations[r0].orientation, bone.m_Rotations[r1].orientation, factor);
    rot = ktm::normalize(rot);
    return rot.matrix4x4();
}
ktm::fmat4x4 interpolateScale(const Bone& bone, float time) {
    if (bone.m_NumScales <= 0) {
        return ktm::scale3d(ktm::fvec3(1, 1, 1));
    }
    if (bone.m_NumScales == 1) {
        return ktm::scale3d(bone.m_Scales[0].scale);
    }
    const int s0 = getScaleIndex(bone, time);
    const int s1 = s0 + 1;
    const float last = bone.m_Scales[s0].timeStamp;
    const float next = bone.m_Scales[s1].timeStamp;
    const float factor = (time - last) / (next - last);
    const ktm::fvec3 sc = ktm::lerp(bone.m_Scales[s0].scale, bone.m_Scales[s1].scale, factor);
    return ktm::scale3d(sc);
}

void calculateBoneTransform(const AnimationState& state,
                            const Animation& anim,
                            const Animation::AssimpNodeData& node,
                            const ktm::fmat4x4& parent,
                            std::vector<ktm::fmat4x4>& outBones) {
    ktm::fmat4x4 nodeTransform = node.transformation;
    if (const Bone* b = findBone(anim, node.name)) {
        const ktm::fmat4x4 T = interpolatePosition(*b, state.currentTime);
        const ktm::fmat4x4 R = interpolateRotation(*b, state.currentTime);
        const ktm::fmat4x4 S = interpolateScale(*b, state.currentTime);
        nodeTransform = T * R * S;
    }
    const ktm::fmat4x4 global = parent * nodeTransform;
    if (auto it = state.model->m_BoneInfoMap.find(node.name); it != state.model->m_BoneInfoMap.end()) {
        const int index = it->second->ID;
        const ktm::fmat4x4& offset = it->second->OffsetMatrix;
        if (index >= 0 && static_cast<size_t>(index) < outBones.size()) {
            outBones[index] = global * offset;
        }
    }
    for (const auto& child : node.children) {
        calculateBoneTransform(state, anim, child, global, outBones);
    }
}

std::vector<ktm::fvec3> calculateVertices(const ktm::fvec3& startMin, const ktm::fvec3& startMax) {
    std::vector<ktm::fvec3> vertices;
    vertices.reserve(8);
    vertices.push_back(startMin);
    vertices.emplace_back(startMax.x, startMin.y, startMin.z);
    vertices.emplace_back(startMin.x, startMax.y, startMin.z);
    vertices.emplace_back(startMax.x, startMax.y, startMin.z);
    vertices.emplace_back(startMin.x, startMin.y, startMax.z);
    vertices.emplace_back(startMax.x, startMin.y, startMax.z);
    vertices.emplace_back(startMin.x, startMax.y, startMax.z);
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

using namespace Corona;

AnimationSystem::AnimationSystem()
    : ThreadedSystem("AnimationSystem") {}

void AnimationSystem::configure(const Interfaces::SystemContext& context) {
    ThreadedSystem::configure(context);
    resource_service_ = services().try_get<Interfaces::IResourceService>();
    scheduler_ = services().try_get<Interfaces::ICommandScheduler>();
    if (scheduler_) {
        system_queue_handle_ = scheduler_->get_queue(name());
        if (!system_queue_handle_) {
            system_queue_handle_ = scheduler_->create_queue(name());
        }
    } else {
        system_queue_handle_.reset();
    }
}

void AnimationSystem::onStart() {
}

void AnimationSystem::onTick() {
    if (auto* queue = command_queue()) {
        int spun = 0;
        while (spun < 100 && !queue->empty()) {
            if (!queue->try_execute()) {
                continue;
            }
            ++spun;
        }
    }

    // frame_count_++;
    // if (frame_count_ >= 60) {
    //     frame_count_ = 0;
    //     send_collision_event();
    // }

    // 遍历关注的 AnimationState 并推进
    if (auto* caches = data_caches()) {
        auto& anim_cache = caches->get<AnimationState>();
        anim_cache.safe_loop_foreach(state_cache_keys_, [&](std::shared_ptr<AnimationState> st) {
            if (!st || !st->model) {
                return;
            }
            update_animation_state(*st, 1.0f / 120.0f);  // 与系统线程目标帧率一致
        });

        auto& model_cache = caches->get<Model>();
        model_cache.safe_loop_foreach(model_cache_keys_, [&](uint64_t id, std::shared_ptr<Model> m) {
            if (!m) {
                return;
            }
            other_model_cache_keys_.erase(id);
            update_physics(*m);
            other_model_cache_keys_.insert(id);
        });
    }
}

void AnimationSystem::onStop() {
    boneMatrices_.clear();
    animationTime_.clear();
}

void AnimationSystem::process_animation(uint64_t /*id*/) {
}

void AnimationSystem::watch_state(uint64_t id) {
    state_cache_keys_.insert(id);
}

void AnimationSystem::unwatch_state(uint64_t id) {
    state_cache_keys_.erase(id);
}

void AnimationSystem::watch_model(uint64_t id) {
    model_cache_keys_.insert(id);
    other_model_cache_keys_.insert(id);
}

void AnimationSystem::unwatch_model(uint64_t id) {
    model_cache_keys_.erase(id);
    other_model_cache_keys_.erase(id);
}

void AnimationSystem::clear_watched() {
    state_cache_keys_.clear();
    model_cache_keys_.clear();
    other_model_cache_keys_.clear();
}

void AnimationSystem::update_animation_state(AnimationState& state, float dt) {
    if (!state.model) {
        return;
    }
    const auto animCount = state.model->skeletalAnimations.size();
    if (animCount == 0 || state.animationIndex >= animCount) {
        return;
    }

    const Animation& anim = state.model->skeletalAnimations[state.animationIndex];
    // 推进时间（anim.m_TicksPerSecond 为动画时钟刻度）
    state.currentTime += static_cast<float>(anim.m_TicksPerSecond) * dt;
    const auto duration = static_cast<float>(anim.m_Duration);
    if (duration > 0.0f) {
        state.currentTime = fmod(state.currentTime, duration);
    }

    // 准备输出骨矩阵数组
    const size_t boneCount = state.model->m_BoneInfoMap.size();
    state.bones.resize(boneCount, ktm::fmat4x4::from_eye());

    const ktm::fmat4x4 identity = ktm::fmat4x4::from_eye();
    // 从根开始递归计算
    calculateBoneTransform(state, anim, anim.m_RootNode, identity, state.bones);

    // if (!state.model->bonesMatrixBuffer) {
    //     state.model->bonesMatrixBuffer = HardwareBuffer(state.bones, BufferUsage::StorageBuffer);
    // } else {
    //     state.model->bonesMatrixBuffer.copyFromData(state.bones.data(), state.bones.size() * sizeof(ktm::fmat4x4));
    // }
}

void AnimationSystem::send_collision_event() {
    CE_LOG_INFO("Sending collision event");
    if (!scheduler_) {
        CE_LOG_WARN("[AnimationSystem] 无可用的命令调度服务，无法投递事件");
        return;
    }
    auto main_queue = scheduler_->get_queue("MainThread");
    if (!main_queue) {
        CE_LOG_WARN("[AnimationSystem] MainThread 队列不存在");
        return;
    }
    std::string payload = "type:collision,src:animation,frame:60";
    main_queue->enqueue([payload] {
        Corona::PythonBridge::send(payload);
    });
}

void AnimationSystem::update_physics(Model& m) {
    collisionActors_.clear();

    auto StartMin = m.minXYZ;
    auto StartMax = m.maxXYZ;

    // 计算世界坐标下的包围盒顶点
    std::vector<ktm::fvec3> Vertices1 = calculateVertices(StartMin, StartMax);
    ktm::fmat4x4 actorMatrix = m.modelMatrix;
    for (auto& v : Vertices1) {
        v = ktm::fvec4(actorMatrix * ktm::fvec4(v, 1.0f)).xyz();
    }

    if (auto* caches = data_caches()) {
        auto& model_cache = caches->get<Model>();
        model_cache.safe_loop_foreach(other_model_cache_keys_, [&](std::shared_ptr<Model> otherModel) {
            if (!otherModel) {
                return;
            }

            auto otherStartMin = otherModel->minXYZ;
            auto otherStartMax = otherModel->maxXYZ;

            // 计算世界坐标下的包围盒顶点
            std::vector<ktm::fvec3> Vertices2 = calculateVertices(otherStartMin, otherStartMax);
            ktm::fmat4x4 otherModelMatrix = otherModel->modelMatrix;
            for (auto& v : Vertices2) {
                v = ktm::fvec4(otherModelMatrix * ktm::fvec4(v, 1.0f)).xyz();
            }

            // 碰撞检测
            if (checkCollision(Vertices1, Vertices2)) {
                CE_LOG_INFO("Collision detected!");
                collisionActors_.insert(otherModel.get());
                collisionActors_.insert(std::addressof(m));

                // 碰撞响应
                // 计算碰撞法线（从actor指向otherActor）
                ktm::fvec3 center1 = (StartMin + StartMax) * 0.5f;
                ktm::fvec3 center2 = (otherStartMin + otherStartMax) * 0.5f;
                ktm::fvec3 normal = ktm::normalize(center2 - center1);

                // 物体分离（防止穿透）
                const float separation = 0.02f;
                m.positon += ktm::fvec3(-normal * separation);
                otherModel->positon += (normal * separation);

                // 直接施加反弹位移
                const float bounceStrength = 0.1f;  // 反弹强度
                m.positon += ktm::fvec3(-normal * bounceStrength);
                otherModel->positon += (normal * bounceStrength);

                // 更新模型矩阵           /*变换矩阵 = 平移 * 旋转 * 缩放/     //先缩放在旋转后平移//
                m.modelMatrix = ktm::fmat4x4(ktm::translate3d(m.positon) * ktm::translate3d(m.rotation) * ktm::translate3d(m.scale));
                otherModel->modelMatrix = ktm::fmat4x4(ktm::translate3d(otherModel->positon) * ktm::translate3d(otherModel->rotation) * ktm::translate3d(otherModel->scale));
            }
        });
    }
}
