#include <corona/events/display_system_events.h>
#include <corona/events/engine_events.h>
#include <corona/kernel/core/i_logger.h>
#include <corona/kernel/event/i_event_bus.h>
#include <corona/kernel/event/i_event_stream.h>
#include <corona/systems/imgui/imgui_system.h>
#include <corona/systems/imgui/res/BrowserWindow.h>

CefMessageRouterConfig g_messageRouterConfig;

namespace Corona::Systems {

bool ImguiSystem::initialize(Kernel::ISystemContext* ctx) {
    CFW_LOG_NOTICE("ImguiSystem: Initializing...");

    g_messageRouterConfig.js_query_function = "cefQuery";         // JS端调用的函数名
    g_messageRouterConfig.js_cancel_function = "cefQueryCancel";  // JS端取消函数名

    // CEF 初始化
    CefMainArgs mainArgs(GetModuleHandle(nullptr));

    // 创建App实例
    CefRefPtr<SimpleApp> app(new SimpleApp());
    int exitCode = CefExecuteProcess(mainArgs, app.get(), nullptr);
    if (exitCode >= 0) {
        return exitCode;
    }

    CefSettings settings;
    settings.multi_threaded_message_loop = true;
    settings.windowless_rendering_enabled = true;  // 启用离屏渲染

    settings.no_sandbox = true;             // 禁用沙箱
    settings.remote_debugging_port = 9222;  // 启用远程调试

    // 启用日志
    settings.log_severity = LOGSEVERITY_INFO;     // 或LOGSEVERITY_WARNING减少输出
    settings.uncaught_exception_stack_size = 10;  // 堆栈跟踪大小

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
    CefString(&settings.user_agent).FromASCII("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36");
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

    // SDL 初始化
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << '\n';
        CefShutdown();
        return EXIT_FAILURE;
    }

    // 设置 OpenGL 属性
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    // 创建 SDL 窗口 (带 OpenGL 支持)
    window = SDL_CreateWindow("LearnVulkanExample", 1400, 900,
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);

    if (window == nullptr) {
        std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << '\n';
        SDL_Quit();
        CefShutdown();
        return EXIT_FAILURE;
    }

    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);

    // 创建 OpenGL 上下文
    glContext = SDL_GL_CreateContext(window);
    if (glContext == nullptr) {
        std::cerr << "SDL_GL_CreateContext Error: " << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        SDL_Quit();
        CefShutdown();
        return EXIT_FAILURE;
    }
    SDL_GL_MakeCurrent(window, glContext);
    SDL_GL_SetSwapInterval(1);  // 启用 VSync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;    // 启用 Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;  // 启用多视口

    ImGui::StyleColorsDark();

    // 当启用 viewports 时调整样式
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // 使用 OpenGL 后端初始化 ImGui
    ImGui_ImplSDL3_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 330");
    // 创建初始的浏览器标签页
    // CreateBrowserTab("file:///D:/CoronaEngine/editor/CabbageEditor/Frontend/dist/index.html");
    //CreateBrowserTab(ResolveHtmlPathForCef("/test.html"));
    // CreateBrowserTab("https://www.google.com");

    showDemoWindow = false;
    running = true;
    return true;
}

void ImguiSystem::update() {

    while (SDL_PollEvent(&event)) {
    ImGui_ImplSDL3_ProcessEvent(&event);
        if (event.type == SDL_EVENT_QUIT) {
            running = false;
        }
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // 创建 DockSpace
    ImGuiWindowFlags dockSpaceFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    dockSpaceFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
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
    std::vector<BrowserTab*> tabsToClose;
    for (auto* tab : g_tabs) {
        if (!tab->open) {
            tabsToClose.push_back(tab);
            continue;
        }

        // 更新纹理
        UpdateBrowserTexture(tab);

        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);

        if (ImGui::Begin(tab->name.c_str(), &tab->open, ImGuiWindowFlags_NoScrollbar)) {
            // URL 输入框和导航按钮
            ImGui::PushItemWidth(-200);
            if (ImGui::InputText("##url", tab->urlBuffer, sizeof(tab->urlBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
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

                // 重新创建 OpenGL 纹理
                if (tab->textureId != 0) {
                    glDeleteTextures(1, &tab->textureId);
                }
                tab->textureId = CreateOpenGLTexture(tab->width, tab->height);

                // 通知浏览器调整大小
                if (tab->client) {
                    tab->client->Resize(tab->width, tab->height);
                }
            }

            // 显示浏览器内容
            if (tab->textureId != 0) {
                ImGui::Image((ImTextureID)(intptr_t)tab->textureId, availSize);

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
                        float wheel = io.MouseWheel;
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
    for (auto* tab : tabsToClose) {
        g_tabs.erase(std::remove(g_tabs.begin(), g_tabs.end(), tab), g_tabs.end());
        CloseBrowserTab(tab);
    }

    ImGui::Render();

    int displayW, displayH;
    SDL_GetWindowSize(window, &displayW, &displayH);
    glViewport(0, 0, displayW, displayH);
    glClearColor(0.176f, 0.176f, 0.188f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // 更新和渲染额外的平台窗口
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        SDL_GLContext backupContext = SDL_GL_GetCurrentContext();
        SDL_Window* backupWindow = SDL_GL_GetCurrentWindow();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        SDL_GL_MakeCurrent(backupWindow, backupContext);
    }

    SDL_GL_SwapWindow(window);

}

void ImguiSystem::shutdown() {
    CFW_LOG_NOTICE("DisplaySystem: Shutting down...");


    for (auto* tab : g_tabs) {
        CloseBrowserTab(tab);
    }
    g_tabs.clear();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();


    CefShutdown();

}

}  // namespace Corona::Systems
