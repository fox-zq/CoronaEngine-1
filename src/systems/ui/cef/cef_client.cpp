#include "cef_client.h"

#include <iostream>

#include "browser_types.h"
#include "cef_handler.h"

namespace Corona::Systems::UI {

// ----------------------------------------------------------------------------
// OffscreenCefClient Implementation
// ----------------------------------------------------------------------------

OffscreenCefClient::OffscreenCefClient() {
    render_handler_ = new OffscreenRenderHandler();
    // Create the browser-side router for query handling.
    CefMessageRouterConfig config;
    browser_side_router_ = CefMessageRouterBrowserSide::Create(config);

    // Register handlers (optional)
    js_handler_ = new BrowserSideJSHandler();
    browser_side_router_->AddHandler(js_handler_, false);
}

CefRefPtr<CefRenderHandler> OffscreenCefClient::GetRenderHandler() {
    return render_handler_;
}

void OffscreenCefClient::SetTab(BrowserTab* tab) {
    if (render_handler_) {
        render_handler_->tab = tab;
    }
}

void OffscreenCefClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();

    // 浏览器创建完成
    if (!browser_) {
        browser_ = browser;

        // Create config if not available globally or use valid one
        CefMessageRouterConfig config;
        config.js_query_function = "cefQuery";
        config.js_cancel_function = "cefQueryCancel";

        browser_side_router_ = CefMessageRouterBrowserSide::Create(config);

        // 注册自定义的 JS 处理器
        js_handler_ = new BrowserSideJSHandler();
        if (browser_side_router_.get()) {
            browser_side_router_->AddHandler(js_handler_, true);
        }
    }
}

void OffscreenCefClient::OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode) {
    CEF_REQUIRE_UI_THREAD();

    if (frame->IsMain()) {
        // Main frame load end
    }
}

void OffscreenCefClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    browser_ = nullptr;
    if (browser_side_router_)
        browser_side_router_->OnBeforeClose(browser);
}

void OffscreenCefClient::Resize(int width, int height) {
    if (browser_) {
        browser_->GetHost()->WasResized();
    }
}

bool OffscreenCefClient::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                                        CefRefPtr<CefFrame> frame,
                                        CefRefPtr<CefRequest> request,
                                        bool user_gesture,
                                        bool is_redirect) {
    CEF_REQUIRE_UI_THREAD();
    if (browser_side_router_)
        browser_side_router_->OnBeforeBrowse(browser, frame);
    return false;
}

void OffscreenCefClient::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) {
    if (render_handler_)
        render_handler_->GetViewRect(browser, rect);
}

void OffscreenCefClient::OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type,
                                 const RectList& dirtyRects, const void* buffer,
                                 int width, int height) {
    if (render_handler_)
        render_handler_->OnPaint(browser, type, dirtyRects, buffer, width, height);
}

bool OffscreenCefClient::OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                                          cef_log_severity_t level,
                                          const CefString& message,
                                          const CefString& source,
                                          int line) {
    // 映射日志级别
    const char* levelStr = "";
    switch (level) {
        case LOGSEVERITY_DEBUG:
            levelStr = "DEBUG";
            break;
        case LOGSEVERITY_INFO:
            levelStr = "INFO";
            break;
        case LOGSEVERITY_WARNING:
            levelStr = "WARNING";
            break;
        case LOGSEVERITY_ERROR:
            levelStr = "ERROR";
            break;
        default:
            levelStr = "LOG";
            break;
    }

    // 输出到控制台
    std::cout << "[Browser Console][" << levelStr << "] ";

    // 如果有源文件和行号，显示它们
    if (!source.empty()) {
        std::cout << source.ToString() << ":" << line << " - ";
    }

    std::cout << message.ToString() << std::endl;

    return true;  // 消息已处理
}

// ----------------------------------------------------------------------------
// SimpleApp Implementation
// ----------------------------------------------------------------------------

SimpleApp::SimpleApp() {
    std::cout << "SimpleApp constructor called" << std::endl;
    render_process_handler_ = new SimpleRenderProcessHandler();

    CefMessageRouterConfig config;
    config.js_query_function = "cefQuery";
    config.js_cancel_function = "cefQueryCancel";
    renderer_side_router_ = CefMessageRouterRendererSide::Create(config);
}

void SimpleApp::OnBeforeCommandLineProcessing(const CefString& process_type,
                                              CefRefPtr<CefCommandLine> command_line) {
    // 禁用同源策略，允许加载本地文件
    command_line->AppendSwitch("disable-web-security");
    // 允许访问文件
    command_line->AppendSwitch("allow-file-access-from-files");
    // 允许访问本地资源
    command_line->AppendSwitch("allow-file-access");
    // 禁用沙箱（仅用于开发）
    command_line->AppendSwitch("no-sandbox");
    // Disable GPU to avoid GPU subprocess crashes on some systems/configs
    // (CEF will fall back to software rendering)
    command_line->AppendSwitch("disable-gpu");
    command_line->AppendSwitch("disable-gpu-compositing");
    // command_line->AppendSwitch("disable-surfaces");
    //  启用插件
    command_line->AppendSwitch("enable-plugins");
    // 启用网络服务
    command_line->AppendSwitch("enable-net-benchmarking");
    // 禁用PDF查看器
    command_line->AppendSwitch("disable-pdf-extension");
    // 禁用PDF查看器
    command_line->AppendSwitch("disable-pdf-viewer");
    // 禁用组件更新
    command_line->AppendSwitch("disable-component-update");
    // 禁用后台网络
    command_line->AppendSwitch("disable-background-networking");
    // 禁用D3D11
    command_line->AppendSwitch("disable-d3d11");
    // 禁用加速视频解码
    command_line->AppendSwitch("disable-accelerated-video-decode");
}

} // namespace Corona::Systems::UI
