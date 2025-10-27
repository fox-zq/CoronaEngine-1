#include <corona/script/PythonHotfix.h>
#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <queue>
#include <ranges>
#include <regex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// 静态工具函数实现
int64_t PythonHotfix::GetCurrentTimeMsec() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

bool PythonHotfix::EndsWith(const std::string& str, const std::string& suffix) {
    if (str.size() < suffix.size()) return false;
    return std::equal(suffix.rbegin(), suffix.rend(), str.rbegin());
}

void PythonHotfix::NormalizeModuleName(std::string& path_like) {
    if (path_like.empty()) return;
    std::ranges::replace(path_like, '\\', '.');
    std::ranges::replace(path_like, '/', '.');
    static const std::regex pattern(R"(([^\.]*?)\.py$)");
    std::smatch match;
    if (std::regex_search(path_like, match, pattern) && match.size() > 1) {
        path_like = match[1].str();
    } else {
        size_t lastDot = path_like.find_last_of('.');
        if (lastDot != std::string::npos)
            path_like = path_like.substr(lastDot + 1);
        else
            path_like.clear();
    }
}

void PythonHotfix::TraverseDirectory(const std::filesystem::path& directory,
                                     std::queue<std::unordered_set<std::string>>& message_que,
                                     int64_t check_time_ms) {
    std::unordered_set<std::string> local_set;
    if (!std::filesystem::exists(directory)) return;

    auto ensure_init = [](const std::filesystem::path& dir) {
        auto initp = dir / "__init__.py";
        if (!std::filesystem::exists(initp)) {
            std::ofstream f(initp.string(), std::ios::app);
        }
    };
    ensure_init(directory);

    static const std::set<std::string> kSkip = {"__pycache__", "__init__.py", ".pyc", "StaticComponents.py"};

    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory)) {
        const auto& p = entry.path();
        if (entry.is_directory()) {
            ensure_init(p);
            continue;
        }
        if (!entry.is_regular_file()) continue;

        const std::string file_name = p.filename().string();
        if (!EndsWith(file_name, ".py")) continue;

        if (std::ranges::any_of(kSkip, [&](const std::string& s) { return EndsWith(file_name, s); })) continue;

        std::error_code ec;
        auto ftime = std::filesystem::last_write_time(p, ec);
        if (ec) continue;
        auto syst = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
        int64_t mod_ms = std::chrono::duration_cast<std::chrono::milliseconds>(syst.time_since_epoch()).count();
        if (check_time_ms - mod_ms > kFileRecentWindowMs) continue;

        std::string mod_name = p.string();
        NormalizeModuleName(mod_name);
        if (!mod_name.empty()) local_set.insert(mod_name);
    }

    if (!local_set.empty()) message_que.push(std::move(local_set));
}

void PythonHotfix::CheckPythonFileDependence() {
    if (packageSet.empty()) return;

    nanobind::gil_scoped_acquire gil;

    nanobind::module_ sys = nanobind::module_::import_("sys");
    nanobind::object modules_obj = nanobind::getattr(sys, "modules");

    nanobind::module_ types = nanobind::module_::import_("types");
    nanobind::object ModuleType = nanobind::getattr(types, "ModuleType");
    nanobind::module_ builtins = nanobind::module_::import_("builtins");
    nanobind::object isinstance_fn = nanobind::getattr(builtins, "isinstance");

    // 遍历 sys.modules.items() -> 强制快照，避免迭代期间 dict 发生变化
    nanobind::object items_obj = nanobind::getattr(modules_obj, "items")();
    nanobind::object list_fn = nanobind::getattr(builtins, "list");
    nanobind::object items_list = list_fn(items_obj);

    dependencyGraph.clear();

    // 使用快照进行迭代，避免 RuntimeError: dictionary changed size during iteration
    for (nanobind::handle h : items_list) {
        nanobind::tuple kv;
        try {
            kv = nanobind::cast<nanobind::tuple>(h);
        } catch (...) {
            continue;
        }
        if (kv.size() != 2) continue;
        nanobind::object key = kv[0];
        nanobind::object val = kv[1];

        bool is_mod = false;
        try {
            is_mod = nanobind::cast<bool>(isinstance_fn(val, ModuleType));
        } catch (...) {
            is_mod = false;
        }
        if (!is_mod) continue;

        std::string mod_name;
        try {
            mod_name = nanobind::cast<std::string>(nanobind::getattr(val, "__name__"));
        } catch (...) {
            continue;
        }

        nanobind::object dict_obj;
        try {
            dict_obj = nanobind::getattr(val, "__dict__");
        } catch (...) {
            continue;
        }

        // 遍历 module.__dict__.values() -> 同样转为 list 快照
        nanobind::object values_obj = nanobind::getattr(dict_obj, "values")();
        nanobind::object values_list = list_fn(values_obj);
        for (nanobind::handle vh : values_list) {
            // Use cast from handle to object instead of manual inc_ref/steal
            nanobind::object vv;
            try {
                vv = nanobind::cast<nanobind::object>(vh);
            } catch (...) {
                continue;
            }
            bool is_sub_mod = false;
            try { is_sub_mod = nanobind::cast<bool>(isinstance_fn(vv, ModuleType)); } catch (...) { is_sub_mod = false; }
            if (!is_sub_mod) continue;
            try {
                std::string imported = nanobind::cast<std::string>(nanobind::getattr(vv, "__name__"));
                dependencyGraph[imported].insert(mod_name);
            } catch (...) {
                // ignore
            }
        }
    }

    std::unordered_set<std::string> visited;
    std::queue<std::string> bfs;
    dependencyVec.clear();
    for (const auto& kvp : packageSet) {
        const std::string& mod = kvp.first;
        if (visited.find(mod) == visited.end()) {
            visited.insert(mod);
            bfs.push(mod);
            dependencyVec.push_back(mod);
        }
    }
    while (!bfs.empty()) {
        auto cur = bfs.front();
        bfs.pop();
        auto it = dependencyGraph.find(cur);
        if (it == dependencyGraph.end()) continue;
        for (const auto& dep : it->second) {
            if (visited.find(dep) == visited.end()) {
                visited.insert(dep);
                bfs.push(dep);
                dependencyVec.push_back(dep);
            }
        }
    }
}

bool PythonHotfix::ReloadPythonFile() {
    nanobind::gil_scoped_acquire gil;

    CheckPythonFileDependence();
    bool reload = !packageSet.empty();

    if (reload) {
        std::cout << "[Hotfix][Reload] dependency order (before reverse): ";
        for (size_t i = 0; i < dependencyVec.size(); ++i) {
            if (i) std::cout << " -> ";
            std::cout << dependencyVec[i];
        }
        std::cout << std::endl;
    }

    nanobind::module_ sys = nanobind::module_::import_("sys");
    nanobind::object modules_obj = nanobind::getattr(sys, "modules");

    nanobind::module_ importlib = nanobind::module_::import_("importlib");
    nanobind::object reload_fn = nanobind::getattr(importlib, "reload");
    nanobind::module_ types = nanobind::module_::import_("types");
    nanobind::object ModuleType = nanobind::getattr(types, "ModuleType");
    nanobind::module_ builtins = nanobind::module_::import_("builtins");
    nanobind::object isinstance_fn = nanobind::getattr(builtins, "isinstance");

    for (int i = static_cast<int>(dependencyVec.size()) - 1; i >= 0; --i) {
        const std::string& name = dependencyVec[i];
        std::cout << "[Hotfix][Reload] begin reload idx=" << (dependencyVec.size() - 1 - i)
                  << "/" << dependencyVec.size() << ": " << name << std::endl;

        nanobind::object old_mod;
        try {
            old_mod = nanobind::getattr(modules_obj, "get")(name.c_str());
        } catch (...) {
            old_mod = nanobind::none();
        }

        if (old_mod.is_none()) {
            try {
                nanobind::object import_module = nanobind::getattr(importlib, "import_module");
                old_mod = import_module(name.c_str());
            } catch (...) {
                std::cout << "[Hotfix][Reload] skip (module not found and import failed): " << name << std::endl;
                continue;
            }
        }

        bool is_mod = false;
        try {
            is_mod = nanobind::cast<bool>(isinstance_fn(old_mod, ModuleType));
        } catch (...) {
            is_mod = false;
        }
        if (!is_mod) {
            std::cout << "[Hotfix][Reload] skip (not a module): " << name << "\n";
            continue;
        }

        try {
            nanobind::object new_mod = reload_fn(old_mod);
            std::cout << "[Hotfix][Reload] reload ok: name=" << name << std::endl;
            // 可选：验证 sys.modules[name] 指向新对象
        } catch (const std::exception& e) {
            std::cout << "[Hotfix][Reload] reload failed: " << name << " err=" << e.what() << std::endl;
            continue;
        } catch (...) {
            std::cout << "[Hotfix][Reload] reload failed: " << name << " (unknown error)" << std::endl;
            continue;
        }
    }

    packageSet.clear();
    dependencyGraph.clear();
    dependencyVec.clear();

    return reload;
}
