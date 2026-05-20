#pragma once

#include <curl/curl.h>

#include <cstddef>
#include <cstdio>
#include <string_view>

// Buffer sizes for CURL transfers
constexpr size_t NX_CURL_BUFFERSIZE = 0x2000L;         // 8KB
constexpr size_t NX_CURL_UPLOAD_BUFFERSIZE = 0x2000L;  // 8KB

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

// Shared upload types

/// State for streaming file upload via CURL read callback
struct UploadInfo {
    FILE* f;
    size_t sizeLeft;
};

/// Result of file upload validation
enum class ValidationResult {
    Success,  // Valid and should upload
    Skip,     // Valid but skip due to config
    Error     // Invalid file
};

// Shared upload utility functions

/// CURL read callback for streaming file content
size_t uploadReadFunction(void* ptr, size_t size, size_t nmemb,
                          void* data) noexcept;

/// Configure CURL timeouts based on file type (image vs video)
void setCurlTimeouts(CURL* curl, bool isVideo);

/// Common validation logic for file uploads
ValidationResult validateUploadFile(std::string_view path,
                                    std::string_view logPrefix,
                                    std::string_view& tid, bool& isVideo,
                                    bool uploadScreenshots, bool uploadVideos);

// File type detection utilities

/// Check if file is a video based on extension
inline bool isVideoFile(std::string_view path) {
    return (path.size() >= 4 && path.substr(path.size() - 4) == ".mp4");
}

/// Get max retries based on boolean flag
inline int getMaxRetries(bool isVideo) {
    if (isVideo) {
        return VideoTimeouts::maxRetries;
    }
    return ImageTimeouts::maxRetries;
}

/// Get max retries based on file path
inline int getMaxRetriesFromPath(std::string_view path) {
    if (isVideoFile(path)) {
        return VideoTimeouts::maxRetries;
    }
    return ImageTimeouts::maxRetries;
}
