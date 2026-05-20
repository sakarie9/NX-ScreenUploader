#pragma once

#include <string>
#include <string_view>

namespace DiscordChannel {

// Default values for Discord channel configuration
namespace Defaults {
constexpr std::string_view BOT_TOKEN = "";
constexpr std::string_view CHANNEL_ID = "";
constexpr std::string_view API_URL = "https://discord.com/api/v10";
constexpr bool UPLOAD_SCREENSHOTS = true;
constexpr bool UPLOAD_MOVIES = false;
}  // namespace Defaults

// INI section name
inline constexpr const char* SECTION = "discord";

// Discord channel configuration
struct Config {
    bool enabled{false};

    std::string botToken{Defaults::BOT_TOKEN};
    std::string channelId{Defaults::CHANNEL_ID};
    std::string apiUrl{Defaults::API_URL};
    bool uploadScreenshots{Defaults::UPLOAD_SCREENSHOTS};
    bool uploadMovies{Defaults::UPLOAD_MOVIES};

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

/// Upload a file to Discord
[[nodiscard]] bool send(std::string_view path);

}  // namespace DiscordChannel
