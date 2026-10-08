#include "upload.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <filesystem>
#include <string_view>

#include "config.hpp"
#include "logger.hpp"

namespace fs = std::filesystem;

// Upload result classification

UploadStatus classifyCurl(CURLcode res) noexcept {
    switch (res) {
        // Transport level failures that a later attempt may recover from
        case CURLE_OPERATION_TIMEDOUT:
        case CURLE_COULDNT_CONNECT:
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_RESOLVE_PROXY:
        case CURLE_SEND_ERROR:
        case CURLE_RECV_ERROR:
        case CURLE_GOT_NOTHING:
        case CURLE_PARTIAL_FILE:
        case CURLE_SSL_CONNECT_ERROR:
            return UploadStatus::Transient;
        default:
            // Configuration, URL, certificate and similar errors will fail
            // again on every retry.
            return UploadStatus::Permanent;
    }
}

UploadStatus classifyHttp(long responseCode) noexcept {
    if (responseCode == 408 || responseCode == 425 || responseCode == 429) {
        return UploadStatus::Transient;  // Too early / rate limited
    }
    if (responseCode >= 500 && responseCode <= 599) {
        return UploadStatus::Transient;  // Server side problem
    }
    // 400/401/403/404/413/415/422 and friends: retrying cannot help
    return UploadStatus::Permanent;
}

const char* toString(UploadStatus status) noexcept {
    switch (status) {
        case UploadStatus::Success:
            return "success";
        case UploadStatus::Skipped:
            return "skipped";
        case UploadStatus::Transient:
            return "transient failure";
        case UploadStatus::Permanent:
            return "permanent failure";
    }
    return "unknown";
}

// CURL read callback for streaming file content
size_t uploadReadFunction(void* ptr, size_t size, size_t nmemb,
                          void* data) noexcept {
    auto* ui = static_cast<UploadInfo*>(data);
    const size_t maxBytes = size * nmemb;

    if (maxBytes < 1 || ui->sizeLeft == 0) {
        return 0;
    }

    const size_t bytesToRead = std::min(ui->sizeLeft, maxBytes);
    const size_t bytesRead = std::fread(ptr, 1, bytesToRead, ui->f);
    ui->sizeLeft -= bytesRead;

    // Log progress every 100KB or at completion
    static size_t lastLoggedProgress = 0;
    const size_t currentProgress = ui->sizeLeft;
    if (currentProgress == 0 ||
        (lastLoggedProgress - currentProgress) >= 102400) {
        Logger::get().debug() << "[Upload] Progress: " << currentProgress
                              << " bytes remaining" << endl;
        lastLoggedProgress = currentProgress;
    }

    return bytesRead;
}

// CURL timeout configuration helper
void setCurlTimeouts(CURL* curl, bool isVideo) {
    const UploadPolicy& policy = Config::get().policy(isVideo);

    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, policy.connectTimeout);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, policy.idleTimeout);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, policy.lowSpeedLimit);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, policy.totalTimeout);
}

// File upload validation
ValidationResult validateUploadFile(std::string_view path,
                                    std::string_view logPrefix,
                                    std::string_view& tid, bool& isVideo,
                                    bool uploadScreenshots, bool uploadVideos) {
    // Extract Title ID (32 chars from the last 36 chars of the path)
    if (path.length() < 36) {
        Logger::get().error() << logPrefix << "Invalid path length" << endl;
        return ValidationResult::Error;
    }

    tid = path.substr(path.length() - 36, 32);
    Logger::get().debug() << logPrefix << "Title ID: " << tid << endl;

    isVideo = path.back() == '4';
    // Check target-specific config to determine whether this type is allowed to
    // upload
    const bool shouldUpload = isVideo ? uploadVideos : uploadScreenshots;
    if (!shouldUpload) {
        Logger::get().info()
            << logPrefix << "Skipping upload for " << path << endl;
        return ValidationResult::Skip;
    }

    return ValidationResult::Success;
}