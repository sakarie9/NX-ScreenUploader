#include "config.hpp"

#include <minIni.h>

#include "project.h"

// Configuration file path
static constexpr const char* CONFIG_PATH =
    "sdmc:/config/" APP_TITLE "/config.ini";

// Helper: read a string value with an upper bound of maxlen characters
static std::string ini_get_string(const char* section, const char* key,
                                  const char* default_value,
                                  size_t maxlen = 256) {
    std::string result(maxlen, '\0');
    int len = ini_gets(section, key, default_value, result.data(), maxlen,
                       CONFIG_PATH);
    if (len > 0) {
        result.resize(len);
    } else {
        result = default_value;
    }
    return result;
}

bool Config::refresh() {
    // Read Telegram configuration
    m_telegramBotToken =
        ini_get_string("general", "telegram_bot_token", "undefined");
    m_telegramChatId =
        ini_get_string("general", "telegram_chat_id", "undefined");
    m_telegramApiUrl = ini_get_string("general", "telegram_api_url",
                                      "https://api.telegram.org");

    // Read which file types to upload
    m_uploadScreenshots =
        ini_getbool("general", "upload_screenshots", 1, CONFIG_PATH) != 0;
    m_uploadMovies =
        ini_getbool("general", "upload_movies", 1, CONFIG_PATH) != 0;
    m_keepLogs = ini_getbool("general", "keep_logs", 0, CONFIG_PATH) != 0;

    // Read upload mode: compressed, original, or both (default: compressed)
    std::string uploadModeStr =
        ini_get_string("general", "upload_mode", "compressed");
    if (uploadModeStr == "compressed") {
        m_uploadMode = UploadMode::Compressed;
    } else if (uploadModeStr == "original") {
        m_uploadMode = UploadMode::Original;
    } else if (uploadModeStr == "both") {
        m_uploadMode = UploadMode::Both;
    } else {
        m_uploadMode = UploadMode::Compressed;  // Fallback default
    }

    // Read check interval (seconds), default 3s, minimum 1s
    m_checkIntervalSeconds =
        (int)ini_getl("general", "check_interval", 3, CONFIG_PATH);
    if (m_checkIntervalSeconds < 1) {
        m_checkIntervalSeconds = 1;
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
