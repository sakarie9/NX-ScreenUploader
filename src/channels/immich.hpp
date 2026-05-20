#pragma once

#include <string>
#include <string_view>

namespace ImmichChannel {

// Default values for Immich channel configuration
namespace Defaults {
constexpr std::string_view SERVER_URL = "";
constexpr std::string_view API_KEY = "";
constexpr bool UPLOAD_SCREENSHOTS = true;
constexpr bool UPLOAD_MOVIES = false;
}  // namespace Defaults

// INI section name
inline constexpr const char* SECTION = "immich";

// Immich channel configuration
struct Config {
    bool enabled{false};

    std::string serverUrl{Defaults::SERVER_URL};
    std::string apiKey{Defaults::API_KEY};
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

/// Upload a file to Immich
[[nodiscard]] bool send(std::string_view path);

}  // namespace ImmichChannel
