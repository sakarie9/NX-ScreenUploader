#include "channels/ntfy.hpp"

#include <curl/curl.h>

#include <cstdio>
#include <filesystem>
#include <string_view>

#include "channels/ini_helpers.hpp"
#include "config.hpp"
#include "logger.hpp"
#include "upload.hpp"
#include "utils.hpp"

namespace fs = std::filesystem;

bool NtfyChannel::Config::load(const char* configPath) {
    url = IniHelpers::getString(SECTION, "url", Defaults::URL, configPath);
    topic =
        IniHelpers::getString(SECTION, "topic", Defaults::TOPIC, configPath);
    token =
        IniHelpers::getString(SECTION, "token", Defaults::TOKEN, configPath);
    priority = IniHelpers::getString(SECTION, "priority", Defaults::PRIORITY,
                                     configPath);
    uploadScreenshots =
        IniHelpers::getBool(SECTION, "upload_screenshots",
                            Defaults::UPLOAD_SCREENSHOTS, configPath);
    uploadMovies = IniHelpers::getBool(SECTION, "upload_movies",
                                       Defaults::UPLOAD_MOVIES, configPath);

    return true;
}

bool NtfyChannel::Config::validate() {
    if (!topic.empty()) {
        return true;
    }
    Logger::get().warn() << "Ntfy channel disabled: Invalid or missing "
                            "configuration (topic is not set)"
                         << endl;
    return false;
}

bool NtfyChannel::send(std::string_view path) {
    constexpr std::string_view logPrefix = "[ntfy] ";
    std::string_view tid;
    bool isMovie;

    const size_t size = filesize(path);
    Logger::get().info() << logPrefix << "Starting upload - File: " << path
                         << ", Size: " << size << " bytes ("
                         << (size / 1024.0 / 1024.0) << " MB)" << endl;

    // Validate file and check if upload is needed
    const auto validationResult = validateUploadFile(
        path, logPrefix, tid, isMovie, ::Config::get().ntfy.uploadScreenshots,
        ::Config::get().ntfy.uploadMovies);
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
    const auto& ntfyUrl = ::Config::get().ntfy.url;
    const auto& topic = ::Config::get().ntfy.topic;

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

    Logger::get().debug() << logPrefix << "URL: " << url << endl;

    // Build headers
    struct curl_slist* headers = nullptr;

    std::string filenameHeader = "Filename: ";
    filenameHeader += filename;
    headers = curl_slist_append(headers, filenameHeader.c_str());

    const auto& token = ::Config::get().ntfy.token;
    if (!token.empty()) {
        std::string authHeader = "Authorization: Bearer ";
        authHeader += token;
        headers = curl_slist_append(headers, authHeader.c_str());
    }

    const auto& priority = ::Config::get().ntfy.priority;
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
