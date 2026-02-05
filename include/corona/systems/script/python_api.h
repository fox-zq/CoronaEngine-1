#pragma once

#include <Python.h>
#include <corona/systems/script/python_hotfix.h>
#include <nanobind/nanobind.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <shared_mutex>
#include <string>

namespace Corona::Script::Python {

struct PythonAPI {
    PythonAPI();

    ~PythonAPI();

    void runPythonScript();
    static void checkPythonScriptChange();
    void checkReleaseScriptChange();
    void sendMessage(const std::string& message) const;

    
    nanobind::object pStartFunc;   // callable 'start'
    nanobind::object pJsCallFunc;  // callable 'js_call'

   private:
    static const std::string codePath;

    PythonHotfix hotfixManger;
    mutable std::shared_mutex queMtx;

    int64_t lastHotReloadTime = 0;  // ms
    bool hasHotReload = false;

    nanobind::object pModule;      // module 'main'
    nanobind::object pFunc;        // callable 'run'
    nanobind::object messageFunc;  // callable 'put_queue'

    std::vector<std::string> moduleList;
    std::vector<std::string> callableList;

    PyConfig config{};  // 值初始化

    bool ensureInitialized();
    bool performHotReload();
    void invokeEntry(bool isReload) const;
    static int64_t nowMsec();
    static std::wstring str2wstr(const std::string& str);
    static std::string wstr2str(const std::wstring& wstr);
    static void copyModifiedFiles(const std::filesystem::path& sourceDir,
                                  const std::filesystem::path& destDir,
                                  int64_t checkTimeMs);
};
}  // namespace Corona::Script::Python
