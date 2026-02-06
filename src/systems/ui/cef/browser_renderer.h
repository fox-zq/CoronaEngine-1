#pragma once

#include <imgui.h>
#include <vector>
#include <memory>

namespace Corona::Systems::UI {

// 浏览器窗口渲染器
class BrowserRenderer {
public:
    BrowserRenderer() = default;

    // 渲染所有浏览器标签页
    // 返回需要关闭的标签页ID列表
    std::vector<int> render_browser_tabs(ImGuiID dock_space_id,
                                          int& active_tab_id,
                                          int& url_input_active_tab,
                                          ImGuiIO* io);

private:
    // 渲染单个浏览器标签页
    void render_single_tab(int tab_id,
                           ImGuiID dock_space_id,
                           int& active_tab_id,
                           int& url_input_active_tab,
                           ImGuiIO* io);

    // 设置窗口位置和大小
    void setup_window_transform(struct BrowserTab* tab,
                                ImGuiID dock_space_id,
                                bool is_main_tab);

    // 处理浏览器内容区域的鼠标事件
    void handle_browser_mouse_events(struct BrowserTab* tab,
                                     int tab_id,
                                     int& active_tab_id,
                                     int& url_input_active_tab,
                                     ImGuiIO* io);
};

}  // namespace Corona::Systems::UI
