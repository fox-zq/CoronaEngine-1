#pragma once

#include <ktm/ktm.h>
#include <entt/entt.hpp>
#include <string>
#include <stdexcept>
#include <corona/core/components/SceneComponents.h>

struct CoronaEngineAPI {
    CoronaEngineAPI() = delete;
    ~CoronaEngineAPI() = delete;

    struct RenderTag {};
    struct AnimationTag {};
    struct AudioTag {};
    struct DisplayTag {};

    struct Actor {
       public:
        Actor(const std::string& path = "");
        ~Actor();

        void move(ktm::fvec3 pos) const;
        void rotate(ktm::fvec3 euler) const;
        void scale(ktm::fvec3 size) const;

       private:
        entt::entity actorID;
    };

    struct Scene {
       public:
        Scene(void* surface = nullptr, bool lightField = false);
        ~Scene();

        // 兼容旧版API的方法
        void setCamera(const ktm::fvec3& position, const ktm::fvec3& forward, const ktm::fvec3& worldUp, float fov) const;
        void setSunDirection(ktm::fvec3 direction) const;
        void setDisplaySurface(void* surface);
        
        // 新增的多相机支持方法
        entt::entity createCamera(const std::string& name = "", 
                                  const ktm::fvec3& position = {0.0f, 0.0f, 5.0f}, 
                                  const ktm::fvec3& forward = {0.0f, 0.0f, -1.0f}, 
                                  const ktm::fvec3& worldUp = {0.0f, 1.0f, 0.0f}, 
                                  float fov = 45.0f, 
                                  bool isMain = false) const;
        
        // 新增的多光源支持方法
        entt::entity createLight(const std::string& name = "", 
                                Corona::Components::Light::Type type = Corona::Components::Light::Type::Directional, 
                                const ktm::fvec3& color = {1.0f, 1.0f, 1.0f}, 
                                float intensity = 1.0f, 
                                bool isMain = false) const;
        
        // 设置主相机
        void setMainCamera(entt::entity cameraEntity) const;
        
        // 设置主光源
        void setMainLight(entt::entity lightEntity) const;

       private:
        entt::entity sceneID;
    };

   private:
    static entt::registry registry_;
};