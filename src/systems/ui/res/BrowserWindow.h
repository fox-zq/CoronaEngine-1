// BrowserWindow.h
#pragma once

#include "browser_types.h"

// 函数声明
VkDescriptorSet CreateBrowserTexture(int width, int height);
std::string ConvertLocalPathToUrl(const std::string& localPath);
std::string ResolveHtmlPathForCef(const std::string& maybeRelativePath);
extern "C" int CreateBrowserTab(const std::string& url, const std::string& path = "");
void UpdateBrowserTexture(int tabId);
void CloseBrowserTab(int tabId);