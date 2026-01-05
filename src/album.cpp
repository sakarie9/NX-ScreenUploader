#include "album.hpp"

#include <algorithm>
#include <filesystem>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#ifdef ENABLE_TIME_FUNCTIONS
#include <chrono>
#endif

#include "logger.hpp"

namespace fs = std::filesystem;

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

// Collect all files in a directory that are newer than lastPath
void collectFilesNewerThan(const fs::path& dir, std::string_view lastPath,
                           std::vector<std::string>& result) noexcept {
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;

        const auto& p = entry.path();
        auto fullPath = p.string();

        // Compare full path strings - files are sorted by path
        if (fullPath > lastPath) {
            result.push_back(std::move(fullPath));
        }
    }
}

// Find all directories in dir with expected length that are >= minDirName
template <size_t ExpectedLen>
void findDirsGreaterOrEqual(const fs::path& dir, std::string_view minDirName,
                            std::vector<fs::path>& result) noexcept {
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_directory()) continue;

        const auto& p = entry.path();
        auto filename = p.filename().string();

        if (filename.length() != ExpectedLen || !isDigitsOnly(filename))
            continue;

        if (filename >= minDirName) {
            result.push_back(p);
        }
    }
}

// Collect all files from a day directory that are newer than lastItem
void collectFromDayDir(const fs::path& dayDir, std::string_view lastItem,
                       std::vector<std::string>& result) noexcept {
    collectFilesNewerThan(dayDir, lastItem, result);
}

// Collect all files from a month directory
// If isMinMonth is true, only collect from days >= minDay
void collectFromMonthDir(const fs::path& monthDir, std::string_view lastItem,
                         std::string_view minDay, bool isMinMonth,
                         std::vector<std::string>& result) noexcept {
    std::vector<fs::path> days;
    findDirsGreaterOrEqual<2>(monthDir, isMinMonth ? minDay : "", days);

    for (const auto& dayDir : days) {
        auto dayName = dayDir.filename().string();
        if (isMinMonth && dayName == minDay) {
            // This is the minimum day, only collect files newer than lastItem
            collectFromDayDir(dayDir, lastItem, result);
        } else {
            // This is a newer day, collect all files
            collectFilesNewerThan(dayDir, "", result);
        }
    }
}

// Collect all files from a year directory
// If isMinYear is true, only collect from months >= minMonth
void collectFromYearDir(const fs::path& yearDir, std::string_view lastItem,
                        std::string_view minMonth, std::string_view minDay,
                        bool isMinYear,
                        std::vector<std::string>& result) noexcept {
    std::vector<fs::path> months;
    findDirsGreaterOrEqual<2>(yearDir, isMinYear ? minMonth : "", months);

    for (const auto& monthDir : months) {
        auto monthName = monthDir.filename().string();
        if (isMinYear && monthName == minMonth) {
            // This is the minimum month
            collectFromMonthDir(monthDir, lastItem, minDay, true, result);
        } else {
            // This is a newer month, collect all files
            collectFromMonthDir(monthDir, lastItem, "", false, result);
        }
    }
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
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime);
    Logger::get().debug() << "[getLastAlbumItem] Success " << " ("
                          << duration.count() << "ms)" << endl;
    Logger::get().close();
#endif

    return file.string();
}

std::expected<std::vector<std::string>, std::string> getNewAlbumItems(
    std::string_view lastItem) {
#ifdef ENABLE_TIME_FUNCTIONS
    const auto startTime = std::chrono::high_resolution_clock::now();
#endif

    std::vector<std::string> newItems;

    // If lastItem is empty, just get the latest item
    if (lastItem.empty()) {
        auto result = getLastAlbumItem();
        if (result.has_value()) {
            newItems.push_back(std::move(result.value()));
        }
        return newItems;
    }

    // Parse lastItem path to extract year/month/day components
    // Format: img:/YYYY/MM/DD/filename
    // We need to find files newer than lastItem

    // Extract components from lastItem path
    // Expected format: img:/2024/01/15/filename.jpg
    std::string lastItemStr(lastItem);

    // Find year/month/day from the path
    size_t pos = 0;
    if (lastItemStr.starts_with("img:/")) {
        pos = 5;  // Skip "img:/"
    }

    // Extract year (4 digits)
    if (pos + 4 > lastItemStr.length())
        return std::unexpected("Invalid path format");
    std::string_view minYear = std::string_view(lastItemStr).substr(pos, 4);
    pos += 5;  // Skip "YYYY/"

    // Extract month (2 digits)
    if (pos + 2 > lastItemStr.length())
        return std::unexpected("Invalid path format");
    std::string_view minMonth = std::string_view(lastItemStr).substr(pos, 2);
    pos += 3;  // Skip "MM/"

    // Extract day (2 digits)
    if (pos + 2 > lastItemStr.length())
        return std::unexpected("Invalid path format");
    std::string_view minDay = std::string_view(lastItemStr).substr(pos, 2);

    // Collect all files newer than lastItem
    std::vector<fs::path> years;
    findDirsGreaterOrEqual<4>(ALBUM_PATH, minYear, years);

    for (const auto& yearDir : years) {
        auto yearName = yearDir.filename().string();
        if (yearName == minYear) {
            // This is the minimum year
            collectFromYearDir(yearDir, lastItem, minMonth, minDay, true,
                               newItems);
        } else {
            // This is a newer year, collect all files
            collectFromYearDir(yearDir, lastItem, "", "", false, newItems);
        }
    }

    // Sort results to ensure chronological order
    std::ranges::sort(newItems);

#ifdef ENABLE_TIME_FUNCTIONS
    const auto endTime = std::chrono::high_resolution_clock::now();
    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime);
    Logger::get().debug() << "[getNewAlbumItems] Found " << newItems.size()
                          << " new items (" << duration.count() << "ms)"
                          << endl;
    Logger::get().close();
#endif

    return newItems;
}
