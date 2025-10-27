#define PY_SSIZE_T_CLEAN
#include <corona/script/PythonAPI.h>
#include <corona_logger.h>
#include <windows.h>

#include <cstdlib>
#include <iostream>
#include <ranges>
#include <regex>
#include <set>
#include <unordered_map>
#include <typeinfo>

// Diagnostic helper to print Python/C++ environment when imports fail.
static void PrintPythonDiagnostics(const char* where) {
    try {
        nanobind::gil_scoped_acquire gil;
        std::cout << "[Hotfix][Diag] " << where << " -- Py_IsInitialized=" << (Py_IsInitialized() ? 1 : 0) << std::endl;
        try {
            std::cout << "[Hotfix][Diag] CWD=" << std::filesystem::current_path().string() << std::endl;
        } catch (const std::exception& e) {
            std::cout << "[Hotfix][Diag] CWD error: " << e.what() << std::endl;
        }

        PyObject* sysmod = PyImport_ImportModule("sys");
        if (sysmod) {
            PyObject* path = PyObject_GetAttrString(sysmod, "path");
            if (path && PyList_Check(path)) {
                std::cout << "[Hotfix][Diag] sys.path:";
                Py_ssize_t n = PyList_Size(path);
                for (Py_ssize_t i = 0; i < n; ++i) {
                    PyObject* item = PyList_GetItem(path, i); /* borrowed ref */
                    if (item) {
                        const char* s = PyUnicode_AsUTF8(item);
                        if (s) std::cout << " '" << s << "'";
                    }
                }
                std::cout << std::endl;
            }
            Py_XDECREF(path);
            Py_DECREF(sysmod);
        } else {
            std::cout << "[Hotfix][Diag] failed to import sys module" << std::endl;
        }

        if (PyErr_Occurred()) {
            std::cout << "[Hotfix][Diag] PyErr_Occurred() true. Printing traceback:" << std::endl;
            PyErr_Print();
        } else {
            std::cout << "[Hotfix][Diag] PyErr_Occurred() false" << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[Hotfix][Diag] exception in diagnostics: " << e.what() << std::endl;
    }
}

// 声明由 nanobind NB_MODULE(CoronaEngine, m) 生成的初始化函数
extern "C" PyObject* PyInit_CoronaEngine();

// Unified path configuration
namespace PathCfg {
inline std::string Normalize(std::string s) {
    std::ranges::replace(s, '\\', '/');
    return s;
}

inline const std::string& EngineRoot() {
    static std::string root = [] {
        std::string resultPath;
        std::string runtimePath = std::filesystem::current_path().string();
        std::regex pattern(R"((.*)CoronaEngine\b)");
        std::smatch matches;
        if (std::regex_search(runtimePath, matches, pattern)) {
            if (matches.size() > 1) {
                resultPath = matches[1].str() + "CoronaEngine";
            } else {
                throw std::runtime_error("Failed to resolve source path.");
            }
        }
        return Normalize(resultPath);
    }();
    return root;
}

inline const std::string& EditorBackendRel() {
    static const std::string rel = "Editor/CabbageEditor/Backend";
    return rel;
}

inline const std::string& EditorBackendAbs() {
    static const std::string abs = [] { return Normalize(EngineRoot() + "/" + EditorBackendRel()); }();
    return abs;
}

inline std::string RuntimeBackendAbs() {
    auto p = std::filesystem::current_path() / "CabbageEditor" / "Backend";
    return Normalize(p.string());
}

inline std::string SitePackagesDir() {
    return Normalize(std::string(CORONA_PYTHON_MODULE_LIB_DIR) + "/site-packages");
}
}  // namespace PathCfg

const std::string PythonAPI::codePath = PathCfg::EngineRoot();

PythonAPI::PythonAPI() {
}

PythonAPI::~PythonAPI() {
    if (Py_IsInitialized()) {
        {
            nanobind::gil_scoped_acquire guard;
            pModule.reset();
            pFunc.reset();
            messageFunc.reset();
        }
        Py_FinalizeEx();
    }
    PyConfig_Clear(&config);
}

int64_t PythonAPI::nowMsec() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool PythonAPI::ensureInitialized() {
    if (Py_IsInitialized()) {
        return true;
    }

    // 注册 nanobind 导出的 CoronaEngine 模块
    PyImport_AppendInittab("CoronaEngine", &PyInit_CoronaEngine);

    PyConfig_InitPythonConfig(&config);
    PyConfig_SetBytesString(&config, &config.home, CORONA_PYTHON_HOME_DIR);
    PyConfig_SetBytesString(&config, &config.pythonpath_env, CORONA_PYTHON_HOME_DIR);
    config.module_search_paths_set = 1;

    {
        std::string runtimePath = PathCfg::RuntimeBackendAbs();
        PyWideStringList_Append(&config.module_search_paths, str2wstr(runtimePath).c_str());
        PyWideStringList_Append(&config.module_search_paths, str2wstr(CORONA_PYTHON_MODULE_DLL_DIR).c_str());
        PyWideStringList_Append(&config.module_search_paths, str2wstr(CORONA_PYTHON_MODULE_LIB_DIR).c_str());
        PyWideStringList_Append(&config.module_search_paths, str2wstr(PathCfg::SitePackagesDir()).c_str());
    }

    Py_InitializeFromConfig(&config);

    if (!Py_IsInitialized()) {
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
        return false;
    }

    {
        nanobind::gil_scoped_acquire gil;
        try {
            nanobind::module_ main_mod = nanobind::module_::import_("main");
            nanobind::object run_attr = nanobind::getattr(main_mod, "run");
            nanobind::object putq_attr = nanobind::getattr(main_mod, "put_queue");

            if (!nanobind::callable::check_(run_attr)) {
                std::cerr << "[Hotfix][API] 'run' attribute is not callable" << std::endl;
                return false;
            }

            pModule = std::move(main_mod);
            pFunc = std::move(run_attr);
            messageFunc = std::move(putq_attr);
        } catch (const std::exception& e) {
            // Print both C++ exception message (useful for nanobind errors) and Python traceback if set.
            std::cerr << "[Hotfix][API] exception while importing 'main': " << e.what()
                      << " (type: " << typeid(e).name() << ")" << std::endl;
            PrintPythonDiagnostics("import main failure");
            if (PyErr_Occurred()) {
                PyErr_Print();
            }
            pModule.reset();
            pFunc.reset();
            messageFunc.reset();
            return false;
        } catch (...) {
            std::cerr << "[Hotfix][API] unknown exception while importing 'main'" << std::endl;
            PrintPythonDiagnostics("import main unknown failure");
            if (PyErr_Occurred()) {
                PyErr_Print();
            }
            pModule.reset();
            pFunc.reset();
            messageFunc.reset();
            return false;
        }
    }
    return true;
}

bool PythonAPI::performHotReload() {
    int64_t currentTime = PythonHotfix::GetCurrentTimeMsec();  // ms
    constexpr int64_t kHotReloadIntervalMs = 100;              // 100ms
    if (currentTime - lastHotReloadTime <= kHotReloadIntervalMs || hotfixManger.packageSet.empty()) {
        return false;
    }

    std::cout << "[Hotfix] performHotReload triggered. packageSet.size=" << hotfixManger.packageSet.size() << std::endl;

    bool reloadedDeps = hotfixManger.ReloadPythonFile();
    if (!reloadedDeps) {
        std::cout << "[Hotfix] hotfixManger.ReloadPythonFile returned false" << std::endl;
        return false;
    }

    nanobind::gil_scoped_acquire gil;
    std::cout << "[Hotfix] reloading 'main' module (via importlib.reload)" << std::endl;

    try {
        nanobind::module_ importlib = nanobind::module_::import_("importlib");
        nanobind::object reload_func = nanobind::getattr(importlib, "reload");

        nanobind::module_ mod = nanobind::module_::import_("main");
        (void)reload_func(mod);

        nanobind::object newFunc = nanobind::getattr(mod, "run");
        if (!nanobind::callable::check_(newFunc)) {
            std::cout << "[Hotfix][API] new run attr invalid" << std::endl;
            return false;
        }
        nanobind::object newMsg = nanobind::getattr(mod, "put_queue");

        pModule = std::move(mod);
        pFunc = std::move(newFunc);
        messageFunc = std::move(newMsg);
    } catch (const std::exception& e) {
        std::cout << "[Hotfix][API] reload(main) failed: " << e.what()
                  << " (type: " << typeid(e).name() << ")" << std::endl;
        PrintPythonDiagnostics("reload main failure");
        if (PyErr_Occurred()) PyErr_Print();
        return false;
    } catch (...) {
        std::cout << "[Hotfix][API] reload(main) failed with unknown exception" << std::endl;
        PrintPythonDiagnostics("reload main unknown failure");
        if (PyErr_Occurred()) PyErr_Print();
        return false;
    }

    lastHotReloadTime = currentTime;
    hasHotReload = true;
    std::cout << "[Hotfix] performHotReload finished successfully" << std::endl;
    return true;
}

void PythonAPI::invokeEntry(bool isReload) const {
    if (!pFunc.is_valid()) {
        return;
    }
    nanobind::gil_scoped_acquire gil;

    try {
        (void)pFunc(isReload ? 1 : 0);
    } catch (const std::exception& e) {
        std::cerr << "[Hotfix][API] exception while invoking entry: " << e.what()
                  << " (type: " << typeid(e).name() << ")" << std::endl;
        PrintPythonDiagnostics("invoke entry failure");
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
    } catch (...) {
        std::cerr << "[Hotfix][API] unknown exception while invoking entry" << std::endl;
        PrintPythonDiagnostics("invoke entry unknown failure");
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
    }
}

void PythonAPI::sendMessage(const std::string& message) const {
    if (!messageFunc.is_valid()) {
        return;
    }
    nanobind::gil_scoped_acquire gil;

    try {
        (void)messageFunc(message.c_str());
    } catch (const std::exception& e) {
        std::cerr << "[Hotfix][API] exception while sending message: " << e.what()
                  << " (type: " << typeid(e).name() << ")" << std::endl;
        PrintPythonDiagnostics("send message failure");
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
    } catch (...) {
        std::cerr << "[Hotfix][API] unknown exception while sending message" << std::endl;
        PrintPythonDiagnostics("send message unknown failure");
        if (PyErr_Occurred()) {
            PyErr_Print();
        }
    }
}

void PythonAPI::runPythonScript() {
    if (!ensureInitialized()) {
        std::cerr << "Python init failed." << std::endl;
        return;
    }

    bool reloaded = false;
    {
        std::unique_lock lk(queMtx);
        reloaded = performHotReload();
        if (!reloaded && !hotfixManger.packageSet.empty()) {
            hasHotReload = false;
        }
    }

    invokeEntry(reloaded);
}

void PythonAPI::checkPythonScriptChange() {
    const std::string& sourcePath = PathCfg::EditorBackendAbs();
    const std::string runtimePath = PathCfg::RuntimeBackendAbs();
    int64_t checkTime = PythonHotfix::GetCurrentTimeMsec();
    std::cout << "[Hotfix] checkPythonScriptChange: src=" << sourcePath
              << ", dst=" << runtimePath
              << ", t=" << checkTime << std::endl;
    copyModifiedFiles(sourcePath, runtimePath, checkTime);
}

void PythonAPI::checkReleaseScriptChange() {
    static int64_t lastCheckTime = 0;
    static std::unordered_map<std::string, int64_t> lastProcessedMtime;  // mod -> last mtime processed

    int64_t currentTime = PythonHotfix::GetCurrentTimeMsec();
    if (currentTime - lastCheckTime < 100) {
        return;
    }
    lastCheckTime = currentTime;

    std::queue<std::unordered_set<std::string>> messageQue;
    const std::string runtimePathStr = PathCfg::RuntimeBackendAbs();
    const std::filesystem::path runtimePath(runtimePathStr);
    PythonHotfix::TraverseDirectory(runtimePathStr, messageQue, currentTime);

    if (messageQue.empty()) {
        return;
    }

    std::unique_lock lk(queMtx);
    const auto& mods = messageQue.front();
    std::cout << "[Hotfix] detected modified modules (" << mods.size() << ") from runtime scan: ";
    bool first = true;
    for (const auto& m : mods) {
        if (!first) std::cout << ", ";
        std::cout << m;
        first = false;
    }
    std::cout << std::endl;

    auto modToPath = [&](const std::string& mod) {
        std::string rel = mod;  // replace '.' with '/'
        std::ranges::replace(rel, '.', '/');
        return runtimePath / (rel + ".py");
    };

    for (const auto& mod : mods) {
        int64_t fileMtimeMs = 0;
        std::error_code ec;
        const auto filePath = modToPath(mod);
        auto ftime = std::filesystem::last_write_time(filePath, ec);
        if (!ec) {
            auto sysTime = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
            fileMtimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(sysTime.time_since_epoch()).count();
        }

        auto it = lastProcessedMtime.find(mod);
        if (it != lastProcessedMtime.end() && fileMtimeMs > 0 && fileMtimeMs <= it->second) {
            std::cout << "[Hotfix] skip duplicate module in window: '" << mod << "' mtime=" << fileMtimeMs
                      << " lastProcessed=" << it->second << std::endl;
            continue;
        }

        if (!hotfixManger.packageSet.contains(mod)) {
            hotfixManger.packageSet.emplace(mod, currentTime);
            std::cout << "[Hotfix] packageSet.emplace: '" << mod << "' @" << currentTime << std::endl;
        } else {
            std::cout << "[Hotfix] packageSet already contains: '" << mod << "' (skip)" << std::endl;
        }
        if (fileMtimeMs > 0) {
            lastProcessedMtime[mod] = fileMtimeMs;
        }
    }
}

std::wstring PythonAPI::str2wstr(const std::string& str) {
    if (str.empty()) {
        return {};
    }
    int wlen = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), nullptr, 0);
    if (wlen <= 0) {
        return {};
    }
    std::wstring w(static_cast<size_t>(wlen), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), static_cast<int>(str.size()), w.data(), wlen);
    return w;
}

void PythonAPI::copyModifiedFiles(const std::filesystem::path& sourceDir,
                                  const std::filesystem::path& destDir,
                                  int64_t checkTimeMs) {
    static const std::set<std::string> skip = {
        "__pycache__", "__init__.py", ".pyc", "StaticComponents.py"};
    static std::unordered_map<std::string, int64_t> lastCopiedMtime;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(sourceDir)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto& filePath = entry.path();
        std::string fileName = filePath.filename().string();
        if (!PythonHotfix::EndsWith(fileName, ".py")) {
            continue;
        }
        bool skipFile = std::ranges::any_of(skip, [&](const std::string& s) {
            return PythonHotfix::EndsWith(fileName, s);
        });
        if (skipFile) {
            continue;
        }

        try {
            auto ftime = std::filesystem::last_write_time(filePath);
            auto sysTime = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
            int64_t modifyMs = std::chrono::duration_cast<std::chrono::milliseconds>(sysTime.time_since_epoch()).count();

            auto srcKey = filePath.string();
            auto it = lastCopiedMtime.find(srcKey);
            bool newerThanLastCopy = (it == lastCopiedMtime.end()) || (modifyMs > it->second);
            if (checkTimeMs - modifyMs <= PythonHotfix::kFileRecentWindowMs && newerThanLastCopy) {
                auto relativePath = std::filesystem::relative(filePath, sourceDir);
                auto destFilePath = destDir / relativePath;
                std::filesystem::create_directories(destFilePath.parent_path());
                std::filesystem::copy_file(filePath, destFilePath,
                                           std::filesystem::copy_options::overwrite_existing);
                std::filesystem::last_write_time(destFilePath, ftime);

                lastCopiedMtime[srcKey] = modifyMs;

                std::string modName = destFilePath.string();
                PythonHotfix::NormalizeModuleName(modName);
                std::cout << "[Hotfix] copied recent file: " << filePath.string()
                          << " -> " << destFilePath.string()
                          << ", module='" << modName << "' src_mtime=" << modifyMs << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "File copy error: " << e.what() << std::endl;
        }
    }
}
