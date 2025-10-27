# CoronaEngine C++ 依赖关系图

本文根据当前 CMake 配置及核心头文件，实现对 CoronaEngine 中各 C++ 目标、子系统与外部库之间关系的梳理。

## 引擎聚合层
- **CoronaEngine**（`src/CMakeLists.txt`）是对外导出的静态聚合库，链接了引擎核心层（`CoronaCore`、`CoronaCoreServices`、`CoronaCoreKernel`）、所有运行时系统、通用工具头文件、线程原语以及日志/资源辅助模块。通过该目标向下游传播的外部依赖包括 `EnTT::EnTT`、`ktm`、`CoronaResource::Resource`、`Corona::Logger`、`cabbage::concurrent`，以及按需启用的 `CabbageHardware`。
- **EngineFacade**（`include/corona/core/Engine.h`）封装了对静态引擎单例的访问，负责：
  - 使用 `EngineKernel` 完成 `ISystem` 的注册与查询；
  - 为每个系统维护 `SafeCommandQueue` 枢纽，支持跨线程调度；
  - 通过 `cache<T>()` 暴露基于 `SafeDataCache` 的共享中心；
  - 管理 `SystemRegistry`，记录系统元数据；
  - 将资源、日志、命令调度等服务分发给 `CoronaCoreServices`。

内核要求注册的系统继承自 `ThreadedSystem`，且配备具名命令队列（如 `Engine::add_queue(name(), std::make_unique<SafeCommandQueue>())`）。

## 内部目标与依赖
```
CoronaEngine (STATIC)
├─ CoronaCore (STATIC)
│  ├─ corona::core::kernel  → corona::interfaces
│  ├─ corona::core::services → {corona::interfaces, corona::thread, CoronaResource::Resource, Corona::Logger}
│  ├─ corona::interfaces    (INTERFACE 头文件)
│  ├─ corona::utils         (INTERFACE 头文件 + compiler_features)
│  ├─ 外部依赖: ktm（数学库）、EnTT::EnTT（ECS 注册表）
│  └─ 私有依赖: corona::thread, CoronaResource::Resource, Corona::Logger
├─ CoronaSystemAnimation (STATIC)
│  ├─ 公共依赖: corona::system::interface, corona::core, CoronaResource::Resource
│  └─ 私有依赖: corona::script::python（动画相关的 Python 钩子）
├─ CoronaSystemAudio (STATIC)    → corona::system::interface, corona::core
├─ CoronaSystemDisplay (STATIC)  → corona::system::interface, corona::core
├─ CoronaSystemRendering (STATIC)
│  ├─ 公共依赖: corona::system::interface, corona::core, CoronaResource::Resource, CabbageHardware
│  └─ 对外暴露渲染事件（`SceneEvents.h`）供其他模块消费
├─ CoronaScriptPython (STATIC)
│  ├─ 公共依赖: corona::core, Corona::Logger、python313 导入库及 Python 头/库目录
│  └─ 提供 `EngineScripts`、`PythonAPI`、`PythonBridge`、`PythonHotfix` 的公共头文件
├─ CoronaSystemInterface (INTERFACE)
│  └─ 依赖 corona::interfaces 与 Corona::Logger，提供系统层的通用日志能力
├─ CoronaThread (INTERFACE)
│  ├─ 暴露 `SafeCommandQueue`、`SafeDataCache` 等实现
│  └─ 链接 Corona::Logger 与 cabbage::concurrent，提供日志与无锁原语
├─ CoronaUtils (INTERFACE)       → 发布 `include/corona/utils` 下的头文件
├─ CoronaInterfaces (INTERFACE)  → 发布 `ISystem`、`ThreadedSystem`、`ServiceLocator` 等接口
└─ 各系统枢纽目标（INTERFACE/STATIC），由 `src/systems/CMakeLists.txt` 汇总
```

### 运行时可执行文件
- **Corona_runtime**（`engine/CMakeLists.txt`）链接 `CoronaEngine` 与 `EnTT::EnTT`，并附加运行时复制步骤：
  - `corona_install_runtime_deps`：复制 `CoronaEngine` 收集到的 Python DLL/PDB；
  - `helicon_install_runtime_deps`（若可用）：部署 Helicon/Vulkan 组件；
  - `corona_install_corona_editor`：在启用编辑器构建时镜像前后端资源；
  - 通过 `cmake -E copy_directory` 拷贝共享 `assets/` 目录。

### 示例程序
- `corona_add_example()`（`examples/CMakeLists.txt`）统一示例目标的创建流程。示例会链接 `glfw`、`CoronaEngine`、`cabbage::concurrent`，并继承相同的运行时依赖复制逻辑。

## CMake 引入的外部库
- **CoronaResource::Resource**：资源加载与管理，与运行时依赖收集工具共享。
- **Corona::Logger**：日志后端，被线程与系统层广泛使用。
- **cabbage::concurrent**：无锁并发原语，为 `SafeCommandQueue` 提供支持。
- **CabbageHardware**：渲染后端（Vulkan/Helicon 集成），由 `CoronaSystemRendering` 使用。
- **EnTT::EnTT**：实体组件系统注册表，由 `CoronaCore` 暴露并被系统/示例消费。
- **ktm**：数学库，可由 `CoronaCore` 引入。
- **python313**：脚本子系统使用的嵌入式解释器。
- **glfw**：窗口与输入库，仅在启用 `CORONA_BUILD_EXAMPLES` 时由示例链接。
- **Vision**：可选视觉模块，受 `CORONA_BUILD_VISION` 控制；启用后由 `corona_third_party.cmake` 拉取并对外可用。

## 运行时数据与控制流程
1. **系统注册**：`Engine::register_system<T>()`（见 `Engine.h`）实例化系统，确保其队列存在，并调用 `configure_system`。系统继承自 `ISystem`/`ThreadedSystem`，从而访问命令队列 API。
2. **命令队列**：`SafeCommandQueue` 为每个系统提供多生产者、单消费者队列。系统可通过 `Engine::get_queue(name)` 向自身或其他系统派发任务。

4. **数据缓存**：`Engine::cache<T>()` 返回共享的 `SafeDataCache`，用于缓存动画状态等资源。缓存会为每个 ID 加锁，若尝试失败会重试以避免饥饿。
5. **资源加载**：`Engine::resources()` 提供全局 `ResourceManager`。系统使用 `ResourceId::from(...)` 与 `load_once_async(...)` 获取资源；回调涉及系统状态时需重新派发到所属系统队列。
6. **Python 桥接**：脚本模块通过 `PythonBridge::set_sender` 将事件转发到主线程（`Engine::get_queue("MainThread")`）。`AnimationSystem::send_collision_event` 给出了示例。
7. **变换与事件**：变换更新将零向量视为“无变化”；若需重置，请发送极小值或额外标志，防止被忽略。

## 影响依赖的配置选项
- `BUILD_CORONA_RUNTIME`、`BUILD_CORONA_EDITOR`、`CORONA_BUILD_EXAMPLES`、`CORONA_BUILD_VISION` 会整体开启/关闭相关模块及其依赖目标。
- `CORONA_PYTHON_USE_EMBEDDED_FALLBACK` 强制使用内置 Python，确保 `CoronaScriptPython` 及运行时依赖一致。
- `CORONA_CHECK_PY_DEPS` / `CORONA_AUTO_INSTALL_PY_DEPS` 控制配置阶段是否校验、安装 Python 依赖（由 `corona_python.cmake` 执行）。

## 如何使用本图
- 引入新模块时，参考上述目标图确保其链接层级正确（interfaces → core → systems → runtime）。
- 新增依赖时优先在对应模块的 CMake 文件中声明，而非直接修改 `CoronaEngine`。
- 若新系统需要外部资源，更新 `corona_third_party.cmake` 并复用 `corona_runtime_deps.cmake` / `corona_editor.cmake` 的资源复制模式。

本文基于当前的 CMakeLists 与代表性头文件（尤其是 `include/corona/core/Engine.h`）。新增目标或跨模块依赖时请同步更新。