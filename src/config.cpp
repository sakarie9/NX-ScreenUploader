#include "config.hpp"

#include <minIni.h>

#include <cstring>

#include "logger.hpp"
#include "project.h"
#include "utils.hpp"

// 配置文件路径
static constexpr const char* CONFIG_PATH =
    "sdmc:/config/" APP_TITLE "/config.ini";

// Helper 函数：读取字符串，最多 maxlen 字符
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
    // 读取telegram配置
    m_telegramBotToken =
        ini_get_string("general", "telegram_bot_token", "undefined");
    m_telegramChatId =
        ini_get_string("general", "telegram_chat_id", "undefined");
    m_telegramApiUrl = ini_get_string("general", "telegram_api_url",
                                      "https://api.telegram.org");

    // 读取上传类型配置
    m_uploadScreenshots =
        ini_getbool("general", "upload_screenshots", 1, CONFIG_PATH) != 0;
    m_uploadMovies =
        ini_getbool("general", "upload_movies", 1, CONFIG_PATH) != 0;
    m_keepLogs = ini_getbool("general", "keep_logs", 0, CONFIG_PATH) != 0;

    // 读取上传模式配置: compressed, original, both (默认为compressed)
    std::string uploadModeStr =
        ini_get_string("general", "upload_mode", "compressed");
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
    m_checkIntervalSeconds =
        (int)ini_getl("general", "check_interval", 1, CONFIG_PATH);
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
