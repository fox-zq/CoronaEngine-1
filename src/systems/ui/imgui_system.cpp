#include <SDL3/SDL_vulkan.h>
#include <corona/events/display_system_events.h>
#include <corona/events/engine_events.h>
#include <corona/kernel/core/i_logger.h>
#include <corona/kernel/event/i_event_bus.h>
#include <corona/kernel/event/i_event_stream.h>
#include <corona/systems/ui/imgui_system.h>
#include <nanobind/nanobind.h>

#include <cstdarg>
#include <filesystem>
#include <iostream>

#include "res/BrowserWindow.h"
#include "res/browser_types.h"
#include "res/cef_client.h"

namespace nb = nanobind;

NB_MODULE(Imgui, m) {
    m.doc() = "CoronaEngine embedded Python module (nanobind)";

    m.def("create_browser_tab", [](nb::object py_url, nb::object py_path) -> int {
        try {
            if (!py_url.is_valid()) {
                return -1;
            }
            nb::str py_url_str = nb::str(py_url);
            std::string url = py_url_str.c_str();
            nb::str py_path_str = nb::str(py_path);
            std::string path = py_path_str.c_str();
            return CreateBrowserTab(url, path);
        } catch (const std::exception& e) {
            return -1;
        } }, nb::arg("url") = "", nb::arg("path") = "", nb::rv_policy::take_ownership);

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
                    frame->ExecuteJavaScript(js_code, "", 0);
                }
            }
            return nb::str("{\"success\": true}");
        } catch (const std::exception& e) {
            return nb::str("{\"success\": false \"}");
        } }, nb::arg("tab_id"), nb::arg("js_code"));
}

CefMessageRouterConfig g_messageRouterConfig;
extern "C" PyObject* PyInit_Imgui();

namespace Corona::Systems {

// 键码转换函数
static int ConvertSDLKeyCodeToWindows(int sdl_key) {
    if (sdl_key >= SDLK_A && sdl_key <= SDLK_Z) {
        return 0x41 + (sdl_key - SDLK_A);  // A-Z: 0x41-0x5A
    }

    // 数字键映射
    if (sdl_key >= SDLK_0 && sdl_key <= SDLK_9) {
        return 0x30 + (sdl_key - SDLK_0);  // 0-9: 0x30-0x39
    }

    switch (sdl_key) {
        // 符号键映射
        case SDLK_RETURN:
            return 0x0D;  // VK_RETURN
        case SDLK_GRAVE:
            return 0xC0;
        case SDLK_MINUS:
            return 0xBD;
        case SDLK_EQUALS:
            return 0xBB;
        case SDLK_LEFTBRACKET:
            return 0xDB;
        case SDLK_RIGHTBRACKET:
            return 0xDD;
        case SDLK_BACKSLASH:
            return 0xDC;
        case SDLK_SEMICOLON:
            return 0xBA;
        case SDLK_APOSTROPHE:
            return 0xDE;
        case SDLK_COMMA:
            return 0xBC;
        case SDLK_PERIOD:
            return 0xBE;
        case SDLK_SLASH:
            return 0xBF;

        // 导航键映射
        case SDLK_LEFT:
            return 0x25;  // VK_LEFT
        case SDLK_UP:
            return 0x26;  // VK_UP
        case SDLK_RIGHT:
            return 0x27;  // VK_RIGHT
        case SDLK_DOWN:
            return 0x28;  // VK_DOWN
        case SDLK_HOME:
            return 0x24;  // VK_HOME
        case SDLK_END:
            return 0x23;  // VK_END
        case SDLK_PAGEUP:
            return 0x21;  // VK_PRIOR
        case SDLK_PAGEDOWN:
            return 0x22;  // VK_NEXT
        case SDLK_INSERT:
            return 0x2D;  // VK_INSERT
        case SDLK_DELETE:
            return 0x2E;  // VK_DELETE
        case SDLK_BACKSPACE:
            return 0x08;  // VK_BACK

        // 小键盘键
        case SDLK_KP_0:
            return 0x60;
        case SDLK_KP_1:
            return 0x61;
        case SDLK_KP_2:
            return 0x62;
        case SDLK_KP_3:
            return 0x63;
        case SDLK_KP_4:
            return 0x64;
        case SDLK_KP_5:
            return 0x65;
        case SDLK_KP_6:
            return 0x66;
        case SDLK_KP_7:
            return 0x67;
        case SDLK_KP_8:
            return 0x68;
        case SDLK_KP_9:
            return 0x69;
        case SDLK_KP_MULTIPLY:
            return 0x6A;
        case SDLK_KP_PLUS:
            return 0x6B;
        case SDLK_KP_MINUS:
            return 0x6D;
        case SDLK_KP_DECIMAL:
            return 0x6E;
        case SDLK_KP_DIVIDE:
            return 0x6F;
        case SDLK_KP_ENTER:
            return 0x0D;

        // 功能键
        case SDLK_F1:
            return 0x70;
        case SDLK_F2:
            return 0x71;
        case SDLK_F3:
            return 0x72;
        case SDLK_F4:
            return 0x73;
        case SDLK_F5:
            return 0x74;
        case SDLK_F6:
            return 0x75;
        case SDLK_F7:
            return 0x76;
        case SDLK_F8:
            return 0x77;
        case SDLK_F9:
            return 0x78;
        case SDLK_F10:
            return 0x79;
        case SDLK_F11:
            return 0x7A;
        case SDLK_F12:
            return 0x7B;

        default:
            return sdl_key;
    }
}

// 判断是否是修饰键
static bool IsModifierKey(int key) {
    return key == SDLK_LCTRL || key == SDLK_RCTRL ||
           key == SDLK_LSHIFT || key == SDLK_RSHIFT ||
           key == SDLK_LALT || key == SDLK_RALT ||
           key == SDLK_LGUI || key == SDLK_RGUI;
}

// 判断是否应该发送CHAR事件
static bool ShouldSendCharEvent(int key, int modifiers) {
    // 修饰键不发送CHAR事件
    if (IsModifierKey(key)) {
        return false;
    }

    // 功能键不发送CHAR事件
    if ((key >= SDLK_F1 && key <= SDLK_F12)) {
        return false;
    }

    // 回车键需要发送CHAR事件以便浏览器处理换行
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        return true;
    }

    // Ctrl+字母组合键（用于快捷键）应发送CHAR事件
    if (modifiers & EVENTFLAG_CONTROL_DOWN) {
        if ((key >= SDLK_A && key <= SDLK_Z) ||
            (key >= SDLK_0 && key <= SDLK_9)) {
            return true;
        }
    }

    // 导航键不发送CHAR事件
    switch (key) {
        case SDLK_ESCAPE:
        case SDLK_TAB:
        case SDLK_CAPSLOCK:
        case SDLK_PRINTSCREEN:
        case SDLK_SCROLLLOCK:
        case SDLK_PAUSE:
        case SDLK_INSERT:
        case SDLK_HOME:
        case SDLK_PAGEUP:
        case SDLK_DELETE:
        case SDLK_END:
        case SDLK_PAGEDOWN:
        case SDLK_RIGHT:
        case SDLK_LEFT:
        case SDLK_DOWN:
        case SDLK_UP:
        case SDLK_NUMLOCKCLEAR:
        case SDLK_KP_CLEAR:
        case SDLK_BACKSPACE:
            return false;
    }

    // 如果Alt键按下，通常不发送CHAR事件（用于菜单快捷键）
    if (modifiers & EVENTFLAG_ALT_DOWN) {
        if (key >= SDLK_KP_0 && key <= SDLK_KP_9) {
            return true;
        }
        return false;
    }

    return true;
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

bool ImguiSystem::initialize(Kernel::ISystemContext* ctx) {
    CFW_LOG_NOTICE("ImguiSystem: Initializing...");

    g_messageRouterConfig.js_query_function = "cefQuery";
    g_messageRouterConfig.js_cancel_function = "cefQueryCancel";

    CefMainArgs mainArgs(GetModuleHandle(nullptr));
    CefRefPtr<SimpleApp> app(new SimpleApp());
    int exitCode = CefExecuteProcess(mainArgs, app.get(), nullptr);
    if (exitCode >= 0) {
        return exitCode;
    }

    CefSettings settings;
    settings.multi_threaded_message_loop = true;
    settings.windowless_rendering_enabled = true;
    settings.no_sandbox = true;
    settings.remote_debugging_port = 9222;
    settings.log_severity = LOGSEVERITY_INFO;
    settings.uncaught_exception_stack_size = 10;

    CefString(&settings.locale).FromASCII("zh-CN");
    std::filesystem::path cache_path = std::filesystem::current_path() / "cache";
    if (!std::filesystem::exists(cache_path)) {
        std::filesystem::create_directories(cache_path);
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
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << '\n';
        CefShutdown();
        running = false;
        return;
    }

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

    if (volkInitialize() != VK_SUCCESS) {
        std::cerr << "Failed to initialize Volk\n";
        running = false;
        return;
    }

    uint32_t extensions_count = 0;
    char const* const* extensions_names = SDL_Vulkan_GetInstanceExtensions(&extensions_count);
    std::vector<const char*> extensions;
    if (extensions_names) {
        for (uint32_t i = 0; i < extensions_count; i++) {
            extensions.push_back(extensions_names[i]);
        }
    }

    m_VulkanBackend = std::make_unique<VulkanBackend>(window);
    m_VulkanBackend->Initialize(extensions);
    g_vulkan_backend = m_VulkanBackend.get();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    io = &ImGui::GetIO();
    io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io->ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
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

    PyImport_AppendInittab("Imgui", &PyInit_Imgui);
    CreateBrowserTab("file:///E:/workspace/CoronaEngine/build/examples/engine/RelWithDebInfo/test.html");
    showDemoWindow = false;

    using Corona::Kernel::SystemState;
    while (running && get_state() == SystemState::running) {
        update();
    }

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

        if (should_process_in_imgui) {
            ImGui_ImplSDL3_ProcessEvent(&event);
        }

        switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                if (should_process_in_imgui) {
                    ImGui_ImplSDL3_ProcessEvent(&event);
                }
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                if (event.window.windowID == SDL_GetWindowID(window)) {
                    m_VulkanBackend->SetSwapChainRebuild(true);
                    if (should_process_in_imgui) {
                        ImGui_ImplSDL3_ProcessEvent(&event);
                    }
                }
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                if (should_process_in_imgui) {
                    ImGui_ImplSDL3_ProcessEvent(&event);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            case SDL_EVENT_MOUSE_MOTION:
            case SDL_EVENT_MOUSE_WHEEL:
                ImGui_ImplSDL3_ProcessEvent(&event);
                break;
            default:
                if (should_process_in_imgui) {
                    ImGui_ImplSDL3_ProcessEvent(&event);
                }
                break;
        }
    }

    if (m_VulkanBackend->IsSwapChainRebuild()) {
        int width, height;
        SDL_GetWindowSize(window, &width, &height);
        m_VulkanBackend->RebuildSwapChain(width, height);
    }

    m_VulkanBackend->NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    ImGuiWindowFlags dockSpaceFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    dockSpaceFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    dockSpaceFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("DockSpace", nullptr, dockSpaceFlags);
    ImGui::PopStyleVar(3);

    ImGuiID dockSpaceId = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockSpaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

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

    std::vector<int> tabsToClose;

    for (auto& [tabId, tab] : g_tabs) {
        if (!tab->open) {
            tabsToClose.push_back(tabId);
            continue;
        }

        UpdateBrowserTexture(tabId);

        ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
        std::string window_id = tab->name + "##" + std::to_string(tabId);

        if (ImGui::Begin(window_id.c_str(), &tab->open,
                         ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoNavInputs |
                             ImGuiWindowFlags_NoNavFocus)) {
            ImGui::PushItemWidth(-200);
            std::string url_input_id = "##url_" + std::to_string(tabId);

            if (ImGui::InputText(url_input_id.c_str(), tab->urlBuffer, sizeof(tab->urlBuffer),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                if (tab->client && tab->client->GetBrowser()) {
                    tab->client->GetBrowser()->GetMainFrame()->LoadURL(tab->urlBuffer);
                }
            }

            if (ImGui::IsItemActive()) {
                url_input_active_tab = tabId;
                m_ActiveTabId = -1;
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

            if (tab->textureId != VK_NULL_HANDLE) {
                ImGui::Image((ImTextureID)(intptr_t)tab->textureId, availSize);

                bool browser_hovered = ImGui::IsItemHovered();

                if (browser_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    m_ActiveTabId = tabId;
                    url_input_active_tab = -1;
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
                        mouseEvent.modifiers = 0;

                        browser->GetHost()->SendMouseMoveEvent(mouseEvent, false);

                        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                            browser->GetHost()->SendMouseClickEvent(mouseEvent, MBT_LEFT, false, 1);
                        }
                        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                            browser->GetHost()->SendMouseClickEvent(mouseEvent, MBT_LEFT, true, 1);
                        }

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

}  // namespace Corona::Systems