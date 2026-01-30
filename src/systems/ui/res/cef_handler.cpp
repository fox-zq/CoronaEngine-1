#include "cef_handler.h"
#include <iostream>

void BrowserSideJSHandler::initialize_python() {
    if (!Py_IsInitialized()) {
        Py_Initialize();
        PyEval_InitThreads();
        PyEval_SaveThread();
    }

    PyGILState_STATE gstate = PyGILState_Ensure();

    // 每个处理器独立加载模块
    PyRun_SimpleString("import sys");
    PyRun_SimpleString("import os");
    PyRun_SimpleString("sys.path.insert(0, os.getcwd())");

    PyObject* pName = PyUnicode_FromString("test");
    PyObject* pModule = PyImport_Import(pName);
    Py_DECREF(pName);

    if (pModule) {
        pFunc = PyObject_GetAttrString(pModule, "handle_request");
        Py_DECREF(pModule);
    }

    PyGILState_Release(gstate);
}


bool BrowserSideJSHandler::OnQuery(CefRefPtr<CefBrowser> browser,
                                   CefRefPtr<CefFrame> frame,
                                   int64_t query_id,
                                   const CefString& request,
                                   bool persistent,
                                   CefRefPtr<Callback> callback) {
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

            initialize_python();
            std::cout << "Module imported successfully!" << std::endl;
        }

        // 创建参数（原始 JSON 字符串）
        PyObject* pArgs = PyTuple_Pack(1, PyUnicode_FromString(req.c_str()));

        // 调用 Python 函数
        PyObject* pValue = PyObject_CallObject(pFunc, pArgs);
        Py_DECREF(pArgs);

        if (!pValue) {
            PyErr_Print();
            std::cerr << "Python function call failed" << std::endl;
            callback->Failure(0, "Python function call failed");
        } else {
            // 直接返回字符串结果
            if (PyUnicode_Check(pValue)) {
                const char* result = PyUnicode_AsUTF8(pValue);
                callback->Success(result);
            } else {
                // 尝试转换为字符串
                PyObject* strObj = PyObject_Str(pValue);
                if (strObj) {
                    const char* result = PyUnicode_AsUTF8(strObj);
                    callback->Success(result);
                    Py_DECREF(strObj);
                }
            }
            Py_DECREF(pValue);
        }

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

