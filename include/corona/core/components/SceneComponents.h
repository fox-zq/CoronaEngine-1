#pragma once

#include <ktm/type_vec.h>
#include <optional>
#include <string>

namespace Corona::Components {

struct DisplaySurface {
    void* surface = nullptr;
};

struct Camera {
    float fov = 45.0f;
    ktm::fvec3 pos{};
    ktm::fvec3 forward{};
    ktm::fvec3 worldUp{};
    bool isMain = false; // 标识是否为主相机
    std::string name;    // 相机名称，用于标识
};

struct Light {
    enum class Type {
        Directional,  // 方向光
        Point,        // 点光源
        Spot          // 聚光灯
    };
    
    Type type = Type::Directional;
    ktm::fvec3 color = {1.0f, 1.0f, 1.0f};  // 光源颜色
    float intensity = 1.0f;                 // 光源强度
    
    // 方向光参数
    ktm::fvec3 direction = {-1.0f, -1.0f, -1.0f};
    
    // 点光源参数
    float radius = 10.0f;                   // 衰减半径
    
    // 聚光灯参数
    float spotAngle = 45.0f;                // 聚光灯角度（度）
    float spotPenumbra = 10.0f;             // 半影角度（度）
    
    bool isMain = false;                    // 标识是否为主光源
    std::string name;                       // 光源名称，用于标识
};

struct SceneInfo {
    // 可选的主相机ID，用于快速查找主相机
    std::optional<entt::entity> mainCameraID;
    
    // 可选的主光源ID，用于快速查找主光源
    std::optional<entt::entity> mainLightID;
};

}  // namespace Corona::Components