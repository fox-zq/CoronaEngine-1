#include <corona/events/display_system_events.h>
#include <corona/events/engine_events.h>
#include <corona/kernel/core/i_logger.h>
#include <corona/kernel/event/i_event_bus.h>
#include <corona/kernel/event/i_event_stream.h>
#include <corona/systems/ui/imgui_system.h>
#include "res/BrowserWindow.h"
#include "res/cef_client.h"
#include "res/browser_types.h"
#include <iostream>
#include <filesystem>
#include <SDL3/SDL_vulkan.h>
#include <nanobind/nanobind.h>
namespace nb = nanobind;

NB_MODULE(Imgui, m) {
    m.doc() = "CoronaEngine embedded Python module (nanobind)";

    // 注册 BrowserTab 类到 Python
    //nb::class_<BrowserTab>(m, "BrowserTab")
    //    .def("__repr__", [](BrowserTab &self) {
    //        return "<BrowserTab object>";
    //    });

    // 注册 create_browser_tab 函数到 Python
    // python 创建浏览器标签页
    m.def("create_browser_tab", [](nb::object py_url, nb::object py_path) -> int {
        try 
        {
            // 手动转换 Python 字符串
            if (!py_url.is_valid()) {
                std::cerr << "[ERROR] Invalid Python object!" << std::endl;
                return -1;
            }

            // 获取字符串表示
            nb::str py_url_str = nb::str(py_url);
            std::string url = py_url_str.c_str();

            nb::str py_path_str = nb::str(py_path);
            std::string path = py_path_str.c_str();

            std::cout << "[NANOBIND PYOBJ] URL from Python: " << url << std::endl;
            return CreateBrowserTab(url, path);
        }
        catch (const std::exception& e)
        {
            std::cerr << "[ERROR] Exception in create_browser_tab: " << e.what() << std::endl;
            return -1;
        } }, nb::arg("url") = "", nb::arg("path") = "", nb::rv_policy::take_ownership);

    // 注册 execute_javascript 函数到 Python
    // python 指定标签页执行 JavaScript 代码
    m.def("execute_javascript", [](int tab_id, nb::object py_js_code) -> nb::str {
    try {
        if (g_tabs.find(tab_id) == g_tabs.end()) {
            return nb::str("{\"success\": false, \"error\": \"Tab not found\"}");
        }

        nb::str py_str = nb::str(py_js_code);
        std::string js_code = py_str.c_str();
        
        BrowserTab* tab = g_tabs[tab_id];
        if (tab->client && tab->client->GetBrowser()) {
            CefRefPtr<CefFrame> frame = tab->client->GetBrowser()->GetMainFrame();
            if (frame) {
                // 执行 JavaScript 代码
                std::cout << "[INFO] Now call tab js:" << tab_id << ",content:" << js_code << std::endl;
                frame->ExecuteJavaScript(js_code, "", 0);
            }
        }
        return nb::str("{\"success\": true}");
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Exception in execute_javascript: " << e.what() << std::endl;
        return nb::str("{\"success\": false \"}");
    } }, nb::arg("tab_id"), nb::arg("js_code"));
}

 
CefMessageRouterConfig g_messageRouterConfig;

extern "C" PyObject *PyInit_Imgui();

namespace Corona::Systems {


    bool ImguiSystem::initialize(Kernel::ISystemContext *ctx) {
        CFW_LOG_NOTICE("ImguiSystem: Initializing...");

        g_messageRouterConfig.js_query_function = "cefQuery"; // JS端调用的函数名
        g_messageRouterConfig.js_cancel_function = "cefQueryCancel"; // JS端取消函数名

        // CEF 初始化 (main process check and CefInitialize)
        CefMainArgs mainArgs(GetModuleHandle(nullptr));

        // 创建App实例
        CefRefPtr<SimpleApp> app(new SimpleApp());
        int exitCode = CefExecuteProcess(mainArgs, app.get(), nullptr);
        if (exitCode >= 0) {
            return exitCode;
        }

        CefSettings settings;
        settings.multi_threaded_message_loop = true;
        settings.windowless_rendering_enabled = true; // 启用离屏渲染
     
        settings.no_sandbox = true; // 禁用沙箱
        settings.remote_debugging_port = 9222; // 启用远程调试

        // 启用日志
        settings.log_severity = LOGSEVERITY_INFO; // 或LOGSEVERITY_WARNING减少输出
        settings.uncaught_exception_stack_size = 10; // 堆栈跟踪大小

        CefString(&settings.locale).FromASCII("zh-CN");
        std::filesystem::path cache_path = std::filesystem::current_path() / "cache";
        if (!std::filesystem::exists(cache_path)) {
            std::filesystem::create_directories(cache_path);
            std::cout << "Created cache directory: " << cache_path.string() << std::endl;
        }

        CefString(&settings.cache_path).FromString(cache_path.string());
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        CefString(&settings.browser_subprocess_path).FromWString(exePath);
        CefString(&settings.user_agent).FromASCII(
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
        settings.background_color = CefColorSetARGB(255, 255, 255, 255);
        settings.persist_session_cookies = true;

        CefRefPtr<CefCommandLine> command_line = CefCommandLine::CreateCommandLine();
        command_line->InitFromString(::GetCommandLineW());

        // 2. 检查是否有 --type 参数（CEF 自动添加）
        if (command_line->HasSwitch("type")) {
            std::string process_type = command_line->GetSwitchValue("type");
            std::cout << "进程类型: " << process_type;
        } else {
            std::cout << "这是主进程 (Browser 进程)";
        }

        if (!CefInitialize(mainArgs, settings, app.get(), nullptr)) {
            std::cerr << "CefInitialize failed!" << std::endl;
            return EXIT_FAILURE;
        }

        // Defer heavy graphics initialization to the system thread (thread_loop)
        running = true;

        return true;
    }

    void ImguiSystem::thread_loop() {
        // Perform SDL/Vulkan/ImGui initialization on this thread so event handling
        // and rendering occur on the same thread that calls update(). This avoids
        // windows becoming unresponsive when SDL event polling is done from another thread.

        // SDL 初始化
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            std::cerr << "SDL_Init Error: " << SDL_GetError() << '\n';
            CefShutdown();
            running = false;
            return;
        }

        // 创建 SDL 窗口 (带 Vulkan 支持)
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

        if (volkInitialize() != VK_SUCCESS) {
            std::cerr << "Failed to initialize Volk\n";
            running = false;
            return;
        }

        uint32_t extensions_count = 0;
        char const *const *extensions_names = SDL_Vulkan_GetInstanceExtensions(&extensions_count);
        std::vector<const char*> extensions;
        if (extensions_names) {
            for (uint32_t i = 0; i < extensions_count; i++) {
                extensions.push_back(extensions_names[i]);
            }
        }

        m_VulkanBackend = std::make_unique<VulkanBackend>(window);
        m_VulkanBackend->Initialize(extensions);
        // expose backend for browser helpers
        g_vulkan_backend = m_VulkanBackend.get();

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        io = &ImGui::GetIO();
        io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io->ConfigFlags |= ImGuiConfigFlags_DockingEnable; // 启用 Docking
        io->ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // 启用多视口

        ImGui::StyleColorsDark();

        ImGuiStyle &style = ImGui::GetStyle();
        if (io->ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        ImGui_ImplSDL3_InitForVulkan(window);

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
        init_info.PipelineInfoMain.MSAASamples = m_VulkanBackend->GetMSAASamples();

        ImGui_ImplVulkan_Init(&init_info);

        

        //CreateBrowserTab(ResolveHtmlPathForCef("CabbageEditor/Frontend/dist/index.html"));

                    // 注册 nanobind 导出的 CoronaEngine 模块
        PyImport_AppendInittab("Imgui", &PyInit_Imgui);
        CreateBrowserTab("file:///E:/workspace/CoronaEngine/build/examples/engine/RelWithDebInfo/test.html");
        //if (!Py_IsInitialized()) {
        //    Py_Initialize();
        //    PyEval_InitThreads();  // initialize and acquire the GIL
        //    PyEval_SaveThread();   // release GIL
        //}

        //PyGILState_STATE gstate = PyGILState_Ensure();

        //PyRun_SimpleString("import sys");
        //PyRun_SimpleString("import os");
        //PyRun_SimpleString("sys.path.insert(0, os.getcwd())");
        //PyObject *pName = PyUnicode_FromString("test");
        //PyObject *pModule = PyImport_Import(pName);

        //PyObject *pFunc = PyObject_GetAttrString(pModule, "open_browser");
        //PyObject *pArgs = PyTuple_New(1);
        //PyTuple_SetItem(pArgs, 0, PyUnicode_FromString("file:///E:/workspace/CoronaEngine/build/examples/engine/RelWithDebInfo/test.html"));
        //PyObject *pValue = PyObject_CallObject(pFunc, pArgs);

        //PyGILState_Release(gstate);

        showDemoWindow = false;

        // Main loop: call update() until should_run_ becomes false
        using Corona::Kernel::SystemState;
        while (running && get_state() == SystemState::running) {
            // call update which will poll events, render, etc.
            update();
        }

        // Cleanup
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
        SDL_Quit();

        CefShutdown();
    }

    void ImguiSystem::update() {
        if (!running) {
            return;
        }

        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);

            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
            if (event.type == SDL_EVENT_WINDOW_RESIZED && event.window.windowID == SDL_GetWindowID(window)) {
                // Resize swapchain
                m_VulkanBackend->SetSwapChainRebuild(true);
            }
        }

        if (m_VulkanBackend->IsSwapChainRebuild()) {
            int width, height;
            SDL_GetWindowSize(window, &width, &height);
            m_VulkanBackend->RebuildSwapChain(width, height);
        }

        // Start the Dear ImGui frame
        m_VulkanBackend->NewFrame();  // Must be called!
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // 创建 DockSpace
        ImGuiWindowFlags dockSpaceFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        const ImGuiViewport *viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        dockSpaceFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                            ImGuiWindowFlags_NoMove;
        dockSpaceFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("DockSpace", nullptr, dockSpaceFlags);
        ImGui::PopStyleVar(3);

        ImGuiID dockSpaceId = ImGui::GetID("MyDockSpace");
        ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

        // 菜单栏
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

        // 显示 ImGui Demo 窗口
        if (showDemoWindow) {
            ImGui::ShowDemoWindow(&showDemoWindow);
        }

        // 渲染每个浏览器标签页窗口
        std::vector<int> tabsToClose;
        for (auto &[tabId, tab] : g_tabs) {
            if (!tab->open) {
                tabsToClose.push_back(tabId);
                continue;
            }

            // 更新纹理
            UpdateBrowserTexture(tabId);

            ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

            if (ImGui::Begin(tab->name.c_str(), &tab->open, ImGuiWindowFlags_NoScrollbar)) {
                // URL 输入框和导航按钮
                ImGui::PushItemWidth(-200);
                if (ImGui::InputText("##url", tab->urlBuffer, sizeof(tab->urlBuffer),
                                        ImGuiInputTextFlags_EnterReturnsTrue)) {
                    if (tab->client && tab->client->GetBrowser()) {
                        tab->client->GetBrowser()->GetMainFrame()->LoadURL(tab->urlBuffer);
                    }
                }
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

                // 获取可用空间
                ImVec2 availSize = ImGui::GetContentRegionAvail();
                int newWidth = (int)availSize.x;
                int newHeight = (int)availSize.y;

                // 检查是否需要调整大小
                if (newWidth > 0 && newHeight > 0 &&
                    (newWidth != tab->width || newHeight != tab->height)) {
                    tab->width = newWidth;
                    tab->height = newHeight;

                    // 重新创建 Vulkan 纹理
                    if (tab->textureId != VK_NULL_HANDLE) {
                        // TODO: ImGui_ImplVulkan_RemoveTexture(tab->textureId);
                        tab->textureId = VK_NULL_HANDLE;
                    }
                    tab->textureId = CreateBrowserTexture(tab->width, tab->height);

                    // 通知浏览器调整大小
                    if (tab->client) {
                        tab->client->Resize(tab->width, tab->height);
                    }
                }

                // 显示浏览器内容
                if (tab->textureId != VK_NULL_HANDLE) {
                    ImGui::Image((ImTextureID)(intptr_t)tab->textureId, availSize);

                    // Input handling code...
                    // (Simplified for brevity, assuming existing code handles input well)
                    // 处理鼠标事件
                    if (ImGui::IsItemHovered()) {
                        CefRefPtr<CefBrowser> browser = tab->client ? tab->client->GetBrowser() : nullptr;
                        if (browser) {
                            ImVec2 mousePos = ImGui::GetMousePos();
                            ImVec2 itemPos = ImGui::GetItemRectMin();
                            int x = (int)(mousePos.x - itemPos.x);
                            int y = (int)(mousePos.y - itemPos.y);

                            CefMouseEvent mouseEvent;
                            mouseEvent.x = x;
                            mouseEvent.y = y;
                            mouseEvent.modifiers = 0;

                            browser->GetHost()->SendMouseMoveEvent(mouseEvent, false);

                            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                                browser->GetHost()->SendMouseClickEvent(mouseEvent, MBT_LEFT, false, 1);
                            }
                            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                                browser->GetHost()->SendMouseClickEvent(mouseEvent, MBT_LEFT, true, 1);
                            }

                            // 滚轮事件
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

        // 关闭标记为关闭的标签页
        for (auto tabId : tabsToClose) {
            CloseBrowserTab(tabId);
            g_tabs.erase(tabId);
        }

        ImGui::Render();
        ImDrawData *draw_data = ImGui::GetDrawData();
        const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
        if (!is_minimized) {
            m_VulkanBackend->RenderFrame(draw_data);
            m_VulkanBackend->PresentFrame();
        }

        // Update and Render additional Platform Windows
        if (io->ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

    }

    void ImguiSystem::shutdown() {
        CFW_LOG_NOTICE("DisplaySystem: Shutting down...");

        // Signal thread to stop if running
        running = false;

        // Vulkan backend and SDL cleanup are performed in thread_loop when the
        // system thread exits. Here just close browser tabs and clear state.
        for (auto &[tabId, tab] : g_tabs) {
            CloseBrowserTab(tabId);
        }
        g_tabs.clear();
    }


}  // namespace Corona::Systems

