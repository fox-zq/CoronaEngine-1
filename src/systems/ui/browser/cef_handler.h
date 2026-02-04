#pragma once
// CEF headers
#include <Python.h>
#include <cef_app.h>
#include <cef_browser.h>
#include <cef_client.h>
#include <cef_display_handler.h>
#include <cef_life_span_handler.h>
#include <cef_load_handler.h>
#include <cef_render_handler.h>
#include <cef_request_handler.h>
#include <wrapper/cef_helpers.h>
#include <wrapper/cef_message_router.h>

#include <iostream>
#include <mutex>

#include "browser_types.h"

extern CefMessageRouterConfig message_router_config;
// 离屏渲染的 CefRenderHandler
class OffscreenRenderHandler : public CefRenderHandler {
   public:
    BrowserTab* tab = nullptr;

    void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
    void OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type,
                 const RectList& dirty_rects, const void* buffer,
                 int width, int height) override;

    IMPLEMENT_REFCOUNTING(OffscreenRenderHandler);
};

class BrowserSideJSHandler : public CefMessageRouterBrowserSide::Handler, public CefBaseRefCounted {
   public:
    BrowserSideJSHandler() {
        initialize_python();
    };
    ~BrowserSideJSHandler() {
        PyGILState_STATE gstate = PyGILState_Ensure();
        Py_XDECREF(pFunc);
        PyGILState_Release(gstate);
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
    PyObject* pFunc;
    void initialize_python();

    IMPLEMENT_REFCOUNTING(BrowserSideJSHandler);
    DISALLOW_COPY_AND_ASSIGN(BrowserSideJSHandler);
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
            renderer_side_router_ = CefMessageRouterRendererSide::Create(message_router_config);
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
