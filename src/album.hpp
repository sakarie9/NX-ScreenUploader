#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <vector>

[[nodiscard]] std::expected<std::string, std::string> getLastAlbumItem();
[[nodiscard]] std::expected<std::vector<std::string>, std::string>
getNewAlbumItems(std::string_view lastItem);
