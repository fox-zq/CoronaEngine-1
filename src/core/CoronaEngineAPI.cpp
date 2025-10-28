//
// Created by 25473 on 25-9-19.
//

#include <corona/api/CoronaEngineAPI.h>
#include <corona/core/Engine.h>
#include <corona/core/components/ActorComponents.h>
#include <corona/core/components/SceneComponents.h>
#include <stdexcept>

// 定义静态 ECS 注册表
entt::registry CoronaEngineAPI::registry_;

CoronaEngineAPI::Scene::Scene(void* surface, bool /*lightField*/)
    : sceneID(registry_.create()) {
    registry_.emplace<RenderTag>(sceneID);
    registry_.emplace<Corona::Components::SceneInfo>(sceneID);
    if (surface) {
        registry_.emplace_or_replace<Corona::Components::DisplaySurface>(sceneID, Corona::Components::DisplaySurface{surface});
    }
}

CoronaEngineAPI::Scene::~Scene() {
    // 先销毁所有属于该场景的相机和光源
    auto& sceneInfo = registry_.get<Corona::Components::SceneInfo>(sceneID);
    
    // 销毁所有相机实体
    registry_.view<Corona::Components::Camera>().each([this](entt::entity cameraEntity, const Corona::Components::Camera&) {
        if (registry_.valid(cameraEntity)) {
            registry_.destroy(cameraEntity);
        }
    });
    
    // 销毁所有光源实体
    registry_.view<Corona::Components::Light>().each([this](entt::entity lightEntity, const Corona::Components::Light&) {
        if (registry_.valid(lightEntity)) {
            registry_.destroy(lightEntity);
        }
    });
    
    registry_.destroy(sceneID);
}

// 兼容旧版API的设置主相机方法
void CoronaEngineAPI::Scene::setCamera(const ktm::fvec3& position, const ktm::fvec3& forward, const ktm::fvec3& worldUp, float fov) const {
    auto& sceneInfo = registry_.get<Corona::Components::SceneInfo>(sceneID);
    
    // 如果已经有主相机，则更新它
    if (sceneInfo.mainCameraID.has_value() && registry_.valid(sceneInfo.mainCameraID.value())) {
        auto& camera = registry_.get<Corona::Components::Camera>(sceneInfo.mainCameraID.value());
        camera.fov = fov;
        camera.pos = position;
        camera.forward = forward;
        camera.worldUp = worldUp;
    } else {
        // 创建新的主相机
        entt::entity cameraEntity = registry_.create();
        registry_.emplace<Corona::Components::Camera>(cameraEntity, 
            Corona::Components::Camera{fov, position, forward, worldUp, true, "MainCamera"});
        sceneInfo.mainCameraID = cameraEntity;
    }
}

// 兼容旧版API的设置主光源方法
void CoronaEngineAPI::Scene::setSunDirection(ktm::fvec3 direction) const {
    auto& sceneInfo = registry_.get<Corona::Components::SceneInfo>(sceneID);
    
    // 如果已经有主光源，则更新它
    if (sceneInfo.mainLightID.has_value() && registry_.valid(sceneInfo.mainLightID.value())) {
        auto& light = registry_.get<Corona::Components::Light>(sceneInfo.mainLightID.value());
        if (light.type == Corona::Components::Light::Type::Directional) {
            light.direction = direction;
        }
    } else {
        // 创建新的主方向光
        entt::entity lightEntity = registry_.create();
        Corona::Components::Light light;
        light.type = Corona::Components::Light::Type::Directional;
        light.direction = direction;
        light.isMain = true;
        light.name = "MainSun";
        registry_.emplace<Corona::Components::Light>(lightEntity, light);
        sceneInfo.mainLightID = lightEntity;
    }
}

void CoronaEngineAPI::Scene::setDisplaySurface(void* surface) {
    registry_.emplace_or_replace<Corona::Components::DisplaySurface>(sceneID, Corona::Components::DisplaySurface{surface});
}

// 创建新相机的方法
entt::entity CoronaEngineAPI::Scene::createCamera(const std::string& name, const ktm::fvec3& position, const ktm::fvec3& forward, 
                                               const ktm::fvec3& worldUp, float fov, bool isMain) const {
    auto& sceneInfo = registry_.get<Corona::Components::SceneInfo>(sceneID);
    
    // 创建相机实体
    entt::entity cameraEntity = registry_.create();
    Corona::Components::Camera camera;
    camera.fov = fov;
    camera.pos = position;
    camera.forward = forward;
    camera.worldUp = worldUp;
    camera.isMain = isMain;
    camera.name = name.empty() ? "Camera" : name;
    registry_.emplace<Corona::Components::Camera>(cameraEntity, camera);
    
    // 如果设置为主相机，则更新场景信息
    if (isMain) {
        // 如果已有主相机，取消其主相机状态
        if (sceneInfo.mainCameraID.has_value() && registry_.valid(sceneInfo.mainCameraID.value())) {
            auto& oldMainCamera = registry_.get<Corona::Components::Camera>(sceneInfo.mainCameraID.value());
            oldMainCamera.isMain = false;
        }
        sceneInfo.mainCameraID = cameraEntity;
    }
    
    return cameraEntity;
}

// 创建新光源的方法
entt::entity CoronaEngineAPI::Scene::createLight(const std::string& name, Corona::Components::Light::Type type, 
                                              const ktm::fvec3& color, float intensity, bool isMain) const {
    auto& sceneInfo = registry_.get<Corona::Components::SceneInfo>(sceneID);
    
    // 创建光源实体
    entt::entity lightEntity = registry_.create();
    Corona::Components::Light light;
    light.type = type;
    light.color = color;
    light.intensity = intensity;
    light.isMain = isMain;
    light.name = name.empty() ? "Light" : name;
    registry_.emplace<Corona::Components::Light>(lightEntity, light);
    
    // 如果设置为主光源，则更新场景信息
    if (isMain) {
        // 如果已有主光源，取消其主光源状态
        if (sceneInfo.mainLightID.has_value() && registry_.valid(sceneInfo.mainLightID.value())) {
            auto& oldMainLight = registry_.get<Corona::Components::Light>(sceneInfo.mainLightID.value());
            oldMainLight.isMain = false;
        }
        sceneInfo.mainLightID = lightEntity;
    }
    
    return lightEntity;
}

// 设置相机为主相机的方法
void CoronaEngineAPI::Scene::setMainCamera(entt::entity cameraEntity) const {
    if (!registry_.valid(cameraEntity) || !registry_.try_get<Corona::Components::Camera>(cameraEntity)) {
        throw std::invalid_argument("Invalid camera entity");
    }
    
    auto& sceneInfo = registry_.get<Corona::Components::SceneInfo>(sceneID);
    
    // 取消原主相机的主相机状态
    if (sceneInfo.mainCameraID.has_value() && registry_.valid(sceneInfo.mainCameraID.value())) {
        auto& oldMainCamera = registry_.get<Corona::Components::Camera>(sceneInfo.mainCameraID.value());
        oldMainCamera.isMain = false;
    }
    
    // 设置新主相机
    auto& newMainCamera = registry_.get<Corona::Components::Camera>(cameraEntity);
    newMainCamera.isMain = true;
    sceneInfo.mainCameraID = cameraEntity;
}

// 设置光源为主光源的方法
void CoronaEngineAPI::Scene::setMainLight(entt::entity lightEntity) const {
    if (!registry_.valid(lightEntity) || !registry_.try_get<Corona::Components::Light>(lightEntity)) {
        throw std::invalid_argument("Invalid light entity");
    }
    
    auto& sceneInfo = registry_.get<Corona::Components::SceneInfo>(sceneID);
    
    // 取消原主光源的主光源状态
    if (sceneInfo.mainLightID.has_value() && registry_.valid(sceneInfo.mainLightID.value())) {
        auto& oldMainLight = registry_.get<Corona::Components::Light>(sceneInfo.mainLightID.value());
        oldMainLight.isMain = false;
    }
    
    // 设置新主光源
    auto& newMainLight = registry_.get<Corona::Components::Light>(lightEntity);
    newMainLight.isMain = true;
    sceneInfo.mainLightID = lightEntity;
}

CoronaEngineAPI::Actor::Actor(const std::string& path)
    : actorID(registry_.create()) {
    // 标签（可选）
    registry_.emplace<RenderTag>(actorID);
    // 仅存资源ID/路径为组件
    registry_.emplace_or_replace<Corona::Components::ModelResource>(actorID, Corona::Components::ModelResource{path});
}

CoronaEngineAPI::Actor::~Actor() {
    // 可选：移除标签/组件
    // registry_.remove_if_exists<RenderTag>(actorID);
}

void CoronaEngineAPI::Actor::move(ktm::fvec3 pos) const {
}

void CoronaEngineAPI::Actor::rotate(ktm::fvec3 euler) const {
}

void CoronaEngineAPI::Actor::scale(ktm::fvec3 size) const {
}