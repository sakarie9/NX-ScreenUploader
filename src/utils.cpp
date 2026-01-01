#include "utils.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#ifdef ENABLE_TIME_FUNCTIONS
#include <chrono>
#endif

#include "logger.hpp"

namespace {
constexpr std::string_view ALBUM_PATH = "img:/";

constexpr bool isDigitsOnly(std::string_view str) noexcept {
    return std::ranges::all_of(str,
                               [](char c) { return c >= '0' && c <= '9'; });
}

// Use template parameter to avoid lambda overhead, directly compare filename
// strings
template <size_t ExpectedLen>
[[nodiscard]] fs::path findMaxDir(const fs::path& dir) noexcept {
    fs::path max_path;
    std::string max_filename;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_directory()) continue;

        const auto& p = entry.path();
        auto filename = p.filename().string();

        if (filename.length() != ExpectedLen || !isDigitsOnly(filename))
            continue;

        if (filename > max_filename) {
            max_filename = std::move(filename);
            max_path = p;
        }
    }

    return max_path;
}

[[nodiscard]] fs::path findMaxFileInDir(const fs::path& dir) noexcept {
    fs::path max_path;
    std::string max_filename;

    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;

        const auto& p = entry.path();
        auto filename = p.filename().string();

        if (filename > max_filename) {
            max_filename = std::move(filename);
            max_path = p;
        }
    }

    return max_path;
}

}  // namespace

std::expected<std::string, std::string> getLastAlbumItem() {
#ifdef ENABLE_TIME_FUNCTIONS
    const auto startTime = std::chrono::high_resolution_clock::now();
#endif

    // 1. Find Year (Length 4, Directory)
    const fs::path year = findMaxDir<4>(ALBUM_PATH);
    if (year.empty())
        return std::unexpected("No valid year directories in img:/");

    // 2. Find Month (Length 2, Directory)
    const fs::path month = findMaxDir<2>(year);
    if (month.empty())
        return std::unexpected("No valid month directories in " +
                               year.string());

    // 3. Find Day (Length 2, Directory)
    const fs::path day = findMaxDir<2>(month);
    if (day.empty())
        return std::unexpected("No valid day directories in " + month.string());

    // 4. Find File (Regular File)
    const fs::path file = findMaxFileInDir(day);
    if (file.empty())
        return std::unexpected("No files found in " + day.string());

#ifdef ENABLE_TIME_FUNCTIONS
    const auto endTime = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
        endTime - startTime);
    Logger::get().debug() << "[getLastAlbumItem] Success " << " ("
                          << duration.count() << "ns)" << endl;
    Logger::get().close();
#endif

    return file.string();
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