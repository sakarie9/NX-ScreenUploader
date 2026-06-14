#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <vector>

/// Get the most recent item in the default album directory (img:/)
[[nodiscard]] std::expected<std::string, std::string> getLastAlbumItem();

/// Get all items newer than lastItem in the default album directory (img:/)
[[nodiscard]] std::expected<std::vector<std::string>, std::string>
getNewAlbumItems(std::string_view lastItem);

/// Get the most recent item in a custom base directory
[[nodiscard]] std::expected<std::string, std::string> getLastAlbumItem(
    std::string_view basePath);

/// Get all items newer than lastItem in a custom base directory
/// basePath must end with '/' (e.g. "img:/" or "img:/PNGs/")
[[nodiscard]] std::expected<std::vector<std::string>, std::string>
getNewAlbumItems(std::string_view lastItem, std::string_view basePath);
