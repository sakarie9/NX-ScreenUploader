#include "utils.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <ranges>
#include <string>
#include <vector>

namespace {
constexpr std::string_view ALBUM_PATH = "img:/";

constexpr bool isDigitsOnly(std::string_view str) noexcept {
    return std::ranges::all_of(str,
                               [](char c) { return c >= '0' && c <= '9'; });
}

template <size_t ExpectedLen>
[[nodiscard]] bool isValidDigitPath(const fs::directory_entry& entry) noexcept {
    if (!entry.is_directory()) return false;
    const auto filename = entry.path().filename().string();
    return filename.length() == ExpectedLen && isDigitsOnly(filename);
}
}  // namespace

std::string getLastAlbumItem() {
    std::vector<std::string> years, months, days, files;

    const std::string albumPath{ALBUM_PATH};
    if (!fs::is_directory(albumPath))
        return "<No album directory: " + albumPath + ">";

    years.reserve(8);
    for (const auto& entry : fs::directory_iterator(albumPath)) {
        if (isValidDigitPath<4>(entry))
            years.emplace_back(entry.path().string());
    }
    if (years.empty()) return "<No years in " + albumPath + ">";
    std::ranges::sort(years);

    months.reserve(12);
    for (const auto& entry : fs::directory_iterator(years.back())) {
        if (isValidDigitPath<2>(entry))
            months.emplace_back(entry.path().string());
    }
    if (months.empty()) return "<No months in " + years.back() + ">";
    std::ranges::sort(months);

    days.reserve(31);
    for (const auto& entry : fs::directory_iterator(months.back())) {
        if (isValidDigitPath<2>(entry))
            days.emplace_back(entry.path().string());
    }
    if (days.empty()) return "<No days in " + months.back() + ">";
    std::ranges::sort(days);

    files.reserve(32);
    for (const auto& entry : fs::directory_iterator(days.back())) {
        if (entry.is_regular_file()) files.emplace_back(entry.path().string());
    }
    if (files.empty()) return "<No screenshots in " + days.back() + ">";
    std::ranges::sort(files);

    return files.back();
}

size_t filesize(std::string_view path) {
    std::ifstream f(path.data(), std::ios::binary | std::ios::ate);
    if (!f) return 0;
    return static_cast<size_t>(f.tellg());
}

std::string url_encode(std::string_view value) {
    std::string result;
    result.reserve(value.size() * 3);  // 最坏情况预留

    constexpr std::string_view hexChars = "0123456789ABCDEF";

    for (const unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            result.push_back(c);
        } else {
            result.push_back('%');
            result.push_back(hexChars[c >> 4]);
            result.push_back(hexChars[c & 0x0F]);
        }
    }

    return result;
}
