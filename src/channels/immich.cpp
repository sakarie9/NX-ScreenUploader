#include "channels/immich.hpp"

#include <curl/curl.h>

#include <filesystem>
#include <string>
#include <string_view>

#include "channels/ini_helpers.hpp"
#include "config.hpp"
#include "logger.hpp"
#include "upload.hpp"
#include "utils.hpp"

namespace fs = std::filesystem;

bool ImmichChannel::Config::load(const char* configPath) {
    serverUrl = IniHelpers::getString(SECTION, "server_url",
                                      Defaults::SERVER_URL, configPath);
    apiKey = IniHelpers::getString(SECTION, "api_key", Defaults::API_KEY,
                                   configPath);
    uploadScreenshots =
        IniHelpers::getBool(SECTION, "upload_screenshots",
                            Defaults::UPLOAD_SCREENSHOTS, configPath);
    uploadVideos = IniHelpers::getBool(SECTION, "upload_videos",
                                       Defaults::UPLOAD_VIDEOS, configPath);

    return true;
}

bool ImmichChannel::Config::validate() {
    if (!serverUrl.empty() && !apiKey.empty()) {
        return true;
    }
    Logger::get().warn()
        << "Immich channel disabled: Invalid or missing configuration "
           "(server_url and/or api_key are not set)"
        << endl;
    return false;
}

bool ImmichChannel::send(std::string_view path) {
    constexpr std::string_view logPrefix = "[Immich] ";
    std::string_view tid;
    bool isVideo;

    const size_t size = filesize(path);
    Logger::get().info() << logPrefix << "Starting upload - File: " << path
                         << ", Size: " << size << " bytes ("
                         << (size / 1024.0 / 1024.0) << " MB)" << endl;

    // Validate file and check if upload is needed
    const auto validationResult = validateUploadFile(
        path, logPrefix, tid, isVideo, ::Config::get().immich.uploadScreenshots,
        ::Config::get().immich.uploadVideos);
    if (validationResult == ValidationResult::Error) {
        return false;
    }
    if (validationResult == ValidationResult::Skip) {
        return true;  // Not an error, just skipping per config
    }

    const fs::path filePath{path};
    const std::string filename = filePath.filename().string();

    CURL* curl = curl_easy_init();
    if (!curl) {
        // curl_formfree(formpost);
        Logger::get().error() << logPrefix << "curl_easy_init() failed" << endl;
        return false;
    }

    // Build URL
    const auto& immichServerUrl = ::Config::get().immich.serverUrl;
    const auto& immichApiKey = ::Config::get().immich.apiKey;

    std::string url;
    url.reserve(immichServerUrl.size());
    url += immichServerUrl;
    url += "/api";
    url += "/assets";

    Logger::get().debug() << logPrefix << "URL: " << url << endl;

    // Build headers
    std::string authHeader = "x-api-key: ";
    authHeader += immichApiKey;
    struct curl_slist* slist1 = NULL;
    slist1 = curl_slist_append(slist1, authHeader.c_str());
    curl_mime* mime;
    curl_mimepart* part;

    /* Send the data using a mime object as "assetData" */
    mime = curl_mime_init(curl);
    part = curl_mime_addpart(mime);
    curl_mime_type(part, "multipart/mixed");
    curl_mime_filedata(part, path.data());
    curl_mime_name(part, "assetData");

    // Get the local time for the switch and present it to Immich in the
    // fileCreatedAt and fileModifiedAt fields
    const std::string timeStr = getCurrentTimeISO8601();

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "fileCreatedAt");
    curl_mime_data(part, timeStr.c_str(), CURL_ZERO_TERMINATED);

    part = curl_mime_addpart(mime);
    curl_mime_name(part, "fileModifiedAt");
    curl_mime_data(part, timeStr.c_str(), CURL_ZERO_TERMINATED);

    curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist1);
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, NX_CURL_BUFFERSIZE);
    curl_easy_setopt(curl, CURLOPT_UPLOAD_BUFFERSIZE,
                     NX_CURL_UPLOAD_BUFFERSIZE);
    setCurlTimeouts(curl, isVideo);

    Logger::get().debug() << logPrefix << "CURL config - File type: "
                          << (isVideo ? "video" : "image")
                          << ", Connect timeout: "
                          << (isVideo ? VideoTimeouts::connectTimeout
                                      : ImageTimeouts::connectTimeout)
                          << "s, Idle timeout: "
                          << (isVideo ? VideoTimeouts::idleTimeout
                                      : ImageTimeouts::idleTimeout)
                          << "s, Total timeout: "
                          << (isVideo ? VideoTimeouts::totalTimeout
                                      : ImageTimeouts::totalTimeout)
                          << "s" << endl;
    Logger::get().info() << logPrefix << "Starting CURL transfer..." << endl;

    const CURLcode res = curl_easy_perform(curl);

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
        curl_mime_free(mime);
        curl_easy_cleanup(curl);
        curl_slist_free_all(slist1);

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
        curl_mime_free(mime);
        curl_slist_free_all(slist1);
        return false;
    }
}
