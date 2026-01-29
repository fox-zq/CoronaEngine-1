#pragma once
#include <Python.h>
//#include <nanobind/nanobind.h>
//namespace nb = nanobind;

#include <string>
#include <vector>
#include <iostream>
#include <filesystem>
#include <algorithm>

// SDL & OpenGL
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <volk.h>

// CEF headers
#include <cef_app.h>
#include <cef_client.h>
#include <cef_browser.h>
#include <cef_render_handler.h>
#include <cef_life_span_handler.h>
#include <cef_load_handler.h>
#include <cef_request_handler.h>
#include <cef_display_handler.h>
#include <wrapper/cef_helpers.h>

#include <wrapper/cef_message_router.h>

class OffscreenCefClient;

extern CefMessageRouterConfig g_messageRouterConfig;

// 浏览器窗口数据结构
struct BrowserTab {
    std::string name;
    std::string url;
    CefRefPtr<OffscreenCefClient> client;
    VkDescriptorSet textureId = VK_NULL_HANDLE;  // Vulkan 纹理描述符集
    int width = 800;
    int height = 600;
    bool open = true;
    bool needsResize = false;
    char urlBuffer[1024] = "";
    std::vector<uint8_t> pixelBuffer;
    bool bufferDirty = false;
};


// Expose Vulkan backend pointer so browser texture helpers can access device/queue
namespace Corona::Systems { class VulkanBackend; extern VulkanBackend* g_vulkan_backend; }

// 离屏渲染的 CefRenderHandler
class OffscreenRenderHandler : public CefRenderHandler {
public:
    BrowserTab* tab = nullptr;

    void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
    void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type,
                    const RectList& dirtyRects, const void* buffer,
                    int width, int height) override;

    IMPLEMENT_REFCOUNTING(OffscreenRenderHandler);
};



class BrowserSideJSHandler: public CefMessageRouterBrowserSide::Handler, public CefBaseRefCounted {
public:
    BrowserSideJSHandler() {
      
    
    };

    // 处理来自JS的请求
    virtual bool OnQuery(CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         int64_t query_id,
                         const CefString& request,
                         bool persistent,
                         CefRefPtr<Callback> callback) override;
    

    virtual void OnQueryCanceled(CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        int64_t query_id) override {
        CEF_REQUIRE_UI_THREAD();
        std::cout << "[Browser] Query canceled: " << query_id << std::endl;
    }

private:
    IMPLEMENT_REFCOUNTING(BrowserSideJSHandler);
    DISALLOW_COPY_AND_ASSIGN(BrowserSideJSHandler);


    PyObject* pFunc;

};

// 离屏渲染的 CefClient
class OffscreenCefClient : public CefClient,
    public CefLifeSpanHandler,
    public CefLoadHandler,
    public CefRequestHandler,
    public CefRenderHandler,
    public CefDisplayHandler{
public:
    OffscreenCefClient();
    void SetTab(BrowserTab* tab);

    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
    CefRefPtr<CefRenderHandler> GetRenderHandler() override { return renderHandler_; }
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
        const CefString& error_string) override{

        CEF_REQUIRE_UI_THREAD();
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
        return browser_side_router_->OnProcessMessageReceived(browser, frame, source_process, message);
    }


private:
    CefRefPtr<CefBrowser> browser_;
    CefRefPtr<OffscreenRenderHandler> renderHandler_;
	CefRefPtr<CefMessageRouterBrowserSide> browser_side_router_;
    BrowserSideJSHandler* m_jsHandler;


    IMPLEMENT_REFCOUNTING(OffscreenCefClient);
};


// 创建新的Renderer进程处理器
class SimpleRenderProcessHandler : public CefRenderProcessHandler {
public:
    SimpleRenderProcessHandler() : renderer_side_router_(nullptr) {}

    void OnContextCreated(CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefRefPtr<CefV8Context> context) override {

        CEF_REQUIRE_RENDERER_THREAD();


        if (!renderer_side_router_) {

            renderer_side_router_ = CefMessageRouterRendererSide::Create(g_messageRouterConfig);
        }

        // 这会将cefQuery和cefQueryCancel函数注入到window对象中
        renderer_side_router_->OnContextCreated(browser, frame, context);

        std::cout << "[Renderer] V8 context created, cefQuery injected" << std::endl;
    }

    void OnContextReleased(CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefRefPtr<CefV8Context> context) override {

        CEF_REQUIRE_RENDERER_THREAD();
        if (renderer_side_router_) {
            renderer_side_router_->OnContextReleased(browser, frame, context);
        }
    }

    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
        CefRefPtr<CefFrame> frame,
        CefProcessId source_process,
        CefRefPtr<CefProcessMessage> message) override {

        std::cout << "get Render msg: " << message << std::endl;

        CEF_REQUIRE_RENDERER_THREAD();
        if (renderer_side_router_) {
            return renderer_side_router_->OnProcessMessageReceived(
                browser, frame, source_process, message);
        }
        return false;
    }

private:
    CefRefPtr<CefMessageRouterRendererSide> renderer_side_router_;
    IMPLEMENT_REFCOUNTING(SimpleRenderProcessHandler);
};

// 全局变量声明
extern std::unordered_map<int, BrowserTab*> g_tabs;
extern int g_tabCounter;
// 函数声明
VkDescriptorSet CreateBrowserTexture(int width, int height);
std::string ConvertLocalPathToUrl(const std::string& localPath);
std::string ResolveHtmlPathForCef(const std::string& maybeRelativePath);
extern "C" int CreateBrowserTab(const std::string& url);
void UpdateBrowserTexture(int tabId);
void CloseBrowserTab(int tabId);

// 自定义 App 类
class SimpleApp : public CefApp, public CefRenderProcessHandler {
public:

    SimpleApp();

    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
        return renderProcessHandler_;
    }
    //virtual CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    //    return this;
    //}


    virtual void OnBeforeCommandLineProcessing(const CefString& process_type,
        CefRefPtr<CefCommandLine> command_line) override;


    //// CefBrowserProcessHandler 方法
    //virtual void OnContextInitialized() override {
    //    CEF_REQUIRE_UI_THREAD();

    //    // 创建浏览器窗口
    //    //CreateBrowserTab(ResolveHtmlPathForCef("/test.html"));
    //}

private:
    CefRefPtr<CefRenderProcessHandler> renderProcessHandler_;
    CefRefPtr<CefMessageRouterRendererSide> renderer_side_router_;

    IMPLEMENT_REFCOUNTING(SimpleApp);
};

