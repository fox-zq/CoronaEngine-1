#include "cef_handler.h"

#include <iostream>

void BrowserSideJSHandler::initialize_python() {
    if (!Py_IsInitialized()) {
        Py_Initialize();
        PyEval_SaveThread();
    }

    PyGILState_STATE gstate = PyGILState_Ensure();
    PyObject* pModule = nullptr;  // 在开头声明，确保作用域

    try {
        // 设置 Python 路径
        PyRun_SimpleString("import sys");
        PyRun_SimpleString("import os");
        PyRun_SimpleString("sys.path.insert(0, os.path.join(os.getcwd(), 'CabbageEditor'))");  // 修正括号

        // 加载模块
        PyObject* pName = PyUnicode_FromString("main");
        if (!pName) {
            throw std::runtime_error("Failed to create module name");
        }

        pModule = PyImport_Import(pName);
        Py_DECREF(pName);

        if (!pModule) {
            // 获取错误信息
            PyErr_Print();
            PyGILState_Release(gstate);
            throw std::runtime_error("Failed to import Python module 'main'");
        }

        // 获取 CoronaEditor 类
        PyObject* pClass = PyObject_GetAttrString(pModule, "editor");
        if (!pClass) {
            Py_DECREF(pModule);
            PyErr_Print();
            PyGILState_Release(gstate);
            throw std::runtime_error("Failed to get 'editor' attribute from module");
        }

        if (PyCallable_Check(pClass)) {
            // 获取类方法引用
            pFunc = PyObject_GetAttrString(pClass, "deal_func_from_js");
        }

        Py_DECREF(pClass);
        Py_DECREF(pModule);

    } catch (const std::exception&) {
        // 清理资源
        if (pModule) {
            Py_DECREF(pModule);
        }
        PyErr_Print();
        PyGILState_Release(gstate);
        throw;
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

    if (!Py_IsInitialized()) {
        Py_Initialize();
        PyEval_SaveThread();  // release GIL
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
        PyObject* args = PyTuple_Pack(1, PyUnicode_FromString(req.c_str()));

        // 调用 Python 函数
        PyObject* object = PyObject_CallObject(pFunc, args);
        Py_DECREF(args);

        if (!object) {
            PyErr_Print();
            std::cerr << "Python function call failed" << std::endl;
            callback->Failure(0, "Python function call failed");
        } else {
            // 直接返回字符串结果
            if (PyUnicode_Check(object)) {
                const char* result = PyUnicode_AsUTF8(object);
                callback->Success(result);
            } else {
                // 尝试转换为字符串
                PyObject* str_obj = PyObject_Str(object);
                if (str_obj) {
                    const char* result = PyUnicode_AsUTF8(str_obj);
                    callback->Success(result);
                    Py_DECREF(str_obj);
                }
            }
            Py_DECREF(object);
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
                                     const RectList& dirty_rects, const void* buffer,
                                     int width, int height) {
    if (tab && type == PET_VIEW) {
        // 复制像素数据
        size_t bufferSize = width * height * 4;
        tab->pixel_buffer.resize(bufferSize);
        memcpy(tab->pixel_buffer.data(), buffer, bufferSize);
        tab->buffer_dirty = true;
    }
}
