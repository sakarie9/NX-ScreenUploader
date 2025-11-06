#include "upload.hpp"

#include <curl/curl.h>
#include <netdb.h>

#include <array>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include "config.hpp"
#include "logger.hpp"

namespace fs = std::filesystem;

namespace {
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
}  // namespace

bool sendFileToServer(std::string_view path, size_t size, bool compression) {
    // 提取Title ID (倒数36个字符中的32个字符)
    if (path.length() < 36) {
        Logger::get().error() << "Invalid path length" << std::endl;
        return false;
    }

    const std::string_view tid = path.substr(path.length() - 36, 32);
    Logger::get().debug() << "Title ID: " << tid << std::endl;

    const bool isMovie = path.back() == '4';
    if (!Config::get().uploadAllowed(tid, isMovie)) {
        Logger::get().info() << "Skipping upload for " << path << std::endl;
        return true;
    }

    const fs::path filePath{path};
    const auto fileTypeInfo =
        getFileTypeInfo(filePath.extension().string(), compression);

    if (fileTypeInfo.contentType.empty()) {
        Logger::get().error()
            << "Unknown file extension: " << filePath.extension().string()
            << std::endl;
        return false;
    }

    FILE* f = std::fopen(filePath.c_str(), "rb");
    if (f == nullptr) {
        Logger::get().error() << "fopen() failed" << std::endl;
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
        Logger::get().error() << "curl_easy_init() failed" << std::endl;
        return false;
    }

    // 构建URL
    std::string url;
    url.reserve(128);
    url = Config::get().getTelegramApiUrl();
    url += "/bot";
    url += Config::get().getTelegramBotToken();
    url += "/";
    url += fileTypeInfo.telegramMethod;
    url += "?chat_id=";
    url += Config::get().getTelegramChatId();

    Logger::get().debug() << "URL is " << url << std::endl;

    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, uploadReadFunction);
    curl_easy_setopt(curl, CURLOPT_HTTPPOST, formpost);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    // Reduced buffer sizes from 8KB each to 4KB each to save memory
    curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 0x1000L);
    curl_easy_setopt(curl, CURLOPT_UPLOAD_BUFFERSIZE, 0x1000L);

    const CURLcode res = curl_easy_perform(curl);
    std::fclose(f);

    if (res == CURLE_OK) {
        long responseCode;
        double requestSize;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
        curl_easy_getinfo(curl, CURLINFO_SIZE_UPLOAD, &requestSize);

        Logger::get().debug()
            << requestSize << " bytes sent, response code: " << responseCode
            << std::endl;

        curl_easy_cleanup(curl);
        curl_formfree(formpost);

        if (responseCode == 200) {
            Logger::get().info()
                << "Successfully uploaded " << path << std::endl;
            return true;
        }

        Logger::get().error() << "Error uploading, got response code "
                              << responseCode << std::endl;
        return false;
    } else {
        Logger::get().error()
            << "curl_easy_perform() failed: " << curl_easy_strerror(res)
            << std::endl;
        curl_easy_cleanup(curl);
        curl_formfree(formpost);
        return false;
    }
}
