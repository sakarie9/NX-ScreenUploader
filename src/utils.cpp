#include "utils.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <array>
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
    constexpr std::string_view albumPath = ALBUM_PATH;
    if (!fs::is_directory(albumPath)) return "<No album directory: img:/>";

    // 找最大的年份目录
    fs::path maxYear;
    for (const auto& entry : fs::directory_iterator(albumPath)) {
        if (isValidDigitPath<4>(entry)) {
            const auto& path = entry.path();
            if (maxYear.empty() || path > maxYear) {
                maxYear = path;
            }
        }
    }
    if (maxYear.empty()) return "<No years in img:/>";

    // 找最大的月份目录
    fs::path maxMonth;
    for (const auto& entry : fs::directory_iterator(maxYear)) {
        if (isValidDigitPath<2>(entry)) {
            const auto& path = entry.path();
            if (maxMonth.empty() || path > maxMonth) {
                maxMonth = path;
            }
        }
    }
    if (maxMonth.empty()) return "<No months in year>";

    // 找最大的日期目录
    fs::path maxDay;
    for (const auto& entry : fs::directory_iterator(maxMonth)) {
        if (isValidDigitPath<2>(entry)) {
            const auto& path = entry.path();
            if (maxDay.empty() || path > maxDay) {
                maxDay = path;
            }
        }
    }
    if (maxDay.empty()) return "<No days in month>";

    // 找最大的文件
    fs::path maxFile;
    for (const auto& entry : fs::directory_iterator(maxDay)) {
        if (entry.is_regular_file()) {
            const auto& path = entry.path();
            if (maxFile.empty() || path > maxFile) {
                maxFile = path;
            }
        }
    }
    if (maxFile.empty()) return "<No screenshots in day>";

    return maxFile.string();
}

size_t filesize(std::string_view path) {
    struct stat st;
    if (stat(path.data(), &st) != 0) return 0;
    return static_cast<size_t>(st.st_size);
}

std::string url_encode(std::string_view value) {
    std::string result;
    // More conservative reservation - typically most chars don't need encoding
    result.reserve(value.size() + (value.size() / 4));

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
