#include "browser_manager.h"

#include <cstring>
#include <filesystem>
#include <iostream>
#include <ranges>

#include "cef_client.h"
#include "corona/kernel/core/i_logger.h"

namespace fs = std::filesystem;

namespace Corona::Systems::UI {

static std::string convert_local_path_to_url(const std::string& local_path) {
    if (fs::path(local_path).is_absolute()) {
        std::string url = "file://";
        for (char c : local_path) {
            if (c == '\\') {
                url += '/';
            } else if (c == ' ') {
                url += "%20";
            } else {
                url += c;
            }
        }
        return url;
    }
    return local_path;
}

BrowserManager& BrowserManager::instance() {
    static BrowserManager instance;
    return instance;
}

int BrowserManager::create_tab(const std::string& url, const std::string& path,
                               const std::string& docking_pos,
                               int dock_width, int dock_height,
                               bool dock_fixed) {
    auto tab = std::make_unique<BrowserTab>();

    int id = ++tab_counter_;

    // 设置 docking 属性
    tab->docking_pos = docking_pos;
    tab->dock_width = dock_width;
    tab->dock_height = dock_height;
    tab->dock_fixed = dock_fixed;
    tab->dock_initialized = false;

    // 如果有指定的dock大小，使用它，否则使用默认大小
    if (dock_width > 0 && dock_height > 0) {
        tab->width = dock_width;
        tab->height = dock_height;
    } else {
        tab->width = 1600;
        tab->height = 900;
    }

    tab->name = "Browser " + path;

    CFW_LOG_INFO("Loading Path: {}", path);
    // URL Processing (保持不变)
    std::string full_url = convert_local_path_to_url(url);
    if (!path.empty()) {
        std::string clean_fragment = path;
        if (clean_fragment.starts_with("#")) {
            clean_fragment = clean_fragment.substr(1);
        }
        size_t hash_pos = full_url.find('#');
        if (hash_pos != std::string::npos) {
            full_url = full_url.substr(0, hash_pos);
        }
        full_url += "#" + clean_fragment;
    }
    CFW_LOG_INFO("Loading URL: {}", full_url);

    tab->url = full_url;
    strncpy(tab->url_buffer, full_url.c_str(), sizeof(tab->url_buffer) - 1);

    tab->client = new OffscreenCefClient();
    tab->client->SetTab(tab.get());

    // Create browser texture (CabbageHardware)
    tab->texture_id = create_browser_texture(tab->width, tab->height);

    // Create Offscreen Browser
    CefWindowInfo window_info;
    window_info.SetAsWindowless(GetDesktopWindow());

    CefBrowserSettings browser_settings;
    browser_settings.windowless_frame_rate = 60;
    browser_settings.javascript = STATE_ENABLED;
    browser_settings.local_storage = STATE_ENABLED;
    browser_settings.webgl = STATE_ENABLED;

    CefBrowserHost::CreateBrowser(window_info, CefRefPtr<CefClient>(tab->client), full_url, browser_settings, nullptr, nullptr);

    tabs_[id] = std::move(tab);
    return id;
}

BrowserTab* BrowserManager::get_tab(int tab_id) {
    auto it = tabs_.find(tab_id);
    return it != tabs_.end() ? it->second.get() : nullptr;
}

void BrowserManager::remove_tab(int tab_id) {
    if (!tabs_.contains(tab_id)) return;

    BrowserTab* tab = tabs_[tab_id].get();
    if (tab->client && tab->client->GetBrowser()) {
        tab->client->GetBrowser()->GetHost()->CloseBrowser(true);
    }

    destroy_tab_texture(tab);

    tabs_.erase(tab_id);
}

const std::unordered_map<int, std::unique_ptr<BrowserTab>>& BrowserManager::get_tabs() const {
    return tabs_;
}

std::unordered_map<int, std::unique_ptr<BrowserTab>>& BrowserManager::get_tabs() {
    return tabs_;
}

void BrowserManager::update() {
    for (auto& [tab_id, tab] : tabs_) {
        if (!tab->open) {
            tabs_to_close_.push_back(tab_id);
            continue;
        }
        update_texture(tab_id);
        std::string window_id = tab->name + "##" + std::to_string(tab_id);
    }
}

void BrowserManager::close_all_tabs() {
    std::vector<int> ids;
    ids.reserve(tabs_.size());
    for (const auto& id : tabs_ | std::views::keys) {
        ids.push_back(id);
    }

    for (int id : ids) {
        remove_tab(id);
    }
}

}  // namespace Corona::Systems::UI