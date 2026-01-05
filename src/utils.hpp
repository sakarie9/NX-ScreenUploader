#pragma once

#include <string>
#include <string_view>

[[nodiscard]] size_t filesize(std::string_view path);
[[nodiscard]] std::string url_encode(std::string_view value);
