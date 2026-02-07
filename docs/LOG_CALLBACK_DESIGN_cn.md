# 日志回调系统设计文档

> **状态**: 提案  
> **作者**: CoronaEngine Team  
> **日期**: 2026-02-07  
> **涉及模块**: CoronaFramework (Kernel/Logger), CoronaEngine (ScriptSystem/Python)

---

## 1. 背景与需求

CoronaEngine 的前端编辑器（基于 CEF + Vue）需要实时展示引擎内部的日志输出。当前日志系统仅输出到**控制台**和**文件**，前端无法获取日志内容。

### 目标

- Python 线程初始化时，将一个 Python 函数注册给日志系统，负责把日志转发到前端界面。
- 整个链路需要线程安全，不能引入 GIL 死锁风险。
- 对现有日志系统的改动最小化，保持 `CFW_LOG_*` 宏零开销调用的特性。

---

## 2. 现有架构

```
┌──────────────┐     ┌──────────────────┐     ┌───────────────┐
│ CFW_LOG_*    │ ──> │  Quill Backend   │ ──> │ ConsoleSink   │ ──> stdout
│ PY_LOG_*     │     │  (后端线程)       │     ├───────────────┤
│ VUE_LOG_*    │     │                  │     │ FileSink      │ ──> logs/*.log
└──────────────┘     └──────────────────┘     └───────────────┘
```

| 组件 | 说明 |
|------|------|
| `CFW_LOG_*` 宏 | 编译期展开，直接调用 Quill 前端 API，~10-15ns 延迟 |
| `CoronaLogger` | 静态工具类，管理 Quill Logger 的初始化和生命周期 |
| Quill Backend | 独立线程，异步消费日志消息并分发到各 Sink |
| `ConsoleSink` | 输出到控制台 (stdout) |
| `FileSink` | 输出到带时间戳的日志文件 |

### 核心约束

- **Quill Sink 的 `write_log()` 在 Quill Backend 线程中被调用**，不在任何用户线程中。
- **Python 函数调用必须持有 GIL**。在 Quill Backend 线程中直接获取 GIL 会有死锁风险。
- Python 解释器的生命周期短于引擎，Sink 不能持有 Python 对象的引用。

---

## 3. 方案对比

| 方案 | 描述 | 优点 | 缺点 |
|------|------|------|------|
| **A. CallbackSink + 队列 + 主动拉取** | Sink 将日志推入线程安全队列，Python 侧主动 `drain` | 无 GIL 死锁风险；生命周期解耦 | Python 需在 update 循环中轮询 |
| B. 直接注册 Python 回调 | `set_log_callback(py_func)` | API 最简洁 | Quill Backend 线程调用回调需获取 GIL → 死锁；Python 关闭后回调悬空 |
| C. EventBus 转发 | 日志通过引擎 EventBus 发事件 | 复用现有架构 | 日志是基础设施，不应依赖 EventBus；高频日志冲击事件总线 |

**推荐方案 A**，理由：

1. 彻底避免 GIL 死锁——Quill Backend 线程永远不触碰 Python。
2. CallbackSink 的生命周期独立于 Python 解释器。
3. 消费者自行控制拉取时机和频率，天然适配 Python 的 update 循环。

---

## 4. 推荐方案详细设计

### 4.1 数据流总览

```
┌─────────────┐     ┌────────────────┐     ┌────────────────┐     ┌─────────────┐
│ CFW_LOG_*   │ ──> │ Quill Backend  │ ──> │ CallbackSink   │ ──> │  线程安全    │
│ (任意线程)   │     │ (后端线程)      │     │ ::write_log()  │     │  消息队列    │
└─────────────┘     └────────────────┘     └────────────────┘     └──────┬──────┘
                                                                         │
                    ┌────────────────┐     ┌────────────────┐            │
                    │ 前端 (CEF/Vue) │ <── │ Python 回调函数  │ <── drain_logs()
                    └────────────────┘     │ (Python 线程)   │   (Python 线程主动拉取)
                                           └────────────────┘
```

### 4.2 新增数据结构：`LogEntry`

```cpp
// include/corona/kernel/core/callback_sink.h
namespace Corona::Kernel {

struct LogEntry {
    std::string level;      // "TRACE", "DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"
    std::string message;    // 已格式化的完整日志文本
    uint64_t    timestamp;  // 时间戳（纳秒，epoch）
};

}  // namespace Corona::Kernel
```

每条 `LogEntry` 是一个**值类型**，被拷贝进队列后与 Quill 内部的缓冲区完全解耦。

### 4.3 新增类：`CallbackSink`

```cpp
// include/corona/kernel/core/callback_sink.h
#pragma once
#include <quill/sinks/Sink.h>

#include <mutex>
#include <string>
#include <vector>

namespace Corona::Kernel {

/// 自定义 Quill Sink，将格式化后的日志条目推入线程安全队列。
/// 消费者（如 Python 线程）通过 drain() 主动拉取。
class CallbackSink : public quill::Sink {
public:
    /// 队列最大容量，超出后丢弃最旧条目
    static constexpr size_t kMaxQueueSize = 10000;

    // ========== Quill Sink 接口 (由 Quill Backend 线程调用) ==========

    void write_log(quill::MacroMetadata const* log_metadata,
                   uint64_t                    log_timestamp,
                   std::string_view            thread_id,
                   std::string_view            thread_name,
                   std::string const&          process_id,
                   std::string_view            logger_name,
                   quill::LogLevel             log_level,
                   std::string_view            log_level_description,
                   std::string_view            log_level_short_code,
                   std::vector<std::pair<std::string, std::string>> const* named_args,
                   std::string_view            log_message,
                   std::string_view            log_statement) override
    {
        LogEntry entry;
        entry.level     = std::string(log_level_description);
        entry.message   = std::string(log_statement);  // 完整格式化后的日志行
        entry.timestamp = log_timestamp;

        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (queue_.size() >= kMaxQueueSize) {
            queue_.erase(queue_.begin());  // 丢弃最旧条目
        }
        queue_.push_back(std::move(entry));
    }

    void flush_sink() override { /* no-op: 队列不需要 flush */ }

    // ========== 公共 API (由消费者线程调用) ==========

    /// 拉取并清空所有待处理的日志条目
    /// @return 自上次 drain 以来的所有 LogEntry
    std::vector<LogEntry> drain() {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        std::vector<LogEntry> result;
        result.swap(queue_);
        return result;
    }

    /// 查询队列是否为空
    bool empty() const {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return queue_.empty();
    }

    /// 获取当前队列大小
    size_t size() const {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        return queue_.size();
    }

private:
    mutable std::mutex      queue_mutex_;
    std::vector<LogEntry>   queue_;
};

}  // namespace Corona::Kernel
```

**设计要点**：

- `write_log()` 中只做 `string` 拷贝 + `mutex` 锁队列，开销极小（~100ns 级）。
- `kMaxQueueSize` 防止 Python 侧长时间不拉取导致内存膨胀。
- 使用 `std::vector` 而非 `std::queue`，方便 `drain()` 时一次性 `swap` 清空。

### 4.4 扩展 `CoronaLogger` API

在 `i_logger.h` 中新增：

```cpp
class CoronaLogger {
public:
    // ========== 现有 API（保持不变）==========
    static void initialize();
    static void set_log_level(LogLevel level);
    static void flush();
    static quill::Logger* get_logger();

    // ========== 新增：回调 Sink 管理 ==========

    /// 获取全局 CallbackSink 实例
    /// 首次调用时自动创建并注入到 Logger 的 sinks 列表中
    static CallbackSink* get_callback_sink();

    /// 便捷方法：拉取所有待处理的日志条目
    /// 等价于 get_callback_sink()->drain()
    static std::vector<LogEntry> drain_logs();

    /// 设置 CallbackSink 的独立日志级别过滤
    /// 例如只让 INFO 及以上的日志进入前端队列，避免 TRACE/DEBUG 的大量推送
    static void set_callback_sink_level(LogLevel min_level);

private:
    CoronaLogger() = delete;
};
```

### 4.5 `CoronaLogger` 实现扩展

在 `i_logger.cpp` 的 `initialize_impl()` 中，将 `CallbackSink` 与现有 Sink 一同注入：

```cpp
// i_logger.cpp 匿名命名空间新增
static std::shared_ptr<CallbackSink> g_callback_sink = nullptr;

void initialize_impl() {
    // ... 现有的控制台和文件 Sink 创建 ...

    // 创建 CallbackSink
    g_callback_sink = std::make_shared<CallbackSink>();

    // 三个 sinks 一起注入
    std::vector<std::shared_ptr<quill::Sink>> sinks;
    sinks.push_back(console_sink);
    sinks.push_back(file_sink);
    sinks.push_back(g_callback_sink);  // 新增

    g_logger = quill::Frontend::create_or_get_logger(
        "corona_default", std::move(sinks), formatter_options);

    // ... 其余初始化 ...
}

// 新增实现
CallbackSink* CoronaLogger::get_callback_sink() {
    initialize();
    return g_callback_sink.get();
}

std::vector<LogEntry> CoronaLogger::drain_logs() {
    if (!g_callback_sink) return {};
    return g_callback_sink->drain();
}

void CoronaLogger::set_callback_sink_level(LogLevel min_level) {
    // 可在 CallbackSink 中增加一个 min_level_ 字段进行过滤
    // 或者利用 Quill 的 per-sink filter 机制
}
```

### 4.6 Python 绑定

在 `engine_bindings.cpp` 的 `BindAll()` 中新增：

```cpp
#include <corona/kernel/core/callback_sink.h>

void BindAll(nanobind::module_& m) {
    // ... 现有绑定 ...

    // ================================================================
    // Logger: 日志前端转发接口
    // ================================================================
    nb::class_<Corona::Kernel::LogEntry>(m, "LogEntry")
        .def_ro("level",     &Corona::Kernel::LogEntry::level,
                "Log level string: TRACE/DEBUG/INFO/WARNING/ERROR/CRITICAL")
        .def_ro("message",   &Corona::Kernel::LogEntry::message,
                "Formatted log message")
        .def_ro("timestamp", &Corona::Kernel::LogEntry::timestamp,
                "Timestamp in nanoseconds since epoch");

    m.def("drain_logs", []() -> std::vector<Corona::Kernel::LogEntry> {
        return Corona::Kernel::CoronaLogger::drain_logs();
    }, "Drain all pending log entries from the engine log queue");
}
```

### 4.7 Python 侧使用示例

```python
import CoronaEngine

# ============================================================
# 方式一：在 update 循环中轮询（推荐）
# ============================================================
def run(is_reload):
    """引擎每帧调用"""
    # ... 其他游戏逻辑 ...

    # 拉取本帧新产生的日志，转发到前端
    for entry in CoronaEngine.drain_logs():
        push_log_to_frontend(entry.level, entry.message)


def push_log_to_frontend(level: str, message: str):
    """将日志发送到 CEF/Vue 前端"""
    import json
    payload = json.dumps({
        "type": "engine_log",
        "level": level,
        "message": message
    })
    # 通过 JS Bridge 发送到前端
    editor.execute_js(f"window.dispatchEvent(new CustomEvent('engine-log', {{detail: {payload}}}));")


# ============================================================
# 方式二：在 initialize 中启动独立的日志消费线程
# ============================================================
import threading, time

def log_consumer_thread():
    """后台线程，定期拉取日志"""
    while True:
        entries = CoronaEngine.drain_logs()
        for entry in entries:
            push_log_to_frontend(entry.level, entry.message)
        time.sleep(0.05)  # 50ms 拉取一次

def initialize():
    t = threading.Thread(target=log_consumer_thread, daemon=True)
    t.start()
```

---

## 5. 实现要点

### 5.1 线程安全保证

| 操作 | 调用线程 | 锁 | 说明 |
|------|---------|-----|------|
| `CallbackSink::write_log()` | Quill Backend | `queue_mutex_` | 仅做内存拷贝 + 入队 |
| `CallbackSink::drain()` | Python 线程 | `queue_mutex_` | swap 一次性清空 |
| GIL 获取 | 仅 Python 线程 | — | Quill Backend **永不**触碰 GIL |

### 5.2 队列容量保护

```
kMaxQueueSize = 10000
```

当 Python 侧长时间不调用 `drain_logs()`（如脚本异常、前端未连接）时，队列达到上限后自动丢弃最旧条目。这保证了即使消费者缺席，内存占用也是有界的。

### 5.3 日志级别过滤

建议在 `CallbackSink` 中维护独立的最低级别过滤：

```cpp
class CallbackSink : public quill::Sink {
    // ...
    LogLevel min_level_ = LogLevel::info;  // 默认只转发 INFO 及以上

    void write_log(...) override {
        if (log_level < to_quill_level(min_level_)) return;  // 级别过滤
        // ... 入队逻辑 ...
    }
};
```

这样 TRACE/DEBUG 级别的高频日志不会进入队列，减少不必要的开销。

### 5.4 Python 解释器关闭时的行为

- `CallbackSink` **不持有任何 Python 对象**，因此 Python 关闭后 Sink 依然安全。
- 只是不再有人调用 `drain()`，队列达到上限后自动丢弃。
- 无需在 `PythonAPI::~PythonAPI()` 中做额外清理。

### 5.5 性能分析

| 步骤 | 开销 |
|------|------|
| `CFW_LOG_*` 宏调用 | ~10-15ns（Quill 前端，无变化） |
| Quill Backend → `CallbackSink::write_log()` | ~100-200ns（string 拷贝 + mutex） |
| `drain_logs()` | ~O(n) swap，n 为积累条目数 |
| Python 遍历 + JS Bridge | 取决于前端，通常 ~1ms/100条 |

对于正常日志频率（~100-1000 条/秒），额外的 CallbackSink 开销可忽略不计。

---

## 6. 需要改动的文件清单

| 文件 | 所属模块 | 改动类型 | 说明 |
|------|---------|---------|------|
| `include/corona/kernel/core/callback_sink.h` | CoronaFramework | **新增** | `LogEntry` 结构体 + `CallbackSink` 类 |
| `include/corona/kernel/core/i_logger.h` | CoronaFramework | **修改** | 添加 `get_callback_sink()` / `drain_logs()` / `set_callback_sink_level()` 声明 |
| `src/kernel/core/i_logger.cpp` | CoronaFramework | **修改** | 实现新 API；在 `initialize_impl()` 中注入 CallbackSink |
| `src/systems/script/python/engine_bindings.cpp` | CoronaEngine | **修改** | 绑定 `LogEntry` 类和 `drain_logs()` 函数 |

---

## 7. 未来扩展

- **结构化日志字段**：`LogEntry` 可扩展 `source_file`, `line_number`, `thread_id` 等字段，让前端实现过滤和搜索。
- **多消费者支持**：如果将来有多个前端实例（如远程调试器），可改为每个消费者持有独立的 `CallbackSink` 实例。
- **日志回放**：结合 `FileSink` 的日志文件，前端可以实现历史日志加载和实时日志的无缝切换。
