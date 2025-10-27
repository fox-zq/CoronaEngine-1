//
// Created by 25473 on 25-9-19.
//

#include <corona/api/CoronaEngineAPI.h>
#include <corona/core/Engine.h>
#include <corona/core/components/ActorComponents.h>
#include <corona/core/components/SceneComponents.h>

// 定义静态 ECS 注册表
entt::registry CoronaEngineAPI::registry_;

CoronaEngineAPI::Scene::Scene(void* surface, bool /*lightField*/)
    : sceneID(registry_.create()) {
    registry_.emplace<RenderTag>(sceneID);
    if (surface) {
        registry_.emplace_or_replace<Corona::Components::DisplaySurface>(sceneID, Corona::Components::DisplaySurface{surface});
    }
}

CoronaEngineAPI::Scene::~Scene() {
    registry_.destroy(sceneID);
}

void CoronaEngineAPI::Scene::setCamera(const ktm::fvec3& position, const ktm::fvec3& forward, const ktm::fvec3& worldUp, float fov) const {
    registry_.emplace_or_replace<Corona::Components::Camera>(sceneID, Corona::Components::Camera{fov, position, forward, worldUp});
}

void CoronaEngineAPI::Scene::setSunDirection(ktm::fvec3 direction) const {
    registry_.emplace_or_replace<Corona::Components::SunDirection>(sceneID, Corona::Components::SunDirection{direction});
}

void CoronaEngineAPI::Scene::setDisplaySurface(void* surface) {
    registry_.emplace_or_replace<Corona::Components::DisplaySurface>(sceneID, Corona::Components::DisplaySurface{surface});
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
