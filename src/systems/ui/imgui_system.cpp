#include <SDL3/SDL_vulkan.h>
#include <corona/kernel/core/i_logger.h>
#include <corona/kernel/event/i_event_bus.h>
#include <corona/kernel/event/i_event_stream.h>
#include <corona/systems/ui/imgui_system.h>
#include <imgui_internal.h>
#include <nanobind/nanobind.h>

#include <cstdarg>
#include <filesystem>
#include <iostream>

#include "res/browser_types.h"
#include "res/browser_window.h"
#include "res/cef_client.h"

CefMessageRouterConfig message_router_config;

namespace Corona::Systems {

bool ImguiSystem::initialize(Kernel::ISystemContext* ctx) {
    CFW_LOG_NOTICE("ImguiSystem: Initializing...");

    // 设置 CEF 消息路由函数名称
    message_router_config.js_query_function = "cefQuery";
    message_router_config.js_cancel_function = "cefQueryCancel";

    // 初始化 CEF
    CefMainArgs mainArgs(GetModuleHandle(nullptr));
    CefRefPtr<SimpleApp> app(new SimpleApp());
    int exitCode = CefExecuteProcess(mainArgs, app.get(), nullptr);
    if (exitCode >= 0) {
        return exitCode;
    }

    // 配置 CEF 设置
    CefSettings settings;
    settings.multi_threaded_message_loop = true;   // 启用多线程消息循环
    settings.windowless_rendering_enabled = true;  // 启用无窗口渲染
    settings.no_sandbox = true;                    // 禁用沙箱
    settings.remote_debugging_port = 9222;         // 设置远程调试端口
    settings.log_severity = LOGSEVERITY_INFO;      // 设置日志级别
    settings.uncaught_exception_stack_size = 10;   // 设置未捕获异常堆栈大小

    // 设置语言为简体中文
    CefString(&settings.locale).FromASCII("zh-CN");

    // 设置资源和本地化文件路径
    std::filesystem::path cache_path = std::filesystem::current_path() / "cache";
    if (!std::filesystem::exists(cache_path)) {
        std::filesystem::create_directories(cache_path);
    }
    CefString(&settings.cache_path).FromString(cache_path.string());

    // 设置子进程可执行文件路径和用户代理
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    CefString(&settings.browser_subprocess_path).FromWString(exe_path);
    CefString(&settings.user_agent).FromASCII("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
    settings.background_color = CefColorSetARGB(255, 255, 255, 255);
    settings.persist_session_cookies = true;

    // 处理命令行参数
    CefRefPtr<CefCommandLine> command_line = CefCommandLine::CreateCommandLine();
    command_line->InitFromString(::GetCommandLineW());

    if (!CefInitialize(mainArgs, settings, app.get(), nullptr)) {
        std::cerr << "CefInitialize failed!" << std::endl;
        return EXIT_FAILURE;
    }

    running_ = true;
    pending_key_events_.clear();
    active_tab_id_ = -1;

    return true;
}

void ImguiSystem::thread_loop() {
    // 在系统线程上运行，以确保 SDL 事件处理在同一线程上进行
    // 初始化 SDL
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << '\n';
        CefShutdown();
        running_ = false;
        return;
    }

    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
    window_ = SDL_CreateWindow("Corona Engine (Vulkan)", 1400, 900,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (window_ == nullptr) {
        std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << '\n';
        SDL_Quit();
        CefShutdown();
        running_ = false;
        return;
    }

    SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window_);
    // 启用文本输入和IME支持
    SDL_StartTextInput(window_);
    SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, "1");

    vulkan_backend_ = std::make_unique<VulkanBackend>(window_);
    vulkan_backend_->initialize();
    g_vulkan_backend = vulkan_backend_.get();

    // 初始化 ImGui 上下文
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    io_ = &ImGui::GetIO();
    io_->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io_->ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io_->ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    // 设置ImGui样式 - 使用深色主题但带透明窗口
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();

    // 关键：只设置窗口相关颜色为透明
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);        // 窗口背景完全透明
    style.Colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);  // Docking空区域透明
    style.Colors[ImGuiCol_DockingPreview] = ImVec4(0.2f, 0.2f, 0.8f, 0.3f);  // Docking预览半透明

    // 调整窗口圆角和边框
    style.WindowRounding = 6.0f;
    style.WindowBorderSize = 1.0f;
    style.WindowPadding = ImVec2(8.0f, 8.0f);

    // 初始化 ImGui SDL3 和 Vulkan 后端
    ImGui_ImplSDL3_InitForVulkan(window_);

    // 设置 ImGui Vulkan 初始化信息
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = vulkan_backend_->get_instance();
    init_info.PhysicalDevice = vulkan_backend_->get_physical_device();
    init_info.Device = vulkan_backend_->get_device();
    init_info.QueueFamily = vulkan_backend_->get_queue_family();
    init_info.Queue = vulkan_backend_->get_queue();
    init_info.DescriptorPool = vulkan_backend_->get_descriptor_pool();
    init_info.PipelineInfoMain.RenderPass = vulkan_backend_->get_render_pass();
    init_info.MinImageCount = vulkan_backend_->get_min_image_count();
    init_info.ImageCount = vulkan_backend_->get_image_count();
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    init_info.UseDynamicRendering = false;

    ImGui_ImplVulkan_Init(&init_info);

    using Corona::Kernel::SystemState;
    while (running_ && get_state() == SystemState::running) {
        update();
    }

    // 清理和关闭
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (vulkan_backend_) {
        vulkan_backend_->shutdown();
        vulkan_backend_.reset();
        g_vulkan_backend = nullptr;
    }

    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }

    SDL_StopTextInput(window_);
    SDL_Quit();
    CefShutdown();
}

void ImguiSystem::update() {
    if (!running_) {
        return;
    }

    static int url_input_active_tab = -1;

    pending_key_events_.clear();

    // 处理 SDL 事件
    while (SDL_PollEvent(&event_)) {
        bool should_process_in_imgui = true;
        bool is_input_method_switch = false;

        if (event_.type == SDL_EVENT_KEY_DOWN || event_.type == SDL_EVENT_KEY_UP) {
            int key = static_cast<int>(event_.key.key);
            bool ctrl = (event_.key.mod & SDL_KMOD_CTRL) != 0;
            bool shift = (event_.key.mod & SDL_KMOD_SHIFT) != 0;
            bool alt = (event_.key.mod & SDL_KMOD_ALT) != 0;

            if ((ctrl && shift) || (alt && shift) ||
                (ctrl && key == SDLK_SPACE) ||
                (event_.key.mod & SDL_KMOD_GUI && key == SDLK_SPACE)) {
                is_input_method_switch = true;
            }
        }

        if (is_input_method_switch) {
            ImGui_ImplSDL3_ProcessEvent(&event_);
            continue;
        }

        // 处理键盘和文本输入事件
        if (event_.type == SDL_EVENT_KEY_DOWN ||
            event_.type == SDL_EVENT_KEY_UP ||
            event_.type == SDL_EVENT_TEXT_INPUT ||
            event_.type == SDL_EVENT_TEXT_EDITING) {
            switch (event_.type) {
                case SDL_EVENT_KEY_DOWN:
                case SDL_EVENT_KEY_UP:
                    process_sdl_key_event(event_);
                    break;
                case SDL_EVENT_TEXT_INPUT:
                    process_sdl_text_event(event_);
                    break;
                case SDL_EVENT_TEXT_EDITING:
                    process_sdl_ime_event(event_);
                    break;
                default:
                    break;
            }

            if (url_input_active_tab == -1) {
                should_process_in_imgui = false;
            }
        }

        // 将事件传递给 ImGui 进行处理
        if (should_process_in_imgui) {
            ImGui_ImplSDL3_ProcessEvent(&event_);
        }

        switch (event_.type) {
            case SDL_EVENT_QUIT:  // 退出事件
                running_ = false;
                if (should_process_in_imgui) {
                    ImGui_ImplSDL3_ProcessEvent(&event_);
                }
                break;
            case SDL_EVENT_WINDOW_RESIZED:  // 窗口大小调整事件
                if (event_.window.windowID == SDL_GetWindowID(window_)) {
                    vulkan_backend_->set_swap_chain_rebuild(true);
                    if (should_process_in_imgui) {
                        ImGui_ImplSDL3_ProcessEvent(&event_);
                    }
                }
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:  // 窗口获得焦点事件
            case SDL_EVENT_WINDOW_FOCUS_LOST:    // 窗口失去焦点事件
                if (should_process_in_imgui) {
                    ImGui_ImplSDL3_ProcessEvent(&event_);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:  // 鼠标按钮按下事件
            case SDL_EVENT_MOUSE_BUTTON_UP:    // 鼠标按钮抬起事件
            case SDL_EVENT_MOUSE_MOTION:       // 鼠标移动事件
            case SDL_EVENT_MOUSE_WHEEL:        // 鼠标滚轮事件
                ImGui_ImplSDL3_ProcessEvent(&event_);
                break;
            default:
                if (should_process_in_imgui) {
                    ImGui_ImplSDL3_ProcessEvent(&event_);
                }
                break;
        }
    }

    // 处理挂起的键盘事件
    if (vulkan_backend_->is_swap_chain_rebuild()) {
        int width, height;
        SDL_GetWindowSize(window_, &width, &height);
        vulkan_backend_->rebuild_swap_chain(width, height);
    }

    // 开始新的 ImGui 帧
    vulkan_backend_->new_frame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // 创建透明的DockSpace
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    // 设置DockSpace窗口标志
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar |
                                    ImGuiWindowFlags_NoDocking |
                                    ImGuiWindowFlags_NoTitleBar |
                                    ImGuiWindowFlags_NoCollapse |
                                    ImGuiWindowFlags_NoResize |
                                    ImGuiWindowFlags_NoMove |
                                    ImGuiWindowFlags_NoBringToFrontOnFocus |
                                    ImGuiWindowFlags_NoNavFocus |
                                    ImGuiWindowFlags_NoBackground;  // 重要：无背景

    // 开始DockSpace窗口
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    // 关键：设置窗口背景完全透明
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_DockingEmptyBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

    ImGui::Begin("DockSpace", nullptr, window_flags);

    // 弹出样式
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(3);

    // 创建DockSpace
    ImGuiID dockSpaceId = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

    // 创建主菜单栏
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Tab")) {
                create_browser_tab("https://www.baidu.com");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                running_ = false;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Websites")) {
            if (ImGui::MenuItem("Baidu")) {
                create_browser_tab("https://www.baidu.com");
            }
            if (ImGui::MenuItem("Bing")) {
                create_browser_tab("https://www.bing.com");
            }
            if (ImGui::MenuItem("Google")) {
                create_browser_tab("https://www.google.com");
            }
            if (ImGui::MenuItem("GitHub")) {
                create_browser_tab("https://www.github.com");
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    ImGui::End();

    // 渲染浏览器标签页
    std::vector<int> tabsToClose;
    for (auto& [tabId, tab] : tabs) {
        if (!tab->open) {
            tabsToClose.push_back(tabId);
            continue;
        }

        update_browser_texture(tabId);  // 更新浏览器纹理

        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
        std::string window_id = tab->name + "##" + std::to_string(tabId);

        if (ImGui::Begin(window_id.c_str(), &tab->open,
                         ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoNavInputs |
                             ImGuiWindowFlags_NoNavFocus)) {
            ImGui::PushItemWidth(-200);
            std::string url_input_id = "##url_" + std::to_string(tabId);
            // URL 输入框
            if (ImGui::InputText(url_input_id.c_str(), tab->url_buffer, sizeof(tab->url_buffer),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                if (tab->client && tab->client->GetBrowser()) {
                    tab->client->GetBrowser()->GetMainFrame()->LoadURL(tab->url_buffer);
                }
            }

            if (ImGui::IsItemActive()) {
                url_input_active_tab = tabId;
                active_tab_id_ = -1;

                // 当URL输入框激活时，移除浏览器焦点
                if (tab->client && tab->client->GetBrowser()) {
                    tab->client->GetBrowser()->GetHost()->SetFocus(false);
                    has_browser_focus_ = false;
                }
            }
            // 导航按钮
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button("Go")) {
                if (tab->client && tab->client->GetBrowser()) {
                    tab->client->GetBrowser()->GetMainFrame()->LoadURL(tab->url_buffer);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Back")) {
                if (tab->client && tab->client->GetBrowser()) {
                    tab->client->GetBrowser()->GoBack();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Forward")) {
                if (tab->client && tab->client->GetBrowser()) {
                    tab->client->GetBrowser()->GoForward();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Refresh")) {
                if (tab->client && tab->client->GetBrowser()) {
                    tab->client->GetBrowser()->Reload();
                }
            }
            // 浏览器内容区域
            ImVec2 availSize = ImGui::GetContentRegionAvail();
            int newWidth = static_cast<int>(availSize.x);
            int newHeight = static_cast<int>(availSize.y);

            if (newWidth > 0 && newHeight > 0 &&
                (newWidth != tab->width || newHeight != tab->height)) {
                tab->width = newWidth;
                tab->height = newHeight;

                if (tab->texture_id != VK_NULL_HANDLE) {
                    tab->texture_id = VK_NULL_HANDLE;
                }
                tab->texture_id = create_browser_texture(tab->width, tab->height);

                if (tab->client) {
                    tab->client->Resize(tab->width, tab->height);
                }
            }

            // 修改浏览器内容区域的鼠标事件处理
            if (tab->texture_id != VK_NULL_HANDLE) {
                ImGui::Image((ImTextureID)(intptr_t)tab->texture_id, availSize);

                bool browser_hovered = ImGui::IsItemHovered();

                if (browser_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    active_tab_id_ = tabId;
                    url_input_active_tab = -1;

                    // 1. 获取当前状态
                    Uint32 currentTime = SDL_GetTicks();
                    ImVec2 currentPos = ImGui::GetMousePos();
                    ImVec2 itemPos = ImGui::GetItemRectMin();

                    // 2. 内部逻辑：计算连击次数
                    float distance = sqrtf(powf(currentPos.x - last_click_pos_.x, 2) +
                                           powf(currentPos.y - last_click_pos_.y, 2));

                    if ((currentTime - last_click_time_) < kDoubleClickTime && distance < kDoubleClickDist) {
                        manual_click_count_++;
                    } else {
                        manual_click_count_ = 1;  // 重置为单击
                    }

                    // 更新上一次的状态
                    last_click_time_ = currentTime;
                    last_click_pos_ = currentPos;

                    // 3. 准备 CEF 事件
                    if (tab->client && tab->client->GetBrowser()) {
                        tab->client->GetBrowser()->GetHost()->SetFocus(true);
                        has_browser_focus_ = true;

                        CefMouseEvent mouseEvent;
                        mouseEvent.x = static_cast<int>(currentPos.x - itemPos.x);
                        mouseEvent.y = static_cast<int>(currentPos.y - itemPos.y);

                        // 设置修饰键状态
                        mouseEvent.modifiers = EVENTFLAG_LEFT_MOUSE_BUTTON;
                        if (ImGui::GetIO().KeyCtrl) mouseEvent.modifiers |= EVENTFLAG_CONTROL_DOWN;
                        if (ImGui::GetIO().KeyShift) mouseEvent.modifiers |= EVENTFLAG_SHIFT_DOWN;

                        // 发送鼠标按下事件 (带上我们计算的计数)
                        tab->client->GetBrowser()->GetHost()->SendMouseClickEvent(mouseEvent, MBT_LEFT, false, manual_click_count_);

                        is_left_mouse_down_ = true;
                    }
                }

                // 处理鼠标抬起 (Mouse Up)
                if (browser_hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                    if (is_left_mouse_down_) {
                        if (tab->client && tab->client->GetBrowser()) {
                            ImVec2 mousePos = ImGui::GetMousePos();
                            ImVec2 itemPos = ImGui::GetItemRectMin();

                            CefMouseEvent mouseEvent;
                            mouseEvent.x = static_cast<int>(mousePos.x - itemPos.x);
                            mouseEvent.y = static_cast<int>(mousePos.y - itemPos.y);

                            // 抬起时必须与按下时的 clickCount 保持一致，否则双击无效
                            tab->client->GetBrowser()->GetHost()->SendMouseClickEvent(mouseEvent, MBT_LEFT, true, manual_click_count_);
                        }
                        is_left_mouse_down_ = false;

                        // 如果连击次数过大，可以在此处选择是否重置 (通常由时间差逻辑自然处理)
                        if (manual_click_count_ >= 3) manual_click_count_ = 0;
                    }
                }

                if (browser_hovered) {
                    CefRefPtr<CefBrowser> browser = tab->client ? tab->client->GetBrowser() : nullptr;
                    if (browser) {
                        ImVec2 mousePos = ImGui::GetMousePos();
                        ImVec2 itemPos = ImGui::GetItemRectMin();
                        int x = static_cast<int>(mousePos.x - itemPos.x);
                        int y = static_cast<int>(mousePos.y - itemPos.y);

                        CefMouseEvent mouseEvent;
                        mouseEvent.x = x;
                        mouseEvent.y = y;

                        // 设置修饰键状态
                        mouseEvent.modifiers = 0;
                        if (is_left_mouse_down_) {
                            mouseEvent.modifiers |= EVENTFLAG_LEFT_MOUSE_BUTTON;
                        }
                        if (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)) {
                            mouseEvent.modifiers |= EVENTFLAG_SHIFT_DOWN;
                        }
                        if (ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl)) {
                            mouseEvent.modifiers |= EVENTFLAG_CONTROL_DOWN;
                        }

                        // 发送鼠标移动事件
                        // 关键：总是发送鼠标移动事件，让CEF知道鼠标位置
                        browser->GetHost()->SendMouseMoveEvent(mouseEvent, false);

                        // 处理鼠标拖动选择文本
                        if (is_left_mouse_down_) {
                            // 检查是否开始拖动
                            ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
                            float dragDistance = dragDelta.x * dragDelta.x + dragDelta.y * dragDelta.y;

                            if (dragDistance > 4.0f) {  // 拖动超过2像素
                                is_mouse_dragging_ = true;

                                // 在拖动过程中，持续发送鼠标移动事件以支持文本选择
                                browser->GetHost()->SendMouseMoveEvent(mouseEvent, false);
                            }
                        }

                        // 鼠标滚轮事件
                        float wheel = io_->MouseWheel;
                        if (wheel != 0) {
                            browser->GetHost()->SendMouseWheelEvent(mouseEvent, 0, static_cast<int>(wheel * 100));
                        }
                    }
                }
            }
        }
        ImGui::End();
    }

    // 发送键盘事件给浏览器
    if (active_tab_id_ != -1 && url_input_active_tab == -1 && !pending_key_events_.empty()) {
        send_key_events_to_browser(active_tab_id_);
    }

    for (auto tabId : tabsToClose) {
        close_browser_tab(tabId);
        tabs.erase(tabId);
        if (tabId == active_tab_id_) {
            active_tab_id_ = -1;
        }
        if (tabId == url_input_active_tab) {
            url_input_active_tab = -1;
        }
    }

    // 渲染 ImGui
    ImGui::Render();
    ImDrawData* draw_data = ImGui::GetDrawData();
    const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
    if (!is_minimized) {
        vulkan_backend_->render_frame(draw_data);
        vulkan_backend_->present_frame();
    }

    if (io_->ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

void ImguiSystem::shutdown() {
    CFW_LOG_NOTICE("DisplaySystem: Shutting down...");
    running_ = false;

    for (auto& [tabId, tab] : tabs) {
        close_browser_tab(tabId);
    }
    tabs.clear();
    pending_key_events_.clear();
}

// 发送键盘事件到浏览器
void ImguiSystem::send_key_events_to_browser(int tab_id) {
    if (tabs.find(tab_id) == tabs.end() || !tabs[tab_id]->client ||
        !tabs[tab_id]->client->GetBrowser()) {
        return;
    }

    BrowserTab* tab = tabs[tab_id];
    CefRefPtr<CefBrowser> browser = tab->client->GetBrowser();

    for (const auto& pending_event : pending_key_events_) {
        if (pending_event.type == PendingKeyEvent::kMKeyEvent) {
            CefKeyEvent cef_key_event;

            // 设置事件类型
            cef_key_event.type = pending_event.pressed ? KEYEVENT_RAWKEYDOWN : KEYEVENT_KEYUP;

            // 转换键码
            cef_key_event.windows_key_code = convert_sdl_key_code_to_windows(pending_event.key_code);
            cef_key_event.native_key_code = pending_event.scan_code;

            // 设置修饰键
            cef_key_event.modifiers = pending_event.modifiers;

            // 对于组合键（如Ctrl+C），需要特殊处理
            cef_key_event.character = pending_event.key_code;
            cef_key_event.unmodified_character = pending_event.key_code;

            // 对于常见的编辑组合键，发送完整的键序列
            bool is_common_edit_shortcut = false;
            if (pending_event.modifiers & EVENTFLAG_CONTROL_DOWN) {
                switch (pending_event.key_code) {
                    case SDLK_A:  // Ctrl+A (全选)
                    case SDLK_C:  // Ctrl+C (复制)
                    case SDLK_V:  // Ctrl+V (粘贴)
                    case SDLK_Z:  // Ctrl+Z (撤销)
                    case SDLK_Y:  // Ctrl+Y (重做/复原)
                        is_common_edit_shortcut = true;
                        break;
                    default:
                        break;
                }
            }

            // 发送RAWKEYDOWN或KEYUP事件
            browser->GetHost()->SendKeyEvent(cef_key_event);

            // 特殊处理回车键：总是发送CHAR事件
            if (pending_event.pressed &&
                (pending_event.key_code == SDLK_RETURN || pending_event.key_code == SDLK_KP_ENTER)) {
                // 对于回车键，需要发送CHAR事件以便浏览器能处理换行
                CefKeyEvent char_event = cef_key_event;
                char_event.type = KEYEVENT_CHAR;
                char_event.character = 0x0D;  // 回车符的ASCII码
                char_event.unmodified_character = 0x0D;
                browser->GetHost()->SendKeyEvent(char_event);
            }

            // 对于组合键，需要发送CHAR事件以确保浏览器能正确处理
            if (pending_event.pressed && pending_event.is_modifier_combo) {
                // 对于编辑组合键，发送CHAR事件
                if (is_common_edit_shortcut) {
                    cef_key_event.type = KEYEVENT_CHAR;
                    browser->GetHost()->SendKeyEvent(cef_key_event);
                } else {
                    // 对于其他组合键，根据原始逻辑处理
                    switch (pending_event.key_code) {
                        case SDLK_RETURN:
                        case SDLK_KP_ENTER:
                        case SDLK_TAB:
                        case SDLK_BACKSPACE:
                        case SDLK_DELETE:
                        case SDLK_ESCAPE:
                            // 这些特殊键需要发送CHAR事件
                            cef_key_event.type = KEYEVENT_CHAR;
                            browser->GetHost()->SendKeyEvent(cef_key_event);
                            break;

                        default:
                            // 对于字母、数字、符号等常规组合键，不发送CHAR事件
                            // 这些字符将通过TEXT_EVENT处理
                            break;
                    }
                }
            }
        } else if (pending_event.type == PendingKeyEvent::kTextEvent) {
            // 处理文本输入 - 所有字符输入都通过这里处理
            const std::string& text = pending_event.text;
            if (!text.empty()) {
                // 检查文本中是否包含控制字符
                bool has_control_chars = false;
                for (char c : text) {
                    if (c == '\b' || c == '\t' || c == '\n' || c == '\r') {
                        has_control_chars = true;
                        break;
                    }
                }

                if (!has_control_chars) {
                    // 处理普通文本字符
                    bool is_ascii = true;
                    for (char c : text) {
                        if (static_cast<unsigned char>(c) >= 128) {
                            is_ascii = false;
                            break;
                        }
                    }

                    if (is_ascii) {
                        // ASCII文本，直接发送
                        for (char c : text) {
                            if (c >= 32 && c < 127) {  // 可打印字符
                                CefKeyEvent cef_text_event;
                                cef_text_event.type = KEYEVENT_CHAR;
                                cef_text_event.modifiers = 0;
                                cef_text_event.windows_key_code = static_cast<uint16_t>(c);
                                cef_text_event.native_key_code = static_cast<uint16_t>(c);
                                cef_text_event.character = static_cast<uint16_t>(c);
                                cef_text_event.unmodified_character = static_cast<uint16_t>(c);
                                browser->GetHost()->SendKeyEvent(cef_text_event);
                            }
                        }
                    } else {
                        // 非ASCII文本（中文），使用UTF-16转换
                        char* utf16_text = SDL_iconv_string("UTF-16LE", "UTF-8", text.c_str(), text.length() + 1);
                        if (utf16_text) {
                            auto* utf16_chars = reinterpret_cast<uint16_t*>(utf16_text);
                            size_t utf16_len = 0;
                            while (utf16_chars[utf16_len] != 0) {
                                utf16_len++;
                            }
                            for (size_t i = 0; i < utf16_len; i++) {
                                CefKeyEvent cef_text_event;
                                cef_text_event.type = KEYEVENT_CHAR;
                                cef_text_event.modifiers = 0;
                                cef_text_event.windows_key_code = utf16_chars[i];
                                cef_text_event.native_key_code = utf16_chars[i];
                                cef_text_event.character = utf16_chars[i];
                                cef_text_event.unmodified_character = utf16_chars[i];
                                browser->GetHost()->SendKeyEvent(cef_text_event);
                            }
                            SDL_free(utf16_text);
                        }
                    }
                }
            }
        }
    }

    // 清空待处理事件
    pending_key_events_.clear();
}

// 处理SDL键盘事件
void ImguiSystem::process_sdl_key_event(const SDL_Event& event) {
    bool pressed = (event.type == SDL_EVENT_KEY_DOWN);
    int key_code = static_cast<int>(event.key.key);
    int scan_code = static_cast<int>(event.key.scancode);
    int modifiers = 0;

    // 转换SDL modifiers到CEF modifiers
    Uint32 sdl_mod = event.key.mod;
    if (sdl_mod & SDL_KMOD_CTRL) modifiers |= EVENTFLAG_CONTROL_DOWN;
    if (sdl_mod & SDL_KMOD_SHIFT) modifiers |= EVENTFLAG_SHIFT_DOWN;
    if (sdl_mod & SDL_KMOD_ALT) modifiers |= EVENTFLAG_ALT_DOWN;
    if (sdl_mod & SDL_KMOD_GUI) modifiers |= EVENTFLAG_COMMAND_DOWN;
    if (sdl_mod & SDL_KMOD_CAPS) modifiers |= EVENTFLAG_CAPS_LOCK_ON;
    if (sdl_mod & SDL_KMOD_NUM) modifiers |= EVENTFLAG_NUM_LOCK_ON;

    // 检测常见的编辑组合键
    bool is_common_edit_shortcut = false;
    if (modifiers & EVENTFLAG_CONTROL_DOWN) {
        switch (key_code) {
            case SDLK_A:  // Ctrl+A (全选)
            case SDLK_C:  // Ctrl+C (复制)
            case SDLK_V:  // Ctrl+V (粘贴)
            case SDLK_Z:  // Ctrl+Z (撤销)
            case SDLK_Y:  // Ctrl+Y (重做/复原)
                is_common_edit_shortcut = true;
                break;
            default:
                break;
        }
    }

    // 对于Ctrl/Alt+字母等组合键，需要特殊处理
    bool is_modifier_combo = (modifiers & (EVENTFLAG_CONTROL_DOWN | EVENTFLAG_ALT_DOWN)) &&
                             ((key_code >= 'a' && key_code <= 'z') ||
                              (key_code >= 'A' && key_code <= 'Z') ||
                              (key_code >= '0' && key_code <= '9'));

    // 存储键盘事件
    PendingKeyEvent key_event(PendingKeyEvent::kMKeyEvent);
    key_event.key_code = key_code;
    key_event.scan_code = scan_code;
    key_event.modifiers = modifiers;
    key_event.pressed = pressed;
    key_event.is_modifier_combo = is_modifier_combo || is_common_edit_shortcut;  // 标记为组合键

    pending_key_events_.push_back(key_event);
}

// 处理SDL文本输入事件
void ImguiSystem::process_sdl_text_event(const SDL_Event& event) {
    if (event.text.text && event.text.text[0]) {
        PendingKeyEvent text_event(PendingKeyEvent::kTextEvent);
        text_event.text = event.text.text;
        pending_key_events_.push_back(text_event);
    }
}

// 处理SDL IME事件
void ImguiSystem::process_sdl_ime_event(const SDL_Event& event) {
    if (event.edit.text && event.edit.text[0]) {
        PendingKeyEvent ime_event(PendingKeyEvent::kImeComposition);
        ime_event.text = event.edit.text;
        ime_event.ime_start = event.edit.start;
        ime_event.ime_length = event.edit.length;
        pending_key_events_.push_back(ime_event);
    }
}

}  // namespace Corona::Systems
