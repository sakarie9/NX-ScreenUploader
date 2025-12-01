#pragma once

#include <string_view>

// Send file to Telegram with optional compression
[[nodiscard]] bool sendFileToTelegram(std::string_view path, size_t size,
                                      bool compression);

// Send file to ntfy.sh (always original, no compression)
[[nodiscard]] bool sendFileToNtfy(std::string_view path, size_t size);

// Legacy alias for backward compatibility
[[nodiscard]] bool sendFileToServer(std::string_view path, size_t size,
                                    bool compression);
