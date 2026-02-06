#pragma once

#include <imgui.h>

namespace Corona::Systems::UI {

// UI 布局管理器
class UILayoutManager {
public:
    UILayoutManager() = default;

    // 设置并开始 DockSpace
    ImGuiID setup_dockspace();

    // 结束 DockSpace
    void end_dockspace();

private:
    bool dockspace_active_ = false;
};

}  // namespace Corona::Systems::UI
