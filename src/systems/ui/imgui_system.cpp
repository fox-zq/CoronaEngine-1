#include <SDL3/SDL_vulkan.h>
#include <corona/kernel/core/i_logger.h>
#include <corona/kernel/core/kernel_context.h>
#include <corona/systems/ui/imgui_system.h>
#include <imgui_internal.h>
#include <nanobind/nanobind.h>

#include <cstdarg>
#include <filesystem>

#include "cef/browser_types.h"
#include "cef/browser_manager.h"
#include "cef/browser_renderer.h"
#include "cef/cef_client.h"
#include "sdl/sdl_key_utils.h"
#include "sdl/sdl_mouse_utils.h"
#include "imgui/ui_layout_manager.h"

namespace Corona::Systems::UI {
CefMessageRouterConfig message_router_config;
}

namespace Corona::Systems {

bool ImguiSystem::initialize(Kernel::ISystemContext* ctx) {
    CFW_LOG_NOTICE("ImguiSystem: Initializing...");

    // 设置 CEF 消息路由函数名称
    UI::message_router_config.js_query_function = "cefQuery";
    UI::message_router_config.js_cancel_function = "cefQueryCancel";

    // 初始化 CEF
    CefMainArgs main_args(GetModuleHandle(nullptr));
    CefRefPtr<UI::SimpleApp> app(new UI::SimpleApp());
    int exitCode = CefExecuteProcess(main_args, app.get(), nullptr);
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

    if (!CefInitialize(main_args, settings, app.get(), nullptr)) {
        CFW_LOG_ERROR("Failed to initialize CEF.");
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
        CFW_LOG_ERROR("Failed to initialize SDL: {}", SDL_GetError());
        CefShutdown();
        running_ = false;
        return;
    }

    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
    window_ = SDL_CreateWindow("Corona Engine (Vulkan)", 1920, 1080,
                               SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (window_ == nullptr) {
        CFW_LOG_ERROR("Failed to create window: {}", SDL_GetError());
        SDL_Quit();
        CefShutdown();
        running_ = false;
        return;
    }

    SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window_);
    SDL_StartTextInput(window_);
    SDL_SetHint(SDL_HINT_IME_IMPLEMENTED_UI, "1");

    vulkan_backend_ = std::make_unique<VulkanBackend>(window_);
    vulkan_backend_->initialize();

    UI::BrowserManager::instance().set_vulkan_backend(vulkan_backend_.get());

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
    style.WindowRounding = 1.0f;
    style.WindowBorderSize = 1.0f;
    style.WindowPadding = ImVec2(1.0f, 1.0f);

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
        UI::BrowserManager::instance().set_vulkan_backend(nullptr);
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
    static UI::UILayoutManager layout_manager;
    static UI::BrowserRenderer browser_renderer;

    pending_key_events_.clear();

    // 处理 SDL 事件（使用原有逻辑，暂时保留在这里）
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
            case SDL_EVENT_QUIT:
                running_ = false;
                if (should_process_in_imgui) {
                    ImGui_ImplSDL3_ProcessEvent(&event_);
                }
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                if (event_.window.windowID == SDL_GetWindowID(window_)) {
                    window_size_changed_ = true;
                    vulkan_backend_->set_swap_chain_rebuild(true);
                    if (should_process_in_imgui) {
                        ImGui_ImplSDL3_ProcessEvent(&event_);
                    }
                }
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                if (should_process_in_imgui) {
                    ImGui_ImplSDL3_ProcessEvent(&event_);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            case SDL_EVENT_MOUSE_MOTION:
            case SDL_EVENT_MOUSE_WHEEL:
                ImGui_ImplSDL3_ProcessEvent(&event_);
                break;
            default:
                if (should_process_in_imgui) {
                    ImGui_ImplSDL3_ProcessEvent(&event_);
                }
                break;
        }
    }

    // 处理交换链重建
    if (vulkan_backend_->is_swap_chain_rebuild()) {
        int width, height;
        SDL_GetWindowSize(window_, &width, &height);
        vulkan_backend_->rebuild_swap_chain(width, height);
    }

    // 开始新的 ImGui 帧
    vulkan_backend_->new_frame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    // 设置 DockSpace
    ImGuiID dock_space_id = layout_manager.setup_dockspace();

    // 渲染浏览器标签页
    std::vector<int> tabs_to_close = browser_renderer.render_browser_tabs(
        dock_space_id, active_tab_id_, url_input_active_tab, io_);

    // 结束 DockSpace
    layout_manager.end_dockspace();

    // 发送键盘事件给浏览器
    if (active_tab_id_ != -1 && url_input_active_tab == -1 && !pending_key_events_.empty()) {
        send_key_events_to_browser(active_tab_id_);
    }

    // 关闭标签页
    for (auto tab_id : tabs_to_close) {
        UI::BrowserManager::instance().remove_tab(tab_id);
        if (tab_id == active_tab_id_) {
            active_tab_id_ = -1;
        }
        if (tab_id == url_input_active_tab) {
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

    auto& tabs = UI::BrowserManager::instance().get_tabs();
    std::vector<int> ids;
    for (const auto& [id, tab] : tabs) {
        ids.push_back(id);
    }

    for (int id : ids) {
        UI::BrowserManager::instance().remove_tab(id);
    }

    pending_key_events_.clear();
}

// 发送键盘事件到浏览器
void ImguiSystem::send_key_events_to_browser(int tab_id) {
    auto* tab = UI::BrowserManager::instance().get_tab(tab_id);
    if (!tab || !tab->client || !tab->client->GetBrowser()) {
        return;
    }

    CefRefPtr<CefBrowser> browser = tab->client->GetBrowser();

    for (const auto& pending_event : pending_key_events_) {
        if (pending_event.type == PendingKeyEvent::kMKeyEvent) {
            CefKeyEvent cef_key_event;

            // 设置事件类型
            cef_key_event.type = pending_event.pressed ? KEYEVENT_RAWKEYDOWN : KEYEVENT_KEYUP;

            // 转换键码
            cef_key_event.windows_key_code = UI::KeyUtils::convert_sdl_key_code_to_windows(pending_event.key_code);
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
                        if (char* utf16_text = SDL_iconv_string("UTF-16LE", "UTF-8", text.c_str(), text.length() + 1)) {
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
