#include "upload.hpp"

#include <curl/curl.h>

#include <filesystem>
#include <string_view>

#include "config.hpp"
#include "logger.hpp"

namespace fs = std::filesystem;

namespace {

constexpr size_t NX_CURL_BUFFERSIZE = 0x2000L;         // 8KB
constexpr size_t NX_CURL_UPLOAD_BUFFERSIZE = 0x2000L;  // 8KB

struct UploadInfo {
    FILE* f;
    size_t sizeLeft;
};

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

struct FileTypeInfo {
    std::string_view contentType;
    std::string_view copyName;
    std::string_view telegramMethod;
};

constexpr FileTypeInfo getFileTypeInfo(std::string_view extension,
                                       bool compression) noexcept {
    if (extension == ".jpg") {
        return compression
                   ? FileTypeInfo{"image/jpeg", "photo", "sendPhoto"}
                   : FileTypeInfo{"image/jpeg", "document", "sendDocument"};
    } else if (extension == ".mp4") {
        return compression
                   ? FileTypeInfo{"video/mp4", "video", "sendVideo"}
                   : FileTypeInfo{"video/mp4", "document", "sendDocument"};
    }
    return FileTypeInfo{"", "", ""};
}

// Validation result for file uploads
enum class ValidationResult {
    Success,  // Valid and should upload
    Skip,     // Valid but skip due to config
    Error     // Invalid file
};

// Common validation logic for file uploads
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

// Helper to configure CURL timeouts based on file type
inline void setCurlTimeouts(CURL* curl, bool isVideo) {
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

}  // namespace

bool sendFileToTelegram(std::string_view path, size_t size, bool compression) {
    constexpr std::string_view logPrefix = "[Telegram] ";
    std::string_view tid;
    bool isMovie;

    Logger::get().info() << logPrefix << "Starting upload - File: " << path
                         << ", Size: " << size << " bytes ("
                         << (size / 1024.0 / 1024.0) << " MB)"
                         << ", Compression: "
                         << (compression ? "enabled" : "disabled") << endl;

    // Validate file and check if upload is needed
    const auto validationResult =
        validateUploadFile(path, logPrefix, tid, isMovie,
                           Config::get().telegramUploadScreenshots(),
                           Config::get().telegramUploadMovies());
    if (validationResult == ValidationResult::Error) {
        return false;
    }
    if (validationResult == ValidationResult::Skip) {
        return true;  // Not an error, just skipping per config
    }

    const fs::path filePath{path};
    const auto fileTypeInfo =
        getFileTypeInfo(filePath.extension().string(), compression);

    if (fileTypeInfo.contentType.empty()) {
        Logger::get().error() << logPrefix << "Unknown file extension: "
                              << filePath.extension().string() << endl;
        return false;
    }

    FILE* f = std::fopen(filePath.c_str(), "rb");
    if (f == nullptr) {
        Logger::get().error()
            << logPrefix << "fopen() failed for file: " << path << endl;
        return false;
    }

    UploadInfo ui{f, size};
    struct curl_httppost* formpost = nullptr;
    struct curl_httppost* lastptr = nullptr;

    curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME,
                 fileTypeInfo.copyName.data(), CURLFORM_FILENAME,
                 filePath.c_str(), CURLFORM_STREAM, &ui,
                 CURLFORM_CONTENTSLENGTH, size, CURLFORM_CONTENTTYPE,
                 fileTypeInfo.contentType.data(), CURLFORM_END);

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::fclose(f);
        curl_formfree(formpost);
        Logger::get().error() << logPrefix << "curl_easy_init() failed" << endl;
        return false;
    }

    // Build URL
    const auto apiUrl = Config::get().getTelegramApiUrl();
    const auto botToken = Config::get().getTelegramBotToken();
    const auto chatId = Config::get().getTelegramChatId();

    std::string url;
    url.reserve(apiUrl.size() + botToken.size() + chatId.size() +
                fileTypeInfo.telegramMethod.size() + 20);
    url = apiUrl;
    url += "/bot";
    url += botToken;
    url += "/";
    url += fileTypeInfo.telegramMethod;
    url += "?chat_id=";
    url += chatId;

    Logger::get().debug() << logPrefix << "URL is " << url << endl;

    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, uploadReadFunction);
    curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, NX_CURL_BUFFERSIZE);
    curl_easy_setopt(curl, CURLOPT_UPLOAD_BUFFERSIZE,
                     NX_CURL_UPLOAD_BUFFERSIZE);
    setCurlTimeouts(curl, isMovie);

    Logger::get().debug() << logPrefix << "CURL config - File type: "
                          << (isMovie ? "video" : "image")
                          << ", Connect timeout: "
                          << (isMovie ? VideoTimeouts::connectTimeout
                                      : ImageTimeouts::connectTimeout)
                          << "s, Idle timeout: "
                          << (isMovie ? VideoTimeouts::idleTimeout
                                      : ImageTimeouts::idleTimeout)
                          << "s, Total timeout: "
                          << (isMovie ? VideoTimeouts::totalTimeout
                                      : ImageTimeouts::totalTimeout)
                          << "s" << endl;
    Logger::get().info() << logPrefix << "Starting CURL transfer..." << endl;

    const CURLcode res = curl_easy_perform(curl);
    std::fclose(f);

    if (res == CURLE_OK) {
        long responseCode;
        double requestSize;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
        curl_easy_getinfo(curl, CURLINFO_SIZE_UPLOAD, &requestSize);

        Logger::get().debug()
            << logPrefix << requestSize
            << " bytes sent, response code: " << responseCode << endl;

        curl_easy_cleanup(curl);
        curl_formfree(formpost);

        if (responseCode == 200) {
            Logger::get().info()
                << logPrefix << "Successfully uploaded " << path << endl;
            return true;
        }

        Logger::get().error()
            << logPrefix << "HTTP error - Response code: " << responseCode
            << ", File: " << path << ", Size: " << size << " bytes" << endl;
        return false;
    } else {
        double requestSize = 0;
        curl_easy_getinfo(curl, CURLINFO_SIZE_UPLOAD, &requestSize);
        Logger::get().error()
            << logPrefix << "CURL error: " << curl_easy_strerror(res)
            << " (code: " << res << ")"
            << ", Bytes sent: " << requestSize << ", File: " << path << endl;
        curl_easy_cleanup(curl);
        curl_formfree(formpost);
        return false;
    }
}

bool sendFileToNtfy(std::string_view path, size_t size) {
    constexpr std::string_view logPrefix = "[ntfy] ";
    std::string_view tid;
    bool isMovie;

    Logger::get().info() << logPrefix << "Starting upload - File: " << path
                         << ", Size: " << size << " bytes ("
                         << (size / 1024.0 / 1024.0) << " MB)" << endl;

    // Validate file and check if upload is needed
    const auto validationResult = validateUploadFile(
        path, logPrefix, tid, isMovie, Config::get().ntfyUploadScreenshots(),
        Config::get().ntfyUploadMovies());
    if (validationResult == ValidationResult::Error) {
        return false;
    }
    if (validationResult == ValidationResult::Skip) {
        return true;  // Not an error, just skipping per config
    }

    const fs::path filePath{path};
    const std::string filename = filePath.filename().string();

    FILE* f = std::fopen(filePath.c_str(), "rb");
    if (f == nullptr) {
        Logger::get().error()
            << logPrefix << "fopen() failed for file: " << path << endl;
        return false;
    }

    UploadInfo ui{f, size};

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::fclose(f);
        Logger::get().error() << logPrefix << "curl_easy_init() failed" << endl;
        return false;
    }

    // Build URL
    const auto ntfyUrl = Config::get().getNtfyUrl();
    const auto topic = Config::get().getNtfyTopic();

    if (topic.empty()) {
        std::fclose(f);
        curl_easy_cleanup(curl);
        Logger::get().error() << logPrefix << "Topic is not configured" << endl;
        return false;
    }

    std::string url;
    url.reserve(ntfyUrl.size() + topic.size() + 2);
    url = ntfyUrl;
    url += "/";
    url += topic;

    // url will expose bot token in logs, so avoid logging it
    // Logger::get().debug() << logPrefix << "URL is " << url << endl;

    // Build headers
    struct curl_slist* headers = nullptr;

    std::string filenameHeader = "Filename: ";
    filenameHeader += filename;
    headers = curl_slist_append(headers, filenameHeader.c_str());

    const auto token = Config::get().getNtfyToken();
    if (!token.empty()) {
        std::string authHeader = "Authorization: Bearer ";
        authHeader += token;
        headers = curl_slist_append(headers, authHeader.c_str());
    }

    const auto priority = Config::get().getNtfyPriority();
    if (!priority.empty() && priority != "default") {
        std::string priorityHeader = "Priority: ";
        priorityHeader += priority;
        headers = curl_slist_append(headers, priorityHeader.c_str());
    }

    std::string titleHeader = "Title: Screenshot from ";
    titleHeader += tid;
    headers = curl_slist_append(headers, titleHeader.c_str());

    // Configure CURL for PUT upload
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, uploadReadFunction);
    curl_easy_setopt(curl, CURLOPT_READDATA, &ui);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE,
                     static_cast<curl_off_t>(size));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, NX_CURL_BUFFERSIZE);
    curl_easy_setopt(curl, CURLOPT_UPLOAD_BUFFERSIZE,
                     NX_CURL_UPLOAD_BUFFERSIZE);
    setCurlTimeouts(curl, isMovie);

    Logger::get().debug() << logPrefix << "CURL config - File type: "
                          << (isMovie ? "video" : "image")
                          << ", Connect timeout: "
                          << (isMovie ? VideoTimeouts::connectTimeout
                                      : ImageTimeouts::connectTimeout)
                          << "s, Idle timeout: "
                          << (isMovie ? VideoTimeouts::idleTimeout
                                      : ImageTimeouts::idleTimeout)
                          << "s, Total timeout: "
                          << (isMovie ? VideoTimeouts::totalTimeout
                                      : ImageTimeouts::totalTimeout)
                          << "s" << endl;
    Logger::get().info() << logPrefix << "Starting CURL transfer..." << endl;

    const CURLcode res = curl_easy_perform(curl);
    std::fclose(f);

    if (res == CURLE_OK) {
        long responseCode;
        double requestSize;
        double totalTime;
        double uploadSpeed;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
        curl_easy_getinfo(curl, CURLINFO_SIZE_UPLOAD, &requestSize);
        curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &totalTime);
        curl_easy_getinfo(curl, CURLINFO_SPEED_UPLOAD, &uploadSpeed);

        Logger::get().info()
            << logPrefix << "Transfer complete - " << requestSize
            << " bytes sent (" << (requestSize / 1024.0 / 1024.0) << " MB), "
            << "Response code: " << responseCode << ", "
            << "Time: " << totalTime << "s, "
            << "Speed: " << (uploadSpeed / 1024.0) << " KB/s" << endl;

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (responseCode == 200) {
            Logger::get().info()
                << logPrefix << "Successfully uploaded " << path << endl;
            return true;
        }

        Logger::get().error()
            << logPrefix << "HTTP error - Response code: " << responseCode
            << ", File: " << path << ", Size: " << size << " bytes" << endl;
        return false;
    } else {
        double requestSize = 0;
        curl_easy_getinfo(curl, CURLINFO_SIZE_UPLOAD, &requestSize);
        Logger::get().error()
            << logPrefix << "CURL error: " << curl_easy_strerror(res)
            << " (code: " << res << ")"
            << ", Bytes sent: " << requestSize << ", File: " << path << endl;
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        return false;
    }
}

bool sendFileToDiscord(std::string_view path, size_t size) {
    constexpr std::string_view logPrefix = "[Discord] ";
    std::string_view tid;
    bool isMovie;

    Logger::get().info() << logPrefix << "Starting upload - File: " << path
                         << ", Size: " << size << " bytes ("
                         << (size / 1024.0 / 1024.0) << " MB)" << endl;

    // Validate file and check if upload is needed
    const auto validationResult = validateUploadFile(
        path, logPrefix, tid, isMovie, Config::get().discordUploadScreenshots(),
        Config::get().discordUploadMovies());
    if (validationResult == ValidationResult::Error) {
        return false;
    }
    if (validationResult == ValidationResult::Skip) {
        return true;  // Not an error, just skipping per config
    }

    const fs::path filePath{path};
    const std::string filename = filePath.filename().string();

    FILE* f = std::fopen(filePath.c_str(), "rb");
    if (f == nullptr) {
        Logger::get().error()
            << logPrefix << "fopen() failed for file: " << path << endl;
        return false;
    }

    UploadInfo ui{f, size};
    struct curl_httppost* formpost = nullptr;
    struct curl_httppost* lastptr = nullptr;

    curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, "files[0]",
                 CURLFORM_FILENAME, filePath.filename().string().c_str(),
                 CURLFORM_STREAM, &ui, CURLFORM_CONTENTSLENGTH, size,
                 CURLFORM_END);

    CURL* curl = curl_easy_init();
    if (!curl) {
        std::fclose(f);
        curl_formfree(formpost);
        Logger::get().error() << logPrefix << "curl_easy_init() failed" << endl;
        return false;
    }

    // Build URL
    const auto apiUrl = Config::get().getDiscordApiUrl();
    const auto botToken = Config::get().getDiscordBotToken();
    const auto channelId = Config::get().getDiscordChannelId();

    std::string url;
    url.reserve(apiUrl.size() + channelId.size() + 3);
    url = apiUrl;
    url += "/channels/";
    url += channelId;
    url += "/messages";

    Logger::get().debug() << logPrefix << "URL is " << url << endl;

    // Build headers
    struct curl_slist* headers = nullptr;

    std::string authHeader = "Authorization: Bot ";
    authHeader += botToken;
    headers = curl_slist_append(headers, authHeader.c_str());

    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, uploadReadFunction);
    curl_easy_setopt(curl, CURLOPT_READDATA, &ui);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE,
                     static_cast<curl_off_t>(size));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, NX_CURL_BUFFERSIZE);
    curl_easy_setopt(curl, CURLOPT_UPLOAD_BUFFERSIZE,
                     NX_CURL_UPLOAD_BUFFERSIZE);
    setCurlTimeouts(curl, isMovie);

    Logger::get().debug() << logPrefix << "CURL config - File type: "
                          << (isMovie ? "video" : "image")
                          << ", Connect timeout: "
                          << (isMovie ? VideoTimeouts::connectTimeout
                                      : ImageTimeouts::connectTimeout)
                          << "s, Idle timeout: "
                          << (isMovie ? VideoTimeouts::idleTimeout
                                      : ImageTimeouts::idleTimeout)
                          << "s, Total timeout: "
                          << (isMovie ? VideoTimeouts::totalTimeout
                                      : ImageTimeouts::totalTimeout)
                          << "s" << endl;
    Logger::get().info() << logPrefix << "Starting CURL transfer..." << endl;

    const CURLcode res = curl_easy_perform(curl);
    std::fclose(f);

    if (res == CURLE_OK) {
        long responseCode;
        double requestSize;
        double totalTime;
        double uploadSpeed;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
        curl_easy_getinfo(curl, CURLINFO_SIZE_UPLOAD, &requestSize);
        curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &totalTime);
        curl_easy_getinfo(curl, CURLINFO_SPEED_UPLOAD, &uploadSpeed);

        Logger::get().info()
            << logPrefix << "Transfer complete - " << requestSize
            << " bytes sent (" << (requestSize / 1024.0 / 1024.0) << " MB), "
            << "Response code: " << responseCode << ", "
            << "Time: " << totalTime << "s, "
            << "Speed: " << (uploadSpeed / 1024.0) << " KB/s" << endl;

        curl_easy_cleanup(curl);
        curl_formfree(formpost);
        curl_slist_free_all(headers);

        if (responseCode == 200 || responseCode == 201) {
            Logger::get().info()
                << logPrefix << "Successfully uploaded " << path << endl;
            return true;
        }

        Logger::get().error()
            << logPrefix << "HTTP error - Response code: " << responseCode
            << ", File: " << path << ", Size: " << size << " bytes" << endl;
        return false;
    } else {
        double requestSize = 0;
        curl_easy_getinfo(curl, CURLINFO_SIZE_UPLOAD, &requestSize);
        Logger::get().error()
            << logPrefix << "CURL error: " << curl_easy_strerror(res)
            << " (code: " << res << ")"
            << ", Bytes sent: " << requestSize << ", File: " << path << endl;
        curl_easy_cleanup(curl);
        curl_formfree(formpost);
        curl_slist_free_all(headers);
        return false;
    }
}
