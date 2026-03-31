# Corona Engine Python API 使用指南（OOP 版）

版本: 2.0  
日期: 2026-03-31

本文档展示当前 OOP 风格的 Python API 使用方式，示例与绑定接口严格对齐：
- 顶层类名：Geometry, Mechanics, Optics, Acoustics, Kinematics, Actor, ActorProfile, Camera, ImageEffects, Environment, Scene
- Profile 使用 ActorProfile（顶层类），而非 Actor.Profile
- Camera 合并了原 Viewport 的功能（图像效果、尺寸管理、截图等）
- API 不暴露任何句柄或内部几何指针，完全以对象交互

---

## 目录
- 准备工作
- Geometry（几何体）
- 组件（Optics / Mechanics / Kinematics / Acoustics）
- Actor 与 ActorProfile
- Camera 与 ImageEffects
- Environment 与 Scene
- 完整示例
- 常见问题与最佳实践

---

## 准备工作

```python
# 模块名以引擎集成为准，通常为 corona_engine
from corona_engine import (
    Geometry, Mechanics, Optics, Kinematics, Acoustics,
    Actor, ActorProfile,
    Camera, ImageEffects,
    Environment, Scene,
)
```

---

## Geometry（几何体）

Geometry 是所有组件的锚点，存放模型与局部变换（位置/旋转/缩放），旋转统一使用欧拉角（ZYX）。

```python
geo = Geometry("assets/model/character.obj")

# 设置局部变换
geo.set_position([0.0, 0.0, 0.0])
geo.set_rotation([0.0, 1.57, 0.0])  # pitch, yaw, roll（单位通常为弧度）
geo.set_scale([1.0, 1.0, 1.0])

# 读取局部变换
pos = geo.get_position()
rot = geo.get_rotation()
sca = geo.get_scale()
```

注意：API 内部不存放世界矩阵，容器仅存放局部参数；世界矩阵由系统在需要时计算或组合。

---

## 组件（Optics / Mechanics / Kinematics / Acoustics）

组件都需要绑定到一个 Geometry 实例上创建：

```python
geo = Geometry("assets/model/animated_character.obj")

optics = Optics(geo)       # 渲染相关（如蒙皮缓冲会按需自动创建）
mechanics = Mechanics(geo) # 力学/物理（如 AABB 等）
kinematics = Kinematics(geo)  # 动画（提供播放控制）
acoustics = Acoustics(geo)    # 声学（支持音量等参数）

# Kinematics 常用操作
kinematics.set_animation(0)
kinematics.play_animation(1.0)
# ...
kinematics.stop_animation()
print(kinematics.get_animation_index(), kinematics.get_current_time())

# Acoustics 音量
acoustics.set_volume(0.8)
print(acoustics.get_volume())
```

约束：同一个 Profile 中的所有组件必须与 Profile 指定的 Geometry 是同一个实例（系统会做一致性校验）。

---

## Actor 与 ActorProfile

Actor 表示一个实体；Actor 可以持有多个 Profile，每个 Profile 是一组“组件 + 几何体”的组合。

```python
actor = Actor()

# 创建 Profile（顶层类 ActorProfile，不是 Actor.Profile）
profile = ActorProfile()

geo = Geometry("assets/model/character.obj")
profile.geometry = geo
profile.optics = Optics(geo)
profile.mechanics = Mechanics(geo)
profile.kinematics = Kinematics(geo)
profile.acoustics = Acoustics(geo)

# 添加到 Actor，并设为当前激活配置
actor.add_profile(profile)
actor.set_active_profile(profile)

# 查询
active = actor.get_active_profile()
print("profile_count:", actor.profile_count())

# 移除配置
actor.remove_profile(profile)
```

组件复用：允许将同一个组件实例复用到多个 Profile，但前提是这些 Profile 的 geometry 是同一个对象；否则会被拒绝（以确保数据一致性）。

---

## Camera 与 ImageEffects

Camera 存放相机位置、方向、上向量、视野角，同时集成了尺寸管理、图像后处理和截图等功能。

```python
# 相机
cam = Camera()
cam.set(
    position=[0.0, 5.0, -10.0],
    forward=[0.0, 0.0, 1.0],
    world_up=[0.0, 1.0, 0.0],
    fov=60.0,
)
# 如需绑定渲染目标（平台相关指针/句柄），传入整数形式：
# cam.set_surface(surface_ptr_as_int)

# 尺寸与视口矩形
cam.set_size(1920, 1080)
cam.set_viewport_rect(0, 0, 1920, 1080)

# 可选图像效果
fx = ImageEffects()
cam.set_image_effects(fx)

if cam.has_image_effects():
    _fx = cam.get_image_effects()

cam.remove_image_effects()

# 交互/输出
cam.pick_actor_at_pixel(100, 200)
cam.save_screenshot("screenshot.png")
```

说明：Camera 可能没有 ImageEffects，需先判断 `has_image_effects()` 再 `get_image_effects()`。

---

## Environment 与 Scene

```python
scene = Scene()

# 环境
env = Environment()
env.set_sun_direction([1.0, -1.0, 0.0])
env.set_floor_grid(True)  # 当前仅打印提示
scene.set_environment(env)

# Actor
actor = Actor()
prof = ActorProfile()
geo = Geometry("assets/model/bunny2.obj")
prof.geometry = geo
prof.optics = Optics(geo)
actor.add_profile(prof)
scene.add_actor(actor)

# Camera
cam = Camera()
cam.set([0, 2, -6], [0, 0, 1], [0, 1, 0], 60)
cam.set_size(1280, 720)
scene.add_camera(cam)

print("actors:", scene.actor_count())
print("cameras:", scene.camera_count())

# 查询/移除/清空
if scene.has_actor(actor):
    scene.remove_actor(actor)
scene.clear_cameras()
```

---

## 完整示例

```python
from corona_engine import *

# 场景与环境
scene = Scene()
env = Environment()
env.set_sun_direction([0.3, -1.0, 0.2])
scene.set_environment(env)

# 角色（一个 Profile）
geo = Geometry("assets/model/dancing_vampire.dae")
opt = Optics(geo)
kin = Kinematics(geo)
kin.set_animation(0)
kin.play_animation(1.0)

prof = ActorProfile()
prof.geometry = geo
prof.optics = opt
prof.kinematics = kin

actor = Actor()
actor.add_profile(prof)
scene.add_actor(actor)

# 相机
cam = Camera()
cam.set([0, 2, -6], [0, 0, 1], [0, 1, 0], 60)
cam.set_size(1920, 1080)
scene.add_camera(cam)

# 运行时：按需刷新/驱动由引擎系统侧处理（此处仅展示 API 用法）
```

---

## 常见问题与最佳实践

- 组件与 Geometry 的一致性：同一 Profile 内的组件必须来自同一个 Geometry 实例，否则 `Actor.add_profile()` 会拒绝。
- OOP 封装：API 不暴露任何句柄或内部指针，Python 端以对象交互为主。
- 图像效果判空：Camera 可能没有 ImageEffects，先调用 `has_image_effects()` 再 `get_image_effects()`。
- 动画单位：旋转使用欧拉角（ZYX），数值单位通常为弧度。
- 生命周期：Python 持有对象并负责释放；避免让局部变量在被 Scene/Camera/Actor 使用后过早回收。
- 组件复用：允许复用组件，但必须与 Profile 的 Geometry 相同；跨几何体复用会被拒绝。
- 线程/系统：底层容器是系统的“单一事实来源”，API 读取/写入直接作用于容器，减少状态漂移；请避免自行缓存关键状态。
