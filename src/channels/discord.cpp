#include "channels/discord.hpp"

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

bool DiscordChannel::Config::load(const char* configPath) {
    botToken = IniHelpers::getString(SECTION, "bot_token", Defaults::BOT_TOKEN,
                                     configPath);
    channelId = IniHelpers::getString(SECTION, "channel_id",
                                      Defaults::CHANNEL_ID, configPath);
    apiUrl = IniHelpers::getString(SECTION, "api_url", Defaults::API_URL,
                                   configPath);
    uploadScreenshots =
        IniHelpers::getBool(SECTION, "upload_screenshots",
                            Defaults::UPLOAD_SCREENSHOTS, configPath);
    uploadVideos = IniHelpers::getBool(SECTION, "upload_videos",
                                       Defaults::UPLOAD_VIDEOS, configPath);

    return true;
}

bool DiscordChannel::Config::validate() {
    if (!botToken.empty() && !channelId.empty()) {
        return true;
    }
    Logger::get().warn()
        << "Discord channel disabled: Invalid or missing configuration "
           "(bot_token and/or channel_id are not set)"
        << endl;
    return false;
}

bool DiscordChannel::send(std::string_view path) {
    constexpr std::string_view logPrefix = "[Discord] ";
    std::string_view tid;
    bool isVideo;

    const size_t size = filesize(path);
    Logger::get().info() << logPrefix << "Starting upload - File: " << path
                         << ", Size: " << size << " bytes ("
                         << (size / 1024.0 / 1024.0) << " MB)" << endl;

    // Validate file and check if upload is needed
    const auto validationResult =
        validateUploadFile(path, logPrefix, tid, isVideo,
                           ::Config::get().discord.uploadScreenshots,
                           ::Config::get().discord.uploadVideos);
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
    const auto& apiUrl = ::Config::get().discord.apiUrl;
    const auto& botToken = ::Config::get().discord.botToken;
    const auto& channelId = ::Config::get().discord.channelId;

    std::string url;
    url.reserve(apiUrl.size() + channelId.size() + 3);
    url = apiUrl;
    url += "/channels/";
    url += channelId;
    url += "/messages";

    Logger::get().debug() << logPrefix << "URL: " << url << endl;

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
