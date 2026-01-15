#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace Corona::Utils {

/**
 * @brief 将 UTF-8 编码的字符串转换为 std::filesystem::path
 *
 * 在 Windows 上，std::filesystem::path 从 std::string 构造时默认使用 ANSI 编码，
 * 这会导致包含非 ASCII 字符（如中文）的 UTF-8 字符串被错误解析。
 * 此函数确保 UTF-8 字符串在所有平台上都能正确转换为 path。
 *
 * @param utf8_str UTF-8 编码的路径字符串
 * @return std::filesystem::path 正确转换后的路径对象
 */
inline std::filesystem::path utf8_to_path(const std::string& utf8_str) {
#ifdef _WIN32
    // Windows: 先将 UTF-8 转换为 UTF-16 (wstring), 再构造 path
    if (utf8_str.empty()) {
        return {};
    }

    // 计算所需的宽字符缓冲区大小
    int size = MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(),
                                   static_cast<int>(utf8_str.size()), nullptr, 0);
    if (size <= 0) {
        // 转换失败，返回原始路径（可能会有问题，但至少不会崩溃）
        return std::filesystem::path(utf8_str);
    }

    // 分配缓冲区并执行转换
    std::wstring wstr(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(),
                        static_cast<int>(utf8_str.size()), wstr.data(), size);

    return std::filesystem::path(wstr);
#else
    // Linux/macOS: 默认使用 UTF-8，直接构造即可
    return std::filesystem::path(utf8_str);
#endif
}

/**
 * @brief 将 UTF-8 编码的 string_view 转换为 std::filesystem::path
 *
 * @param utf8_sv UTF-8 编码的路径字符串视图
 * @return std::filesystem::path 正确转换后的路径对象
 */
inline std::filesystem::path utf8_to_path(std::string_view utf8_sv) {
    return utf8_to_path(std::string(utf8_sv));
}

/**
 * @brief 将 std::filesystem::path 转换为 UTF-8 编码的字符串
 *
 * 在 Windows 上，path::string() 返回 ANSI 编码的字符串，
 * 此函数确保返回正确的 UTF-8 编码字符串，适用于日志输出等场景。
 *
 * @param path 要转换的路径对象
 * @return std::string UTF-8 编码的路径字符串
 */
inline std::string path_to_utf8(const std::filesystem::path& path) {
#ifdef _WIN32
    // Windows: 将 wstring (UTF-16) 转换为 UTF-8
    const std::wstring& wstr = path.native();
    if (wstr.empty()) {
        return {};
    }

    // 计算所需的 UTF-8 缓冲区大小
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(),
                                   static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        // 转换失败，返回原始 string（可能是 ANSI 编码）
        return path.string();
    }

    // 分配缓冲区并执行转换
    std::string utf8_str(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(),
                        static_cast<int>(wstr.size()), utf8_str.data(), size, nullptr, nullptr);

    return utf8_str;
#else
    // Linux/macOS: 默认使用 UTF-8
    return path.string();
#endif
}

}  // namespace Corona::Utils
