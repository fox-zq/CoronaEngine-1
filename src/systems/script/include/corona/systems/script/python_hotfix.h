#pragma once

#include <filesystem>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// PythonHotfix: 负责扫描 Python 脚本改动、计算依赖并执行重载
// 时间统一：毫秒（ms）
struct PythonHotfix {
    static constexpr int64_t kFileRecentWindowMs = 1000;  // 最近 1s 内修改视为变更

    // 统计信息（可扩展）
    int pyFileCount = 0;

    // 扫描目录，收集最近修改的模块名（push 进 MessageQue）
    static void TraverseDirectory(const std::filesystem::path& directory,
                                  std::queue<std::unordered_set<std::string>>& message_que,
                                  int64_t check_time_ms);

    // 计算依赖（构建反向依赖图：被 import 的模块 -> import 它的模块集合）
    void CheckPythonFileDependence();

    // 依据已记录的 packageSet + 依赖进行重载，返回是否有实际重载
    bool ReloadPythonFile();

    // 获取当前时间（毫秒）
    static int64_t GetCurrentTimeMsec();

    // 工具：判断字符串后缀
    static bool EndsWith(const std::string& str, const std::string& suffix);

    // 工具：路径转模块名（去除分隔符与 .py 后缀）
    static void NormalizeModuleName(std::string& path_like);

    // 已修改模块集合（基础重载顺序按依赖拓扑反向执行）
    std::unordered_map<std::string, int64_t> packageSet;
    std::unordered_map<std::string, std::unordered_set<std::string>> dependencyGraph;
    std::vector<std::string> dependencyVec;
};
