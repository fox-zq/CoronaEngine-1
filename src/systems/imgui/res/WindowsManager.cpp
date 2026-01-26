#include <corona/systems/imgui/res/BrowserWindow.h>
#include <iostream>
#include <cstring>

// 全局变量定义
std::vector<BrowserTab*> g_tabs;
int g_tabCounter = 0;

namespace fs = std::filesystem;

// 创建 Vulkan 纹理（占位符）
VkDescriptorSet CreateBrowserTexture(int width, int height) {
    // TODO: Implement Vulkan texture creation
    // 1. Create VkImage
    // 2. Alloc Memory
    // 3. Create ImageView
    // 4. Create Sampler
    // 5. Use ImGui_ImplVulkan_AddTexture(sampler, imageView, layout) -> VkDescriptorSet
    return VK_NULL_HANDLE;
}

/*
// 创建 OpenGL 纹理
GLuint CreateOpenGLTexture(int width, int height) {
    GLuint textureId;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
    glBindTexture(GL_TEXTURE_2D, 0);
    return textureId;
}
*/

// 转换本地路径为URL
std::string ConvertLocalPathToUrl(const std::string& localPath) {
    if (localPath.find("http://") == 0 || localPath.find("https://") == 0) {
        return localPath;
    }

    // 检查是否是绝对路径
    if (fs::path(localPath).is_absolute()) {
        // 转换为file:/// URL
        std::string url = "localfile:///";
        for (char c : localPath) {
            if (c == '\\') {
                url += '/';
            }
            else if (c == ' ') {
                url += "%20";
            }
            else {
                url += c;
            }
        }
        return url;
    }

    return localPath;
}

// Resolve a relative HTML path (e.g. "/test.html" or "test.html") against the
// HTML_SOURCE_DIR provided by CMake.
// If the macro isn't defined, we fall back to the current working directory.
std::string ResolveHtmlPathForCef(const std::string& maybeRelativePath) {
    fs::path base;
#ifdef HTML_SOURCE_DIR
    base = fs::path(HTML_SOURCE_DIR);
#else
    base = fs::current_path();
#endif

    fs::path p = fs::path(maybeRelativePath);

    // Treat leading '/' or '\\' as "project-relative" in our app, not absolute.
    // (On Windows, "/foo" is relative to current drive; keeping it explicit avoids surprises.)
    if (!maybeRelativePath.empty() && (maybeRelativePath[0] == '/' || maybeRelativePath[0] == '\\')) {
        p = fs::path(maybeRelativePath.substr(1));
    }

    // If CEF is given a real file URL, keep it as-is.
    const auto lower = [](std::string s) {
        for (auto& ch : s) ch = (char)tolower((unsigned char)ch);
        return s;
    };
    std::string lp = lower(maybeRelativePath);
    if (lp.rfind("file://", 0) == 0 || lp.rfind("http://", 0) == 0 || lp.rfind("https://", 0) == 0) {
        return maybeRelativePath;
    }

    fs::path absPath;
    if (p.is_absolute()) {
        absPath = p;
    } else {
        absPath = base / p;
    }

    // weakly_canonical handles paths that might not exist yet, or normalization
    // Note: weakly_canonical requires C++17
    absPath = fs::weakly_canonical(absPath);

    // Convert to a file:// URL that CEF understands.
    return std::string("file:///") + absPath.generic_string();
}

// 创建新的浏览器标签页
BrowserTab* CreateBrowserTab(const std::string& url) {
    BrowserTab* tab = new BrowserTab();
    tab->name = "Browser " + std::to_string(++g_tabCounter);

    // 转换本地路径为URL
    std::string fullUrl = ConvertLocalPathToUrl(url);
    std::cout << "Loading URL: " << fullUrl << std::endl;
    tab->url = fullUrl;
    strncpy(tab->urlBuffer, fullUrl.c_str(), sizeof(tab->urlBuffer) - 1);

    tab->client = new OffscreenCefClient();
    tab->client->SetTab(tab);

    // 创建 OpenGL 纹理
    tab->textureId = CreateBrowserTexture(tab->width, tab->height);

    // 创建离屏浏览器
    CefWindowInfo windowInfo;
    windowInfo.SetAsWindowless(GetDesktopWindow());

    CefBrowserSettings browserSettings;
    browserSettings.windowless_frame_rate = 60;
    // 启用JavaScript
    browserSettings.javascript = STATE_ENABLED;
    // 启用本地存储
    browserSettings.local_storage = STATE_ENABLED;
    // 启用WebGL
    browserSettings.webgl = STATE_ENABLED;

    CefBrowserHost::CreateBrowser(windowInfo, tab->client, fullUrl, browserSettings, nullptr, nullptr);

    g_tabs.push_back(tab);
    return tab;
}

// 更新浏览器纹理
void UpdateBrowserTexture(BrowserTab* tab) {
    if (tab->bufferDirty && !tab->pixelBuffer.empty() && tab->textureId != VK_NULL_HANDLE) {
        // TODO: Implement Vulkan texture upload
        // 1. Map memory or use staging buffer
        // 2. Copy pixelBuffer to VkImage
        // 3. Transition image layout
        // For Offscreen CEF, we receive BGRA buffer. We need to upload it.
        tab->bufferDirty = false;
    }
}

// 关闭浏览器标签页
void CloseBrowserTab(BrowserTab* tab) {
    if (tab->client && tab->client->GetBrowser()) {
        tab->client->GetBrowser()->GetHost()->CloseBrowser(true);
    }
    if (tab->textureId != VK_NULL_HANDLE) {
        // TODO: ImGui_ImplVulkan_RemoveTexture(tab->textureId);
        // Also destroy underlying VkImage/Memory/ImageView if we own them.
        tab->textureId = VK_NULL_HANDLE;
    }
    delete tab;
}
