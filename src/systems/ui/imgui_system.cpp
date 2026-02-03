#include <SDL3/SDL_vulkan.h>
#include <corona/events/display_system_events.h>
#include <corona/events/engine_events.h>
#include <corona/kernel/core/i_logger.h>
#include <corona/kernel/event/i_event_bus.h>
#include <corona/kernel/event/i_event_stream.h>
#include <corona/systems/ui/imgui_system.h>
#include <nanobind/nanobind.h>
#include <imgui_internal.h>

#include <cstdarg>
#include <filesystem>
#include <iostream>

#include "res/BrowserWindow.h"
#include "res/browser_types.h"
#include "res/cef_client.h"

CefMessageRouterConfig g_messageRouterConfig;

namespace Corona::Systems {

bool ImguiSystem::initialize(Kernel::ISystemContext* ctx) {
    CFW_LOG_NOTICE("ImguiSystem: Initializing...");

    // 设置 CEF 消息路由函数名称
    g_messageRouterConfig.js_query_function = "cefQuery";
    g_messageRouterConfig.js_cancel_function = "cefQueryCancel";

    // 初始化 CEF
    CefMainArgs mainArgs(GetModuleHandle(nullptr));
    CefRefPtr<SimpleApp> app(new SimpleApp());
    int exitCode = CefExecuteProcess(mainArgs, app.get(), nullptr);
    if (exitCode >= 0) {
        return exitCode;
    }

    // 配置 CEF 设置
    CefSettings settings;
    settings.multi_threaded_message_loop = true;  // 启用多线程消息循环
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
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    CefString(&settings.browser_subprocess_path).FromWString(exePath);
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

    running = true;
    m_PendingKeyEvents.clear();
    m_ActiveTabId = -1;

    return true;
}

void ImguiSystem::thread_loop() {
    // 在系统线程上运行，以确保 SDL 事件处理在同一线程上进行
    // 初始化 SDL
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << '\n';
        CefShutdown();
        running = false;
        return;
    }

    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
    window = SDL_CreateWindow("Corona Engine (Vulkan)", 1400, 900,
                              SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (window == nullptr) {
        std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << '\n';
        SDL_Quit();
        CefShutdown();
        running = false;
        return;
    }

    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);
    // 启用文本输入和IME支持
    SDL_StartTextInput(window);
    SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, "1");


  

    m_VulkanBackend = std::make_unique<VulkanBackend>(window);
    m_VulkanBackend->Initialize();
    g_vulkan_backend = m_VulkanBackend.get();

    // 初始化 ImGui 上下文
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    io = &ImGui::GetIO();
    io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io->ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

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
    ImGui_ImplSDL3_InitForVulkan(window);

    // 设置 ImGui Vulkan 初始化信息
    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = m_VulkanBackend->GetInstance();
    init_info.PhysicalDevice = m_VulkanBackend->GetPhysicalDevice();
    init_info.Device = m_VulkanBackend->GetDevice();
    init_info.QueueFamily = m_VulkanBackend->GetQueueFamily();
    init_info.Queue = m_VulkanBackend->GetQueue();
    init_info.DescriptorPool = m_VulkanBackend->GetDescriptorPool();
    init_info.PipelineInfoMain.RenderPass = m_VulkanBackend->GetRenderPass();
    init_info.MinImageCount = m_VulkanBackend->GetMinImageCount();
    init_info.ImageCount = m_VulkanBackend->GetImageCount();
    init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    // 关键：启用动态渲染（新版本ImGui需要）
    init_info.UseDynamicRendering = false;

    ImGui_ImplVulkan_Init(&init_info);


    // 注册 Imgui 模块到嵌入式 Python 解释器
    CreateBrowserTab("file:///E:/workspace/CoronaEngine/build/examples/engine/RelWithDebInfo/test.html");
    showDemoWindow = false;

    using Corona::Kernel::SystemState;
    while (running && get_state() == SystemState::running) {
        update();
    }

    // 清理和关闭
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (m_VulkanBackend) {
        m_VulkanBackend->Shutdown();
        m_VulkanBackend.reset();
        g_vulkan_backend = nullptr;
    }

    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }

    SDL_StopTextInput(window);
    SDL_Quit();
    CefShutdown();
}

void ImguiSystem::update() {
    if (!running) {
        return;
    }

    static int url_input_active_tab = -1;
    static bool ime_composing = false;

    m_PendingKeyEvents.clear();

    // 处理 SDL 事件
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_TEXT_EDITING) {
            ime_composing = true;
        } else if (event.type == SDL_EVENT_TEXT_INPUT) {
            ime_composing = false;
        }

        bool should_process_in_imgui = true;
        bool is_input_method_switch = false;

        if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
            int key = event.key.key;
            bool ctrl = (event.key.mod & SDL_KMOD_CTRL) != 0;
            bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;
            bool alt = (event.key.mod & SDL_KMOD_ALT) != 0;

            if ((ctrl && shift) || (alt && shift) ||
                (ctrl && key == SDLK_SPACE) ||
                (event.key.mod & SDL_KMOD_GUI && key == SDLK_SPACE)) {
                is_input_method_switch = true;
            }
        }

        if (is_input_method_switch) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            continue;
        }

        // 处理键盘和文本输入事件
        if (event.type == SDL_EVENT_KEY_DOWN ||
            event.type == SDL_EVENT_KEY_UP ||
            event.type == SDL_EVENT_TEXT_INPUT ||
            event.type == SDL_EVENT_TEXT_EDITING) {
            switch (event.type) {
                case SDL_EVENT_KEY_DOWN:
                    ProcessSDLKeyEvent(event);
                    break;
                case SDL_EVENT_KEY_UP:
                    ProcessSDLKeyEvent(event);
                    break;
                case SDL_EVENT_TEXT_INPUT:
                    ProcessSDLTextEvent(event);
                    break;
                case SDL_EVENT_TEXT_EDITING:
                    ProcessSDLIMEEvent(event);
                    break;
            }

            if (url_input_active_tab == -1) {
                should_process_in_imgui = false;
            }
        }

        // 将事件传递给 ImGui 进行处理
        if (should_process_in_imgui) {
            ImGui_ImplSDL3_ProcessEvent(&event);
        }

        switch (event.type) {
            case SDL_EVENT_QUIT:  // 退出事件
                running = false;
                if (should_process_in_imgui) {
                    ImGui_ImplSDL3_ProcessEvent(&event);
                }
                break;
            case SDL_EVENT_WINDOW_RESIZED:  // 窗口大小调整事件
                if (event.window.windowID == SDL_GetWindowID(window)) {
                    m_VulkanBackend->SetSwapChainRebuild(true);
                    if (should_process_in_imgui) {
                        ImGui_ImplSDL3_ProcessEvent(&event);
                    }
                }
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:  // 窗口获得焦点事件
            case SDL_EVENT_WINDOW_FOCUS_LOST:    // 窗口失去焦点事件
                if (should_process_in_imgui) {
                    ImGui_ImplSDL3_ProcessEvent(&event);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:  // 鼠标按钮按下事件
            case SDL_EVENT_MOUSE_BUTTON_UP:    // 鼠标按钮抬起事件
            case SDL_EVENT_MOUSE_MOTION:       // 鼠标移动事件
            case SDL_EVENT_MOUSE_WHEEL:       // 鼠标滚轮事件
                ImGui_ImplSDL3_ProcessEvent(&event);
                break;
            default:
                if (should_process_in_imgui) {
                    ImGui_ImplSDL3_ProcessEvent(&event);
                }
                break;
        }
    }

    // 处理挂起的键盘事件
    if (m_VulkanBackend->IsSwapChainRebuild()) {
        int width, height;
        SDL_GetWindowSize(window, &width, &height);
        m_VulkanBackend->RebuildSwapChain(width, height);
    }

    // 开始新的 ImGui 帧
    m_VulkanBackend->NewFrame();
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
                CreateBrowserTab("https://www.baidu.com");
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                running = false;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("ImGui Demo", nullptr, &showDemoWindow);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Websites")) {
            if (ImGui::MenuItem("Baidu")) {
                CreateBrowserTab("https://www.baidu.com");
            }
            if (ImGui::MenuItem("Bing")) {
                CreateBrowserTab("https://www.bing.com");
            }
            if (ImGui::MenuItem("Google")) {
                CreateBrowserTab("https://www.google.com");
            }
            if (ImGui::MenuItem("GitHub")) {
                CreateBrowserTab("https://www.github.com");
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    ImGui::End();

    if (showDemoWindow) {
        ImGui::ShowDemoWindow(&showDemoWindow);
    }

    // 渲染浏览器标签页
    std::vector<int> tabsToClose;
    for (auto& [tabId, tab] : g_tabs) {
        if (!tab->open) {
            tabsToClose.push_back(tabId);
            continue;
        }

        UpdateBrowserTexture(tabId);  // 更新浏览器纹理

        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
        std::string window_id = tab->name + "##" + std::to_string(tabId);

        if (ImGui::Begin(window_id.c_str(), &tab->open,
                         ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoNavInputs |
                             ImGuiWindowFlags_NoNavFocus)) {
            ImGui::PushItemWidth(-200);
            std::string url_input_id = "##url_" + std::to_string(tabId);
            // URL 输入框
            if (ImGui::InputText(url_input_id.c_str(), tab->urlBuffer, sizeof(tab->urlBuffer),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                if (tab->client && tab->client->GetBrowser()) {
                    tab->client->GetBrowser()->GetMainFrame()->LoadURL(tab->urlBuffer);
                }
            }

            if (ImGui::IsItemActive()) {
                url_input_active_tab = tabId;
                m_ActiveTabId = -1;

                // 当URL输入框激活时，移除浏览器焦点
                if (tab->client && tab->client->GetBrowser()) {
                    tab->client->GetBrowser()->GetHost()->SetFocus(false);
                    m_HasBrowserFocus = false;
                }
            }
            // 导航按钮
            ImGui::PopItemWidth();
            ImGui::SameLine();
            if (ImGui::Button("Go")) {
                if (tab->client && tab->client->GetBrowser()) {
                    tab->client->GetBrowser()->GetMainFrame()->LoadURL(tab->urlBuffer);
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
            int newWidth = (int)availSize.x;
            int newHeight = (int)availSize.y;

            if (newWidth > 0 && newHeight > 0 &&
                (newWidth != tab->width || newHeight != tab->height)) {
                tab->width = newWidth;
                tab->height = newHeight;

                if (tab->textureId != VK_NULL_HANDLE) {
                    tab->textureId = VK_NULL_HANDLE;
                }
                tab->textureId = CreateBrowserTexture(tab->width, tab->height);

                if (tab->client) {
                    tab->client->Resize(tab->width, tab->height);
                }
            }

            // 修改浏览器内容区域的鼠标事件处理
            if (tab->textureId != VK_NULL_HANDLE) {
                ImGui::Image((ImTextureID)(intptr_t)tab->textureId, availSize);

                bool browser_hovered = ImGui::IsItemHovered();

                if (browser_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    m_ActiveTabId = tabId;
                    url_input_active_tab = -1;

                    // 1. 获取当前状态
                    Uint32 currentTime = SDL_GetTicks();
                    ImVec2 currentPos = ImGui::GetMousePos();
                    ImVec2 itemPos = ImGui::GetItemRectMin();

                    // 2. 内部逻辑：计算连击次数
                    float distance = sqrtf(powf(currentPos.x - m_LastClickPos.x, 2) +
                                           powf(currentPos.y - m_LastClickPos.y, 2));

                    if ((currentTime - m_LastClickTime) < DOUBLE_CLICK_TIME && distance < DOUBLE_CLICK_DIST) {
                        m_ManualClickCount++;
                    } else {
                        m_ManualClickCount = 1;  // 重置为单击
                    }

                    // 更新上一次的状态
                    m_LastClickTime = currentTime;
                    m_LastClickPos = currentPos;

                    // 3. 准备 CEF 事件
                    if (tab->client && tab->client->GetBrowser()) {
                        tab->client->GetBrowser()->GetHost()->SetFocus(true);
                        m_HasBrowserFocus = true;

                        CefMouseEvent mouseEvent;
                        mouseEvent.x = (int)(currentPos.x - itemPos.x);
                        mouseEvent.y = (int)(currentPos.y - itemPos.y);

                        // 设置修饰键状态
                        mouseEvent.modifiers = EVENTFLAG_LEFT_MOUSE_BUTTON;
                        if (ImGui::GetIO().KeyCtrl) mouseEvent.modifiers |= EVENTFLAG_CONTROL_DOWN;
                        if (ImGui::GetIO().KeyShift) mouseEvent.modifiers |= EVENTFLAG_SHIFT_DOWN;

                        // 发送鼠标按下事件 (带上我们计算的计数)
                        tab->client->GetBrowser()->GetHost()->SendMouseClickEvent(mouseEvent, MBT_LEFT, false, m_ManualClickCount);

                        m_IsLeftMouseDown = true;
                    }
                }

                // 处理鼠标抬起 (Mouse Up)
                if (browser_hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                    if (m_IsLeftMouseDown) {
                        if (tab->client && tab->client->GetBrowser()) {
                            ImVec2 mousePos = ImGui::GetMousePos();
                            ImVec2 itemPos = ImGui::GetItemRectMin();

                            CefMouseEvent mouseEvent;
                            mouseEvent.x = (int)(mousePos.x - itemPos.x);
                            mouseEvent.y = (int)(mousePos.y - itemPos.y);

                            // 抬起时必须与按下时的 clickCount 保持一致，否则双击无效
                            tab->client->GetBrowser()->GetHost()->SendMouseClickEvent(mouseEvent, MBT_LEFT, true, m_ManualClickCount);
                        }
                        m_IsLeftMouseDown = false;

                        // 如果连击次数过大，可以在此处选择是否重置 (通常由时间差逻辑自然处理)
                        if (m_ManualClickCount >= 3) m_ManualClickCount = 0;
                    }
                }

                if (browser_hovered) {
                    CefRefPtr<CefBrowser> browser = tab->client ? tab->client->GetBrowser() : nullptr;
                    if (browser) {
                        ImVec2 mousePos = ImGui::GetMousePos();
                        ImVec2 itemPos = ImGui::GetItemRectMin();
                        int x = (int)(mousePos.x - itemPos.x);
                        int y = (int)(mousePos.y - itemPos.y);

                        CefMouseEvent mouseEvent;
                        mouseEvent.x = x;
                        mouseEvent.y = y;

                        // 设置修饰键状态
                        mouseEvent.modifiers = 0;
                        if (m_IsLeftMouseDown) {
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
                        if (m_IsLeftMouseDown) {
                            // 检查是否开始拖动
                            ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
                            float dragDistance = dragDelta.x * dragDelta.x + dragDelta.y * dragDelta.y;

                            if (dragDistance > 4.0f) {  // 拖动超过2像素
                                m_IsMouseDragging = true;

                                // 在拖动过程中，持续发送鼠标移动事件以支持文本选择
                                browser->GetHost()->SendMouseMoveEvent(mouseEvent, false);
                            }
                        }

                        // 鼠标滚轮事件
                        float wheel = io->MouseWheel;
                        if (wheel != 0) {
                            browser->GetHost()->SendMouseWheelEvent(mouseEvent, 0, (int)(wheel * 100));
                        }
                    }
                }
            }
        }
        ImGui::End();
    }

    // 发送键盘事件给浏览器
    if (m_ActiveTabId != -1 && url_input_active_tab == -1 && !m_PendingKeyEvents.empty()) {
        SendKeyEventsToBrowser(m_ActiveTabId);
    }

    for (auto tabId : tabsToClose) {
        CloseBrowserTab(tabId);
        g_tabs.erase(tabId);
        if (tabId == m_ActiveTabId) {
            m_ActiveTabId = -1;
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
        m_VulkanBackend->RenderFrame(draw_data);
        m_VulkanBackend->PresentFrame();
    }

    if (io->ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

void ImguiSystem::shutdown() {
    CFW_LOG_NOTICE("DisplaySystem: Shutting down...");
    running = false;

    for (auto& [tabId, tab] : g_tabs) {
        CloseBrowserTab(tabId);
    }
    g_tabs.clear();
    m_PendingKeyEvents.clear();
}

// 发送键盘事件到浏览器
void ImguiSystem::SendKeyEventsToBrowser(int tabId) {
    if (g_tabs.find(tabId) == g_tabs.end() || !g_tabs[tabId]->client ||
        !g_tabs[tabId]->client->GetBrowser()) {
        return;
    }

    BrowserTab* tab = g_tabs[tabId];
    CefRefPtr<CefBrowser> browser = tab->client->GetBrowser();

    for (const auto& pending_event : m_PendingKeyEvents) {
        if (pending_event.type == PendingKeyEvent::MKEY_EVENT) {
            CefKeyEvent cef_key_event;

            // 设置事件类型
            cef_key_event.type = pending_event.pressed ? KEYEVENT_RAWKEYDOWN : KEYEVENT_KEYUP;

            // 转换键码
            cef_key_event.windows_key_code = ConvertSDLKeyCodeToWindows(pending_event.key_code);
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
        } else if (pending_event.type == PendingKeyEvent::TEXT_EVENT) {
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
                            uint16_t* utf16_chars = reinterpret_cast<uint16_t*>(utf16_text);
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
    m_PendingKeyEvents.clear();
}

// 处理SDL键盘事件
void ImguiSystem::ProcessSDLKeyEvent(const SDL_Event& event) {
    bool pressed = (event.type == SDL_EVENT_KEY_DOWN);
    int key_code = event.key.key;
    int scan_code = event.key.scancode;
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
        }
    }

    // 对于Ctrl/Alt+字母等组合键，需要特殊处理
    bool is_modifier_combo = (modifiers & (EVENTFLAG_CONTROL_DOWN | EVENTFLAG_ALT_DOWN)) &&
                             ((key_code >= 'a' && key_code <= 'z') ||
                              (key_code >= 'A' && key_code <= 'Z') ||
                              (key_code >= '0' && key_code <= '9'));

    // 存储键盘事件
    PendingKeyEvent key_event(PendingKeyEvent::MKEY_EVENT);
    key_event.key_code = key_code;
    key_event.scan_code = scan_code;
    key_event.modifiers = modifiers;
    key_event.pressed = pressed;
    key_event.is_modifier_combo = is_modifier_combo || is_common_edit_shortcut;  // 标记为组合键

    m_PendingKeyEvents.push_back(key_event);
}

// 处理SDL文本输入事件
void ImguiSystem::ProcessSDLTextEvent(const SDL_Event& event) {
    if (event.text.text && event.text.text[0]) {
        PendingKeyEvent text_event(PendingKeyEvent::TEXT_EVENT);
        text_event.text = event.text.text;
        m_PendingKeyEvents.push_back(text_event);
    }
}

// 处理SDL IME事件
void ImguiSystem::ProcessSDLIMEEvent(const SDL_Event& event) {
    if (event.edit.text && event.edit.text[0]) {
        PendingKeyEvent ime_event(PendingKeyEvent::IME_COMPOSITION);
        ime_event.text = event.edit.text;
        ime_event.ime_start = event.edit.start;
        ime_event.ime_length = event.edit.length;
        m_PendingKeyEvents.push_back(ime_event);
    }
}

}  // namespace Corona::Systems