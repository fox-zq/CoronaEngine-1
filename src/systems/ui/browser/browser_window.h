// browser_window.h
#pragma once

#include "browser_types.h"

// 函数声明
VkDescriptorSet create_browser_texture(int width, int height);
std::string convert_local_path_to_url(const std::string& local_path);
std::string resolve_html_path_for_cef(const std::string& maybe_relative_path);
void update_browser_texture(int tab_id);
void close_browser_tab(int tab_id);
