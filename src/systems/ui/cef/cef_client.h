#pragma once
#include <cef_app.h>

#include "cef_handler.h"

namespace Corona::Systems::UI {
struct BrowserTab;
class OffscreenRenderHandler;
class BrowserSideJSHandler;
}

namespace Corona::Systems::UI {

// 离屏渲染的 CefClient
class OffscreenCefClient : public CefClient,
                           public CefLifeSpanHandler,
                           public CefLoadHandler,
                           public CefRequestHandler,
                           public CefRenderHandler,
                           public CefDisplayHandler {
   public:
    OffscreenCefClient();
    void SetTab(BrowserTab* tab);

    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
    CefRefPtr<CefRenderHandler> GetRenderHandler() override; // Implementation moved to .cpp or fixed
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
    CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }

    CefRefPtr<CefBrowser> GetBrowser() { return browser_; }
    void Resize(int width, int height);

    bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                        CefRefPtr<CefFrame> frame,
                        CefRefPtr<CefRequest> request,
                        bool user_gesture,
                        bool is_redirect) override;

    void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
    void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type,
                 const RectList& dirtyRects, const void* buffer,
                 int width, int height) override;
    bool OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                          cef_log_severity_t level,
                          const CefString& message,
                          const CefString& source,
                          int line) override;

    void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode) override;

    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
    void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

    void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                   TerminationStatus status,
                                   int error_code,
                                   const CefString& error_string) override {
        CEF_REQUIRE_UI_THREAD();
        // Check browser_side_router_ before using it
        if (browser_side_router_)
            browser_side_router_->OnRenderProcessTerminated(browser);
    }

    virtual bool OnProcessMessageReceived(
        CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefProcessId source_process,
        CefRefPtr<CefProcessMessage> message) override {
        CEF_REQUIRE_UI_THREAD();
        if (message->GetName() == "RendererMessage") {
            // Render 进程已启动并发送了消息
            std::string msg = message->GetArgumentList()->GetString(0);
            std::cout << "收到 Render 进程消息: " << msg << std::endl;
            return true;
        }
        // 处理Renderer进程发来的消息
        if (browser_side_router_)
            return browser_side_router_->OnProcessMessageReceived(browser, frame, source_process, message);
        return false;
    }

   private:
    CefRefPtr<CefBrowser> browser_;
    CefRefPtr<OffscreenRenderHandler> render_handler_;
    CefRefPtr<CefMessageRouterBrowserSide> browser_side_router_;
    BrowserSideJSHandler* js_handler_;

    IMPLEMENT_REFCOUNTING(OffscreenCefClient);
};

class SimpleApp : public CefApp, public CefRenderProcessHandler {
   public:
    SimpleApp();

    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
        return render_process_handler_;
    }

    virtual void OnBeforeCommandLineProcessing(const CefString& process_type,
                                               CefRefPtr<CefCommandLine> command_line) override;

   private:
    CefRefPtr<CefRenderProcessHandler> render_process_handler_;
    CefRefPtr<CefMessageRouterRendererSide> renderer_side_router_;

    IMPLEMENT_REFCOUNTING(SimpleApp);
};

} // namespace Corona::Systems::UI

