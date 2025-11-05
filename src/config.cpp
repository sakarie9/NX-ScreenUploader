#include "config.hpp"

#include <inih.h>

#include <iostream>

#include "logger.hpp"
#include "utils.hpp"

bool Config::refresh() {
    INIReader reader("sdmc:/config/sys-screen-capture-uploader/config.ini");

    if (const int parseError = reader.ParseError(); parseError != 0) {
        Logger::get().error()
            << "Config parse error " << parseError << std::endl;
        error = true;
        return false;
    }

    m_telegramBotToken =
        reader.Get("general", "telegram_bot_token", "undefined");
    m_telegramChatId = reader.Get("general", "telegram_chat_id", "undefined");
    m_telegramApiUrl =
        reader.Get("general", "telegram_api_url", "https://api.telegram.org");
    m_uploadScreenshots =
        reader.GetBoolean("general", "upload_screenshots", true);
    m_uploadMovies = reader.GetBoolean("general", "upload_movies", true);
    m_keepLogs = reader.GetBoolean("general", "keep_logs", false);

    // 读取上传模式配置: compressed, original, both (默认为compressed)
    const std::string uploadModeStr =
        reader.Get("general", "upload_mode", "compressed");
    if (uploadModeStr == "compressed") {
        m_uploadMode = UploadMode::Compressed;
    } else if (uploadModeStr == "original") {
        m_uploadMode = UploadMode::Original;
    } else if (uploadModeStr == "both") {
        m_uploadMode = UploadMode::Both;
    } else {
        m_uploadMode = UploadMode::Compressed;  // 默认值
    }

    // 读取检查间隔配置（秒），默认1秒，最低1秒
    m_checkIntervalSeconds = reader.GetInteger("general", "check_interval", 1);
    if (m_checkIntervalSeconds < 1) {
        m_checkIntervalSeconds = 1;
    }

    if (reader.HasSection("title_screenshots")) {
        const auto keys = reader.Keys("title_screenshots");
        m_titleScreenshots.reserve(keys.size());
        for (const auto& tid : keys) {
            m_titleScreenshots.emplace(
                tid, reader.GetBoolean("title_screenshots", tid, true));
        }
    }

    if (reader.HasSection("title_movies")) {
        const auto keys = reader.Keys("title_movies");
        m_titleMovies.reserve(keys.size());
        for (const auto& tid : keys) {
            m_titleMovies.emplace(tid,
                                  reader.GetBoolean("title_movies", tid, true));
        }
    }

    return true;
}

std::string_view Config::getTelegramBotToken() const noexcept {
    return m_telegramBotToken;
}

std::string_view Config::getTelegramChatId() const noexcept {
    return m_telegramChatId;
}

std::string_view Config::getTelegramApiUrl() const noexcept {
    return m_telegramApiUrl;
}

bool Config::uploadAllowed(std::string_view tid, bool isMovie) const noexcept {
    if (isMovie) {
        if (const auto it = m_titleMovies.find(std::string(tid));
            it != m_titleMovies.end()) {
            return it->second;
        }
        return m_uploadMovies;
    } else {
        if (const auto it = m_titleScreenshots.find(std::string(tid));
            it != m_titleScreenshots.end()) {
            return it->second;
        }
        return m_uploadScreenshots;
    }
}
