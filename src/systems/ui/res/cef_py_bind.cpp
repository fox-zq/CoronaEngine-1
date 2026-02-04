#pragma once

#include <cef_app.h>
#include <corona/systems/script/engine_scripts.h>
#include <nanobind/nanobind.h>

#include "./browser_types.h"
#include "./cef_client.h"
namespace nb = nanobind;

namespace EngineScripts {

void BindCef(nanobind::module_& m) {
    // 向python注册创建浏览器标签页函数绑定
    m.def("create_browser_tab", [](nb::object py_url, nb::object py_path) -> int {
            try {
                if (!py_url.is_valid()) {
                    return -1;
                }
                nb::str py_url_str = nb::str(py_url);
                std::string url = py_url_str.c_str();
                nb::str py_path_str = nb::str(py_path);
                std::string path = py_path_str.c_str();
                return Corona::Systems::UI::create_browser_tab(url, path);
            } catch (const std::exception&) {
                return -1;
            } }, nb::arg("url") = "", nb::arg("path") = "", nb::rv_policy::take_ownership);

    // 向python注册执行JavaScript代码函数绑定
    m.def("execute_javascript", [](int tab_id, nb::object py_js_code) -> nb::str {
            try {
                if (!tabs.contains(tab_id)) {
                    return nb::str("{\"success\": false, \"error\": \"Tab not found\"}");
                }
                nb::str py_str = nb::str(py_js_code);
                std::string js_code = py_str.c_str();
                BrowserTab* tab = tabs[tab_id].get();
                if (tab->client && tab->client->GetBrowser()) {
                    if (CefRefPtr<CefFrame> frame = tab->client->GetBrowser()->GetMainFrame()) {
                        frame->ExecuteJavaScript(js_code, "", 0);
                    }
                }
                return nb::str("{\"success\": true}");
            } catch (const std::exception&) {
                return nb::str("{\"success\": false \"}");
            } }, nb::arg("tab_id"), nb::arg("js_code"));
}

}  // namespace EngineScripts