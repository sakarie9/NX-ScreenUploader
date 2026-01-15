#pragma once

#include <string_view>

// Timeout configurations for images (screenshots)
struct ImageTimeouts {
    static constexpr long connectTimeout = 10L;  // 10 seconds
    static constexpr long idleTimeout = 30L;     // 30 seconds
    static constexpr long totalTimeout = 60L;    // 60 seconds
    static constexpr int maxRetries = 2;
};

// Timeout configurations for videos
struct VideoTimeouts {
    static constexpr long connectTimeout = 15L;  // 15 seconds
    static constexpr long idleTimeout = 60L;     // 60 seconds
    static constexpr long totalTimeout = 300L;   // 5 minutes
    static constexpr int maxRetries = 3;
};

// Check if file is a video based on extension
inline bool isVideoFile(std::string_view path) {
    return (path.size() >= 4 && path.substr(path.size() - 4) == ".mp4");
}

// Get max retries based on boolean flag
inline int getMaxRetries(bool isVideo) {
    if (isVideo) {
        return VideoTimeouts::maxRetries;
    }
    return ImageTimeouts::maxRetries;
}

// Get max retries based on file type (check if path ends with .mp4)
inline int getMaxRetriesFromPath(std::string_view path) {
    if (isVideoFile(path)) {
        return VideoTimeouts::maxRetries;
    }
    return ImageTimeouts::maxRetries;
}

// Send file to Telegram with optional compression
[[nodiscard]] bool sendFileToTelegram(std::string_view path, size_t size,
                                      bool compression);

// Send file to ntfy.sh (always original, no compression)
[[nodiscard]] bool sendFileToNtfy(std::string_view path, size_t size);

// Send file to Discord (always original, no compression)
[[nodiscard]] bool sendFileToDiscord(std::string_view path, size_t size);
