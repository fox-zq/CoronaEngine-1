#include <corona/systems/imgui/res/BrowserWindow.h>
// ----------------------------------------------------------------------------
// OffscreenRenderHandler Implementation
// ----------------------------------------------------------------------------


bool BrowserSideJSHandler::OnQuery(CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    int64_t query_id,
    const CefString& request,
    bool persistent,
    CefRefPtr<Callback> callback) 
{
    CEF_REQUIRE_UI_THREAD();
    std::string req = request.ToString();
    std::cout << "[Browser] Received query: " << req << std::endl;

    // 初始化 Python 环境
    if (!Py_IsInitialized()) {
        Py_Initialize();
        PyEval_InitThreads();  // initialize and acquire the GIL
        PyEval_SaveThread();   // release GIL
    }

    PyGILState_STATE gstate = PyGILState_Ensure();

    try {
        // 确保函数对象存在
        if (!pFunc) {
            std::cout << "=== Loading Python module ===" << std::endl;
            
            // 设置 Python 路径
            PyRun_SimpleString("import sys");
            PyRun_SimpleString("import os");
            PyRun_SimpleString("sys.path.insert(0, os.getcwd())");
            
            // 导入模块
            PyObject* pName = PyUnicode_FromString("test");
            PyObject* pModule = PyImport_Import(pName);
            Py_DECREF(pName);
            
            if (!pModule) {
                PyErr_Print();
                std::cerr << "Failed to import module 'test'" << std::endl;
                callback->Failure(0, "Failed to import Python module");
                PyGILState_Release(gstate);
                return false;
            }
            
            // 获取函数
            pFunc = PyObject_GetAttrString(pModule, "test");
            Py_DECREF(pModule);
            
            if (!pFunc || !PyCallable_Check(pFunc)) {
                std::cerr << "Function 'test' not found or not callable" << std::endl;
                callback->Failure(0, "Python function not found");
                Py_XDECREF(pFunc);
                pFunc = nullptr;
                PyGILState_Release(gstate);
                return false;
            }
            
            std::cout << "Module imported successfully!" << std::endl;
        }
        
        // 创建 Python 字符串参数
        PyObject* pyArg = PyUnicode_FromString(req.c_str());
        if (!pyArg) {
            std::cerr << "Failed to create Python string" << std::endl;
            callback->Failure(0, "Failed to create Python argument");
            PyGILState_Release(gstate);
            return false;
        }
        
        // 创建参数元组
        PyObject* args = PyTuple_Pack(1, pyArg);
        Py_DECREF(pyArg);  // PyTuple_Pack 会增加引用计数
        
        if (!args) {
            std::cerr << "Failed to create argument tuple" << std::endl;
            callback->Failure(0, "Failed to create argument tuple");
            PyGILState_Release(gstate);
            return false;
        }
        
        // 调用 Python 函数
        PyObject* pValue = PyObject_CallObject(pFunc, args);
        Py_DECREF(args);  // 释放参数元组
        
        // 检查调用结果
        if (!pValue) {
            PyErr_Print();
            std::cerr << "Python function call failed" << std::endl;
            callback->Failure(0, "Python function call failed");
            PyGILState_Release(gstate);
            return false;
        }
        
        // 转换返回值给 CEF
        if (PyUnicode_Check(pValue)) {
            // 如果是字符串
            const char* result = PyUnicode_AsUTF8(pValue);
            callback->Success(result);
        } else if (PyLong_Check(pValue)) {
            // 如果是整数
            long result = PyLong_AsLong(pValue);
            callback->Success(std::to_string(result));
        } else if (PyFloat_Check(pValue)) {
            // 如果是浮点数
            double result = PyFloat_AsDouble(pValue);
            callback->Success(std::to_string(result));
        } else if (PyBool_Check(pValue)) {
            // 如果是布尔值
            bool result = PyObject_IsTrue(pValue);
            callback->Success(result ? "true" : "false");
        } else {
            // 其他类型转换为字符串表示
            PyObject* strObj = PyObject_Str(pValue);
            if (strObj) {
                const char* result = PyUnicode_AsUTF8(strObj);
                callback->Success(result);
                Py_DECREF(strObj);
            } else {
                callback->Success("Unsupported return type");
            }
        }
        
        // 释放返回值
        Py_DECREF(pValue);
        
    } catch (const std::exception& e) {
        std::cerr << "Exception in OnQuery: " << e.what() << std::endl;
        callback->Failure(0, e.what());
        PyGILState_Release(gstate);
        return false;
    }
    
    PyGILState_Release(gstate);
    return true;
}


void OffscreenRenderHandler::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) {
    if (tab) {
        rect = CefRect(0, 0, tab->width, tab->height);
    } else {
        rect = CefRect(0, 0, 800, 600);
    }
}

void OffscreenRenderHandler::OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type,
                                     const RectList& dirtyRects, const void* buffer,
                                     int width, int height) {
    if (tab && type == PET_VIEW) {
        // 复制像素数据
        size_t bufferSize = width * height * 4;
        tab->pixelBuffer.resize(bufferSize);
        memcpy(tab->pixelBuffer.data(), buffer, bufferSize);
        tab->bufferDirty = true;
    }
}

// ----------------------------------------------------------------------------
// OffscreenCefClient Implementation
// ----------------------------------------------------------------------------

OffscreenCefClient::OffscreenCefClient() {
    renderHandler_ = new OffscreenRenderHandler();


}


void OffscreenCefClient::SetTab(BrowserTab* tab) {
    renderHandler_->tab = tab;
}

void OffscreenCefClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();


    if (!browser_) {
        browser_ = browser;

        CefRefPtr<CefProcessMessage> msg =
            CefProcessMessage::Create("Ping");
        browser->GetMainFrame()->SendProcessMessage(PID_RENDERER, msg);

        std::cout << "send Render msg: " << msg << std::endl;
        browser_side_router_ = CefMessageRouterBrowserSide::Create(g_messageRouterConfig);

        m_jsHandler = new BrowserSideJSHandler();
        if (browser_side_router_.get())
        {

            browser_side_router_->AddHandler(m_jsHandler, true);
        }

    }


}

void OffscreenCefClient::OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int httpStatusCode) {
    // 页面加载完成
    CEF_REQUIRE_UI_THREAD();


    if (frame->IsMain()) {
        // 或者直接注入代码

    }
}
void OffscreenCefClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
    CEF_REQUIRE_UI_THREAD();
    browser_ = nullptr;
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
	browser_side_router_->OnBeforeBrowse(browser, frame);
    return false;
}

void OffscreenCefClient::GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) {
    renderHandler_->GetViewRect(browser, rect);
}

void OffscreenCefClient::OnPaint(CefRefPtr<CefBrowser> browser, PaintElementType type,
    const RectList& dirtyRects, const void* buffer,
    int width, int height) {
    renderHandler_->OnPaint(browser, type, dirtyRects, buffer, width, height);
}

bool OffscreenCefClient::OnConsoleMessage(CefRefPtr<CefBrowser> browser,
    cef_log_severity_t level,
    const CefString& message,
    const CefString& source,
    int line) {
    // 映射日志级别
    const char* levelStr = "";
    switch (level) {
    case LOGSEVERITY_DEBUG: levelStr = "DEBUG"; break;
    case LOGSEVERITY_INFO: levelStr = "INFO"; break;
    case LOGSEVERITY_WARNING: levelStr = "WARNING"; break;
    case LOGSEVERITY_ERROR: levelStr = "ERROR"; break;
    default: levelStr = "LOG"; break;
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
    renderProcessHandler_ = new SimpleRenderProcessHandler();

    renderer_side_router_ = CefMessageRouterRendererSide::Create(g_messageRouterConfig);
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
    //command_line->AppendSwitch("disable-surfaces");
    // 启用插件
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
