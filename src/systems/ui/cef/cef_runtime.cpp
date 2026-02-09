#include "cef_runtime.h"

#include <corona/kernel/core/i_logger.h>
#include <filesystem>
#include <windows.h>

#include "cef_client.h"

namespace Corona::Systems::UI {

CefMessageRouterConfig message_router_config;

bool initialize_cef() {
    message_router_config.js_query_function = "cefQuery";
    message_router_config.js_cancel_function = "cefQueryCancel";

    // CEF 使用单独的 cef_subprocess.exe 作为子进程可执行文件
    // 这样可以避免主引擎的静态初始化代码（如 Vulkan 设备创建）在子进程中执行

    CefMainArgs main_args(GetModuleHandle(nullptr));
    CefRefPtr<SimpleApp> app(new SimpleApp());

    CefSettings settings;
    settings.multi_threaded_message_loop = true;
    settings.windowless_rendering_enabled = true;
    settings.no_sandbox = true;
    settings.remote_debugging_port = 9222;
    settings.log_severity = LOGSEVERITY_INFO;
    settings.uncaught_exception_stack_size = 10;

    CefString(&settings.locale).FromASCII("zh-CN");

    std::filesystem::path cache_path = std::filesystem::current_path() / "cache";
    if (!std::filesystem::exists(cache_path)) {
        std::filesystem::create_directories(cache_path);
    }
    CefString(&settings.cache_path).FromString(cache_path.string());

    // 使用单独的子进程可执行文件，避免主引擎的静态初始化代码在子进程中执行
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    std::filesystem::path exe_dir = std::filesystem::path(exe_path).parent_path();
    std::filesystem::path subprocess_path = exe_dir / "cef_subprocess.exe";

    if (std::filesystem::exists(subprocess_path)) {
        CefString(&settings.browser_subprocess_path).FromWString(subprocess_path.wstring());
        CFW_LOG_INFO("CEF: Using separate subprocess: {}", subprocess_path.string());
    } else {
        // 回退到使用主可执行文件（可能会有静态初始化问题）
        CefString(&settings.browser_subprocess_path).FromWString(exe_path);
        CFW_LOG_WARNING("CEF: cef_subprocess.exe not found, using main executable as subprocess");
    }

    CefString(&settings.user_agent).FromASCII(
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    settings.background_color = CefColorSetARGB(255, 255, 255, 255);
    settings.persist_session_cookies = true;

    CefRefPtr<CefCommandLine> command_line = CefCommandLine::CreateCommandLine();
    command_line->InitFromString(::GetCommandLineW());

    if (!CefInitialize(main_args, settings, app.get(), nullptr)) {
        CFW_LOG_ERROR("Failed to initialize CEF.");
        return false;
    }

    return true;
}

void shutdown_cef() {
    CFW_LOG_INFO("CEF: Starting shutdown...");

    // 在多线程消息循环模式下，CefShutdown 会等待所有浏览器关闭
    // 如果有浏览器没有正确关闭，这里会阻塞
    CefShutdown();

    CFW_LOG_INFO("CEF: Shutdown complete");
}

}  // namespace Corona::Systems::UI
