#pragma once

#include <curl/curl.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

// Buffer sizes for CURL transfers
constexpr size_t NX_CURL_BUFFERSIZE = 0x2000L;         // 8KB
constexpr size_t NX_CURL_UPLOAD_BUFFERSIZE = 0x2000L;  // 8KB

// Default upload tuning. These values are used as-is unless config.ini
// overrides them, which is why they live here and not in the config module.
namespace UploadDefaults {
// Timeouts in seconds, applied per upload attempt
inline constexpr long IMAGE_CONNECT_TIMEOUT_S = 10;
inline constexpr long IMAGE_IDLE_TIMEOUT_S = 30;
inline constexpr long IMAGE_TOTAL_TIMEOUT_S = 60;
inline constexpr long VIDEO_CONNECT_TIMEOUT_S = 15;
inline constexpr long VIDEO_IDLE_TIMEOUT_S = 60;
inline constexpr long VIDEO_TOTAL_TIMEOUT_S = 300;

// Abort a transfer when it stays below this many bytes per second for
// IDLE_TIMEOUT_S seconds
inline constexpr long LOW_SPEED_LIMIT_BPS = 1;

// Total attempts per channel, not retries after the first one
inline constexpr int IMAGE_MAX_ATTEMPTS = 2;
inline constexpr int VIDEO_MAX_ATTEMPTS = 3;

// Retry backoff in milliseconds
inline constexpr long RETRY_BASE_DELAY_MS = 1000;
inline constexpr long RETRY_MAX_DELAY_MS = 15000;
inline constexpr long RETRY_AFTER_MAX_MS = 60000;

// Total time budget in seconds for a single file, counted over all channels
// and attempts. 0 disables the limit.
inline constexpr long IMAGE_ITEM_BUDGET_S = 0;
inline constexpr long VIDEO_ITEM_BUDGET_S = 0;
}  // namespace UploadDefaults

/// Runtime upload tuning, resolved from config.ini at startup
struct UploadPolicy {
    long connectTimeout;  // seconds
    long idleTimeout;     // seconds
    long totalTimeout;    // seconds, per attempt
    long lowSpeedLimit;   // bytes per second
    int maxAttempts;      // total attempts per channel
    long retryBaseDelayMs;
    long retryMaxDelayMs;
    long retryAfterMaxMs;  // upper bound for a server Retry-After hint
    long itemBudgetS;      // total budget per file, 0 = unlimited
};

/// Build the default policy for images or videos
inline constexpr UploadPolicy makeUploadPolicy(bool isVideo) noexcept {
    return UploadPolicy{
        isVideo ? UploadDefaults::VIDEO_CONNECT_TIMEOUT_S
                : UploadDefaults::IMAGE_CONNECT_TIMEOUT_S,
        isVideo ? UploadDefaults::VIDEO_IDLE_TIMEOUT_S
                : UploadDefaults::IMAGE_IDLE_TIMEOUT_S,
        isVideo ? UploadDefaults::VIDEO_TOTAL_TIMEOUT_S
                : UploadDefaults::IMAGE_TOTAL_TIMEOUT_S,
        UploadDefaults::LOW_SPEED_LIMIT_BPS,
        isVideo ? UploadDefaults::VIDEO_MAX_ATTEMPTS
                : UploadDefaults::IMAGE_MAX_ATTEMPTS,
        UploadDefaults::RETRY_BASE_DELAY_MS,
        UploadDefaults::RETRY_MAX_DELAY_MS,
        UploadDefaults::RETRY_AFTER_MAX_MS,
        isVideo ? UploadDefaults::VIDEO_ITEM_BUDGET_S
                : UploadDefaults::IMAGE_ITEM_BUDGET_S,
    };
}

// Upload result classification

/// How an upload attempt ended. Used by the retry loop to decide whether
/// another attempt can possibly succeed.
enum class UploadStatus : uint8_t {
    Success,    // Uploaded successfully
    Skipped,    // Intentionally not uploaded (config), counts as success
    Transient,  // Failed, but retrying may succeed
    Permanent,  // Failed in a way that retrying cannot fix
};

/// Result of a single upload attempt
struct UploadOutcome {
    UploadStatus status{UploadStatus::Transient};
    long httpCode{0};
    long retryAfterSec{0};  // Retry-After hint from the server, 0 if absent

    [[nodiscard]] constexpr bool ok() const noexcept {
        return status == UploadStatus::Success ||
               status == UploadStatus::Skipped;
    }
};

/// Classify a CURL error code as retryable or not
UploadStatus classifyCurl(CURLcode res) noexcept;

/// Classify an HTTP response code as retryable or not
UploadStatus classifyHttp(long responseCode) noexcept;

/// Human readable name of an upload status, for logging
const char* toString(UploadStatus status) noexcept;

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
