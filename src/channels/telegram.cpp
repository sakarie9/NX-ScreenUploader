#include "channels/telegram.hpp"

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

namespace {

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

}  // namespace

bool TelegramChannel::Config::load(const char* configPath) {
    botToken = IniHelpers::getString(SECTION, "bot_token", Defaults::BOT_TOKEN,
                                     configPath);
    chatId = IniHelpers::getString(SECTION, "chat_id", Defaults::CHAT_ID,
                                   configPath);
    apiUrl = IniHelpers::getString(SECTION, "api_url", Defaults::API_URL,
                                   configPath);
    uploadScreenshots =
        IniHelpers::getBool(SECTION, "upload_screenshots",
                            Defaults::UPLOAD_SCREENSHOTS, configPath);
    uploadMovies = IniHelpers::getBool(SECTION, "upload_movies",
                                       Defaults::UPLOAD_MOVIES, configPath);
    uploadMode = IniHelpers::getString(SECTION, "upload_mode",
                                       Defaults::UPLOAD_MODE, configPath);

    // Validate upload mode
    if (uploadMode != "compressed" && uploadMode != "original" &&
        uploadMode != "both") {
        Logger::get().warn()
            << "Invalid Telegram upload mode: '" << uploadMode
            << "' (valid modes: compressed, original, both). Resetting to "
               "default."
            << endl;
        uploadMode = std::string(Defaults::UPLOAD_MODE);
    }

    // Log Telegram upload mode
    Logger::get().info() << "Telegram upload mode: " << uploadMode << endl;

    return true;
}

bool TelegramChannel::Config::validate() {
    if (!botToken.empty() && !chatId.empty()) {
        return true;
    }
    Logger::get().warn()
        << "Telegram channel disabled: Invalid or missing configuration "
           "(bot_token and/or chat_id are not set)"
        << endl;
    return false;
}

bool TelegramChannel::send(std::string_view path) {
    constexpr std::string_view logPrefix = "[Telegram] ";
    const auto& uploadMode = ::Config::get().telegram.uploadMode;
    std::string_view tid;
    bool isMovie;

    const size_t size = filesize(path);
    Logger::get().info() << logPrefix << "Starting upload - File: " << path
                         << ", Size: " << size << " bytes ("
                         << (size / 1024.0 / 1024.0) << " MB)" << endl;

    // Validate file and check if upload is needed
    const auto validationResult =
        validateUploadFile(path, logPrefix, tid, isMovie,
                           ::Config::get().telegram.uploadScreenshots,
                           ::Config::get().telegram.uploadMovies);
    if (validationResult == ValidationResult::Error) {
        return false;
    }
    if (validationResult == ValidationResult::Skip) {
        return true;
    }

    const fs::path filePath{path};

    // Lambda to handle a single upload attempt with or without compression
    auto attempt = [&](bool compression) -> bool {
        Logger::get().info()
            << logPrefix
            << "Mode: " << (compression ? "compressed" : "original") << endl;

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

        // Add video dimensions support for Telegram video uploads
        if (isMovie) {
            curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, "width",
                         CURLFORM_COPYCONTENTS, "1280", CURLFORM_END);
            curl_formadd(&formpost, &lastptr, CURLFORM_COPYNAME, "height",
                         CURLFORM_COPYCONTENTS, "720", CURLFORM_END);
        }

        CURL* curl = curl_easy_init();
        if (!curl) {
            std::fclose(f);
            curl_formfree(formpost);
            Logger::get().error()
                << logPrefix << "curl_easy_init() failed" << endl;
            return false;
        }

        // Build URL
        const auto& apiUrl = ::Config::get().telegram.apiUrl;
        const auto& botToken = ::Config::get().telegram.botToken;
        const auto& chatId = ::Config::get().telegram.chatId;

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

        Logger::get().debug()
            << logPrefix << "Send method: " << fileTypeInfo.telegramMethod
            << endl;

        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_READFUNCTION, uploadReadFunction);
        curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
        curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, NX_CURL_BUFFERSIZE);
        curl_easy_setopt(curl, CURLOPT_UPLOAD_BUFFERSIZE,
                         NX_CURL_UPLOAD_BUFFERSIZE);
        setCurlTimeouts(curl, isMovie);

        Logger::get().debug()
            << logPrefix
            << "CURL config - File type: " << (isMovie ? "video" : "image")
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
        Logger::get().info()
            << logPrefix << "Starting CURL transfer..." << endl;

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
                << ", Bytes sent: " << requestSize << ", File: " << path
                << endl;
            curl_easy_cleanup(curl);
            curl_formfree(formpost);
            return false;
        }
    };

    // Dispatch based on upload mode
    if (uploadMode == "both") {
        return attempt(true) || attempt(false);
    }
    return attempt(uploadMode == "compressed");
}
