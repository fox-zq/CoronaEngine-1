/**
 * @brief CEF 子进程可执行文件
 *
 * 这是一个轻量级的可执行文件，专门用于 CEF 子进程（Browser, Renderer, GPU 等）。
 * 使用单独的可执行文件可以避免主引擎的静态初始化代码在子进程中执行。
 */

#include <cef_app.h>
#include <wrapper/cef_helpers.h>
#include <wrapper/cef_message_router.h>

#include <iostream>

#ifdef _WIN32
#include <windows.h>
#endif

// 与主程序使用相同的消息路由配置
static CefMessageRouterConfig GetMessageRouterConfig() {
    CefMessageRouterConfig config;
    config.js_query_function = "cefQuery";
    config.js_cancel_function = "cefQueryCancel";
    return config;
}

// 渲染进程处理器 - 负责在渲染进程中注入 cefQuery 函数
class SubprocessRenderHandler : public CefRenderProcessHandler {
   public:
    SubprocessRenderHandler() : renderer_side_router_(nullptr) {}

    void OnContextCreated(CefRefPtr<CefBrowser> browser,
                          CefRefPtr<CefFrame> frame,
                          CefRefPtr<CefV8Context> context) override {
        if (!renderer_side_router_) {
            renderer_side_router_ = CefMessageRouterRendererSide::Create(GetMessageRouterConfig());
        }

        // 将 cefQuery 和 cefQueryCancel 函数注入到 window 对象中
        renderer_side_router_->OnContextCreated(browser, frame, context);

        std::cout << "[Renderer] V8 context created, cefQuery injected" << std::endl;
    }

    void OnContextReleased(CefRefPtr<CefBrowser> browser,
                           CefRefPtr<CefFrame> frame,
                           CefRefPtr<CefV8Context> context) override {
        if (renderer_side_router_) {
            renderer_side_router_->OnContextReleased(browser, frame, context);
        }
    }

    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  CefProcessId source_process,
                                  CefRefPtr<CefProcessMessage> message) override {
        if (renderer_side_router_) {
            return renderer_side_router_->OnProcessMessageReceived(
                browser, frame, source_process, message);
        }
        return false;
    }

   private:
    CefRefPtr<CefMessageRouterRendererSide> renderer_side_router_;
    IMPLEMENT_REFCOUNTING(SubprocessRenderHandler);
};

// CefApp 实现，用于子进程
class SubprocessApp : public CefApp {
   public:
    SubprocessApp() : render_handler_(new SubprocessRenderHandler()) {}

    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
        return render_handler_;
    }

   private:
    CefRefPtr<SubprocessRenderHandler> render_handler_;
    IMPLEMENT_REFCOUNTING(SubprocessApp);
};

#ifdef _WIN32
int APIENTRY wWinMain(HINSTANCE hInstance,
                      HINSTANCE hPrevInstance,
                      LPWSTR lpCmdLine,
                      int nCmdShow) {
    CefMainArgs main_args(hInstance);
#else
int main(int argc, char* argv[]) {
    CefMainArgs main_args(argc, argv);
#endif

    CefRefPtr<SubprocessApp> app(new SubprocessApp());

    // 执行 CEF 子进程逻辑
    // 这将阻塞直到子进程结束
    return CefExecuteProcess(main_args, app.get(), nullptr);
}





