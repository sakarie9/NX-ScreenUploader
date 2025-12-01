#include "utils.hpp"

#include <sys/stat.h>

#include <algorithm>
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

// Helper to find maximum path in directory matching a predicate using ranges
template <size_t ExpectedLen>
[[nodiscard]] fs::path findMaxPath(const fs::path& dir) {
    std::vector<fs::path> paths;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (isValidDigitPath<ExpectedLen>(entry)) {
            paths.push_back(entry.path());
        }
    }

    if (paths.empty()) return {};
    return *std::ranges::max_element(paths);
}

// Helper to find maximum file path in directory using ranges
[[nodiscard]] fs::path findMaxFile(const fs::path& dir) {
    std::vector<fs::path> paths;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            paths.push_back(entry.path());
        }
    }

    if (paths.empty()) return {};
    return *std::ranges::max_element(paths);
}
}  // namespace

std::expected<std::string, std::string> getLastAlbumItem() {
    constexpr std::string_view albumPath = ALBUM_PATH;
    if (!fs::is_directory(albumPath))
        return std::unexpected("No album directory: img:/");

    // Find the largest year directory
    const fs::path maxYear = findMaxPath<4>(albumPath);
    if (maxYear.empty()) return std::unexpected("No years in img:/");

    // Find the largest month directory
    const fs::path maxMonth = findMaxPath<2>(maxYear);
    if (maxMonth.empty()) return std::unexpected("No months in year");

    // Find the largest day directory
    const fs::path maxDay = findMaxPath<2>(maxMonth);
    if (maxDay.empty()) return std::unexpected("No days in month");

    // Find the latest file (lexicographically largest)
    const fs::path maxFile = findMaxFile(maxDay);
    if (maxFile.empty()) return std::unexpected("No screenshots in day");

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

    // constexpr-friendly character check
    constexpr auto isUnreserved = [](unsigned char c) constexpr noexcept {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
               (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
               c == '~';
    };

    for (const unsigned char c : value) {
        if (isUnreserved(c)) {
            result.push_back(static_cast<char>(c));
        } else {
            result.push_back('%');
            result.push_back(hexChars[c >> 4]);
            result.push_back(hexChars[c & 0x0F]);
        }
    }

    return result;
}
