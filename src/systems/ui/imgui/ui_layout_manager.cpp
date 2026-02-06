#include "ui_layout_manager.h"

namespace Corona::Systems::UI {

ImGuiID UILayoutManager::setup_dockspace() {
    // 创建透明的 DockSpace
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    // 设置 DockSpace 窗口标志
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking |
                                    ImGuiWindowFlags_NoTitleBar |
                                    ImGuiWindowFlags_NoCollapse |
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus |
                                    ImGuiWindowFlags_NoNavFocus |
                                    ImGuiWindowFlags_NoBackground;

    // 开始 DockSpace 窗口
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    // 设置窗口背景完全透明
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_DockingEmptyBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    ImGui::Begin("DockSpace", nullptr, window_flags);

    // 弹出样式
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);

    // 创建 DockSpace
    ImGuiID dock_space_id = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dock_space_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    dockspace_active_ = true;

    return dock_space_id;
}

void UILayoutManager::end_dockspace() {
    if (dockspace_active_) {
        ImGui::End();
        dockspace_active_ = false;
    }
}

}  // namespace Corona::Systems::UI
