#include "upload.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <filesystem>
#include <string_view>

#include "config.hpp"
#include "logger.hpp"

namespace fs = std::filesystem;

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
    if (isVideo) {
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,
                         VideoTimeouts::connectTimeout);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME,
                         VideoTimeouts::idleTimeout);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT,
                         1L);  // At least 1 byte/sec
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, VideoTimeouts::totalTimeout);
    } else {
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT,
                         ImageTimeouts::connectTimeout);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME,
                         ImageTimeouts::idleTimeout);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, ImageTimeouts::totalTimeout);
    }
}

// File upload validation
ValidationResult validateUploadFile(std::string_view path,
                                    std::string_view logPrefix,
                                    std::string_view& tid, bool& isMovie,
                                    bool uploadScreenshots, bool uploadMovies) {
    // Extract Title ID (32 chars from the last 36 chars of the path)
    if (path.length() < 36) {
        Logger::get().error() << logPrefix << "Invalid path length" << endl;
        return ValidationResult::Error;
    }

    tid = path.substr(path.length() - 36, 32);
    Logger::get().debug() << logPrefix << "Title ID: " << tid << endl;

    isMovie = path.back() == '4';
    // Check target-specific config to determine whether this type is allowed to
    // upload
    const bool shouldUpload = isMovie ? uploadMovies : uploadScreenshots;
    if (!shouldUpload) {
        Logger::get().info()
            << logPrefix << "Skipping upload for " << path << endl;
        return ValidationResult::Skip;
    }

    return ValidationResult::Success;
}