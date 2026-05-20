#pragma once

#include <string>
#include <string_view>

namespace TelegramChannel {

// Default values for Telegram channel configuration
namespace Defaults {
constexpr std::string_view BOT_TOKEN = "";
constexpr std::string_view CHAT_ID = "";
constexpr std::string_view API_URL = "https://api.telegram.org";
constexpr bool UPLOAD_SCREENSHOTS = true;
constexpr bool UPLOAD_MOVIES = true;
constexpr std::string_view UPLOAD_MODE = "compressed";
}  // namespace Defaults

// INI section name
inline constexpr const char* SECTION = "telegram";

// Telegram channel configuration
struct Config {
    bool enabled{false};

    std::string botToken{Defaults::BOT_TOKEN};
    std::string chatId{Defaults::CHAT_ID};
    std::string apiUrl{Defaults::API_URL};
    bool uploadScreenshots{Defaults::UPLOAD_SCREENSHOTS};
    bool uploadMovies{Defaults::UPLOAD_MOVIES};
    std::string uploadMode{Defaults::UPLOAD_MODE};

    /**
     * Load configuration from INI file
     * @param configPath Path to the config.ini file
     * @return true on success
     */
    bool load(const char* configPath);

    /**
     * Check if the configuration is valid.
     * Logs a warning if invalid.
     * @return true if configuration is valid
     */
    bool validate();
};

/// Upload a file to Telegram (handles compressed/original/both modes
/// internally)
[[nodiscard]] bool send(std::string_view path);

}  // namespace TelegramChannel
