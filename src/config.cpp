#include "config.hpp"

#include <minIni.h>
#include <sys/stat.h>

#include "logger.hpp"
#include "project.h"

// Configuration file path
static constexpr const char* CONFIG_PATH =
    "sdmc:/config/" APP_TITLE "/config.ini";

// Helper: read a string value with an upper bound of maxlen characters
static std::string ini_get_string(const char* section, const char* key,
                                  std::string_view default_value,
                                  size_t maxlen = 256) {
    std::string result(maxlen, '\0');
    int len = ini_gets(section, key, default_value.data(), result.data(),
                       maxlen, CONFIG_PATH);
    if (len > 0) {
        result.resize(len);
    } else {
        result = default_value;
    }
    return result;
}

// Helper: read a boolean value
static bool ini_get_bool(const char* section, const char* key,
                         bool default_value) {
    return ini_getbool(section, key, default_value ? 1 : 0, CONFIG_PATH) != 0;
}

// Helper: read an integer value
static long ini_get_long(const char* section, const char* key,
                         long default_value) {
    return ini_getl(section, key, default_value, CONFIG_PATH);
}

// Helper: check if config file exists
static bool configFileExists() {
    struct stat buffer;
    return stat(CONFIG_PATH, &buffer) == 0;
}

bool Config::refresh() {
    // Check if config file exists
    if (!configFileExists()) {
        Logger::get().error()
            << "Config file not found at: " << CONFIG_PATH << endl;
        return false;
    }

    // Read upload destination toggles
    m_telegramEnabled =
        ini_get_bool("general", "telegram", ConfigDefaults::TELEGRAM_ENABLED);
    m_ntfyEnabled =
        ini_get_bool("general", "ntfy", ConfigDefaults::NTFY_ENABLED);
    m_discordEnabled =
        ini_get_bool("general", "discord", ConfigDefaults::DISCORD_ENABLED);

    // Read Telegram configuration from [telegram] section
    m_telegramBotToken = ini_get_string("telegram", "bot_token",
                                        ConfigDefaults::TELEGRAM_BOT_TOKEN);
    m_telegramChatId =
        ini_get_string("telegram", "chat_id", ConfigDefaults::TELEGRAM_CHAT_ID);
    m_telegramApiUrl =
        ini_get_string("telegram", "api_url", ConfigDefaults::TELEGRAM_API_URL);
    m_telegramUploadScreenshots =
        ini_get_bool("telegram", "upload_screenshots",
                     ConfigDefaults::TELEGRAM_UPLOAD_SCREENSHOTS);
    m_telegramUploadMovies = ini_get_bool(
        "telegram", "upload_movies", ConfigDefaults::TELEGRAM_UPLOAD_MOVIES);

    // Read Telegram upload mode: compressed, original, or both
    // Stored directly as string to avoid unnecessary conversions
    m_telegramUploadMode = ini_get_string("telegram", "upload_mode",
                                          ConfigDefaults::TELEGRAM_UPLOAD_MODE);

    // Read Ntfy configuration from [ntfy] section
    m_ntfyUrl = ini_get_string("ntfy", "url", ConfigDefaults::NTFY_URL);
    m_ntfyTopic = ini_get_string("ntfy", "topic", ConfigDefaults::NTFY_TOPIC);
    m_ntfyToken = ini_get_string("ntfy", "token", ConfigDefaults::NTFY_TOKEN);
    m_ntfyPriority =
        ini_get_string("ntfy", "priority", ConfigDefaults::NTFY_PRIORITY);
    m_ntfyUploadScreenshots = ini_get_bool(
        "ntfy", "upload_screenshots", ConfigDefaults::NTFY_UPLOAD_SCREENSHOTS);
    m_ntfyUploadMovies = ini_get_bool("ntfy", "upload_movies",
                                      ConfigDefaults::NTFY_UPLOAD_MOVIES);

    // Read Discord configuration from [discord] section
    m_discordBotToken = ini_get_string("discord", "bot_token",
                                       ConfigDefaults::DISCORD_BOT_TOKEN);
    m_discordChannelId = ini_get_string("discord", "channel_id",
                                        ConfigDefaults::DISCORD_CHANNEL_ID);
    m_discordApiUrl =
        ini_get_string("discord", "api_url", ConfigDefaults::DISCORD_API_URL);
    m_discordUploadScreenshots =
        ini_get_bool("discord", "upload_screenshots",
                     ConfigDefaults::DISCORD_UPLOAD_SCREENSHOTS);
    m_discordUploadMovies = ini_get_bool("discord", "upload_movies",
                                         ConfigDefaults::DISCORD_UPLOAD_MOVIES);

    // Read general settings
    m_keepLogs =
        ini_get_bool("general", "keep_logs", ConfigDefaults::KEEP_LOGS);
    m_logLevel =
        ini_get_string("general", "log_level", ConfigDefaults::LOG_LEVEL);

    // Validate log level
    // Valid values: debug, info, warn, error
    if (m_logLevel != "debug" && m_logLevel != "info" && m_logLevel != "warn" &&
        m_logLevel != "error") {
        Logger::get().warn()
            << "Invalid log_level: '" << m_logLevel
            << "' (valid levels: debug, info, warn, error). Resetting to "
               "default (info)."
            << endl;
        m_logLevel = ConfigDefaults::LOG_LEVEL;
    }

    // Read check interval (seconds), with minimum enforcement using std::max
    m_checkIntervalSeconds = std::max(
        static_cast<int>(ini_get_long("general", "check_interval",
                                      ConfigDefaults::CHECK_INTERVAL_SECONDS)),
        ConfigDefaults::CHECK_INTERVAL_MINIMUM);

    // ========================================================================
    // Validate configuration and disable invalid channels
    // ========================================================================

    // Validate Telegram upload mode
    if (!ConfigDefaults::isUploadModeValid(m_telegramUploadMode)) {
        Logger::get().warn()
            << "Invalid Telegram upload mode: '" << m_telegramUploadMode
            << "' (valid modes: compressed, original, both). Resetting to "
               "default."
            << endl;
        m_telegramUploadMode = ConfigDefaults::TELEGRAM_UPLOAD_MODE;
    }

    // Validate Telegram configuration
    if (m_telegramEnabled && !ConfigDefaults::isTelegramValid(
                                 m_telegramBotToken, m_telegramChatId)) {
        Logger::get().warn()
            << "Telegram channel disabled: Invalid or missing configuration "
               "(bot_token and/or chat_id are not set or are set to "
               "'undefined')"
            << endl;
        m_telegramEnabled = false;
    }

    // Validate Ntfy configuration
    if (m_ntfyEnabled && !ConfigDefaults::isNtfyValid(m_ntfyTopic)) {
        Logger::get().warn()
            << "Ntfy channel disabled: Invalid or missing configuration (topic "
               "is not set)"
            << endl;
        m_ntfyEnabled = false;
    }

    // Validate Discord configuration
    if (m_discordEnabled && !ConfigDefaults::isDiscordValid(
                                m_discordBotToken, m_discordChannelId)) {
        Logger::get().warn()
            << "discord channel disabled: Invalid or missing configuration "
               "(bot_token and/or channel_id are not set or are set to "
               "'undefined')"
            << endl;
        m_discordEnabled = false;
    }

    // Check if at least one channel is enabled
    if (!m_telegramEnabled && !m_ntfyEnabled && !m_discordEnabled) {
        return false;  // Indicate failure - no valid upload channel available
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

std::string_view Config::getDiscordBotToken() const noexcept {
    return m_discordBotToken;
}

std::string_view Config::getDiscordChannelId() const noexcept {
    return m_discordChannelId;
}

std::string_view Config::getDiscordApiUrl() const noexcept {
    return m_discordApiUrl;
}

std::string_view Config::getNtfyUrl() const noexcept { return m_ntfyUrl; }

std::string_view Config::getNtfyTopic() const noexcept { return m_ntfyTopic; }

std::string_view Config::getNtfyToken() const noexcept { return m_ntfyToken; }

std::string_view Config::getNtfyPriority() const noexcept {
    return m_ntfyPriority;
}
