#pragma once

#include <string>
#include <string_view>

[[nodiscard]] size_t filesize(std::string_view path);
[[nodiscard]] std::string url_encode(std::string_view value);

// Get current time formatted as ISO 8601 string (e.g., "2026-05-09T12:00:00Z")
[[nodiscard]] std::string getCurrentTimeISO8601();

// Get current local time formatted as "2026-05-09 16:19:14"
[[nodiscard]] std::string getCurrentTimeLocal();

// Get current time formatted as custom string (e.g., "2026-05-09 12:00:00")
[[nodiscard]] std::string getCurrentTimeFormatted(
    const char* fmt = "%Y-%m-%d %H:%M:%S");
